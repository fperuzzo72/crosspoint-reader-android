// esp_http_client over the Kotlin bridge.
//
// Replaces HttpClientPosix.cpp on this target. That one speaks HTTP over raw
// sockets and refuses https explicitly, which rules out almost every real OPDS
// catalogue and KOReader sync. Here the request crosses into Kotlin, where TLS,
// the system trust store, proxies and VPNs already exist.
//
// The API's shape does not change at all for the caller: init, set_header,
// open, fetch_headers, read, close, cleanup. CrossPoint does not know it
// crossed a language boundary, and should not have to.

#include <BoardConfig.h>

#if FREEINK_DEVICE_HIBREAK

#include <android/log.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "JniBridge.h"
#include "arduino-shim/esp_http_client.h"

namespace {

// CrossPointHttp's methods, resolved on the first request and kept. A
// jmethodID is stable while the class stays loaded, and ours stays loaded for
// the life of the process.
struct Bridge {
  jclass cls = nullptr;  // GLOBAL reference
  jmethodID open = nullptr;
  jmethodID write = nullptr;
  jmethodID finish = nullptr;
  jmethodID read = nullptr;
  jmethodID header = nullptr;
  jmethodID contentLength = nullptr;
  jmethodID close = nullptr;
  bool ready = false;
};

Bridge g_bridge;

bool ensureBridge(JNIEnv* env) {
  if (g_bridge.ready) {
    return true;
  }
  jclass local = crosspoint::android::findAppClass(env, "org/crosspoint/hibreak/CrossPointHttp");
  if (local == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, "CrossPoint", "CrossPointHttp not found");
    return false;
  }
  g_bridge.cls = static_cast<jclass>(env->NewGlobalRef(local));
  env->DeleteLocalRef(local);

  // A wrong signature here is NOT a silent null return: GetStaticMethodID
  // throws NoSuchMethodError and leaves the exception PENDING. The next JNI
  // call made with a pending exception aborts the VM, and the process dies
  // without passing through any error handling of ours.
  //
  // Hence each lookup clears its own, rather than one clear at the end: the
  // second lookup would already be the first one's "next JNI call".
  auto lookup = [&](const char* name, const char* sig) -> jmethodID {
    jmethodID m = env->GetStaticMethodID(g_bridge.cls, name, sig);
    if (m == nullptr) {
      env->ExceptionClear();
      __android_log_print(ANDROID_LOG_ERROR, "CrossPoint", "CrossPointHttp.%s %s did not resolve", name, sig);
    }
    return m;
  };

  g_bridge.open = lookup("open", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZI)I");
  g_bridge.write = lookup("write", "(I[BI)I");
  g_bridge.finish = lookup("finish", "(I)I");
  g_bridge.read = lookup("read", "(I[B)I");
  g_bridge.header = lookup("header", "(ILjava/lang/String;)Ljava/lang/String;");
  g_bridge.contentLength = lookup("contentLength", "(I)J");
  g_bridge.close = lookup("close", "(I)V");
  g_bridge.ready = g_bridge.open && g_bridge.write && g_bridge.finish && g_bridge.read && g_bridge.header &&
                   g_bridge.contentLength && g_bridge.close;
  return g_bridge.ready;
}

const char* methodName(const esp_http_client_method_t m) {
  switch (m) {
    case HTTP_METHOD_POST:
      return "POST";
    case HTTP_METHOD_PUT:
      return "PUT";
    case HTTP_METHOD_PATCH:
      return "PATCH";
    case HTTP_METHOD_DELETE:
      return "DELETE";
    case HTTP_METHOD_HEAD:
      return "HEAD";
    case HTTP_METHOD_GET:
    default:
      return "GET";
  }
}

}  // namespace

// The handle CrossPoint carries. On the Kotlin side a connection is an integer
// in a map; here we keep that integer plus whatever the API exposes by
// pointer.
struct esp_http_client {
  std::string url;
  std::string headers;  // "Key: Value" lines
  esp_http_client_method_t method = HTTP_METHOD_GET;
  int timeoutMs = 15000;
  int handle = -1;
  int status = -1;
  int64_t contentLength = -1;
  int64_t consumed = 0;
  bool bodyPending = false;
  std::string headerScratch;  // owns the char* get_header hands back
};

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
  if (config == nullptr || config->url == nullptr) {
    return nullptr;
  }
  auto* c = new esp_http_client();
  c->url = config->url;
  c->method = config->method;
  if (config->timeout_ms > 0) {
    c->timeoutMs = config->timeout_ms;
  }
  return c;
}

esp_err_t esp_http_client_cleanup(const esp_http_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  esp_http_client_close(client);
  delete client;
  return ESP_OK;
}

esp_err_t esp_http_client_set_header(const esp_http_client_handle_t client, const char* key, const char* value) {
  if (client == nullptr || key == nullptr || value == nullptr) {
    return ESP_FAIL;
  }
  client->headers += key;
  client->headers += ": ";
  client->headers += value;
  client->headers += '\n';
  return ESP_OK;
}

esp_err_t esp_http_client_set_url(const esp_http_client_handle_t client, const char* url) {
  if (client == nullptr || url == nullptr) {
    return ESP_FAIL;
  }
  client->url = url;
  return ESP_OK;
}

esp_err_t esp_http_client_set_method(const esp_http_client_handle_t client, const esp_http_client_method_t method) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  client->method = method;
  return ESP_OK;
}

esp_err_t esp_http_client_set_redirection(const esp_http_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  // Kotlin does not follow redirects on purpose (see CrossPointHttp.open):
  // CrossPoint decides, and already has its own logic. Here we only swap the
  // URL for the Location and the next open goes to the new place.
  char* location = nullptr;
  if (esp_http_client_get_header(client, "Location", &location) == ESP_OK && location != nullptr &&
      location[0] != '\0') {
    std::fprintf(stderr, "[http] redirecting to %s\n", location);
    std::fflush(stderr);
    client->url = location;
    return ESP_OK;
  }
  std::fprintf(stderr, "[http] redirect status with no Location\n");
  std::fflush(stderr);
  return ESP_FAIL;
}

esp_err_t esp_http_client_open(const esp_http_client_handle_t client, const int writeLen) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  crosspoint::android::JniAttach attach;
  if (!attach || !ensureBridge(attach.env())) {
    std::fprintf(stderr, "[http] open: JNI bridge unavailable\n");
    std::fflush(stderr);
    return ESP_FAIL;
  }
  JNIEnv* env = attach.env();

  jstring jmethod = env->NewStringUTF(methodName(client->method));
  jstring jurl = env->NewStringUTF(client->url.c_str());
  jstring jheaders = env->NewStringUTF(client->headers.c_str());
  const jint h = env->CallStaticIntMethod(g_bridge.cls, g_bridge.open, jmethod, jurl, jheaders,
                                          writeLen > 0 ? JNI_TRUE : JNI_FALSE, client->timeoutMs);
  env->DeleteLocalRef(jmethod);
  env->DeleteLocalRef(jurl);
  env->DeleteLocalRef(jheaders);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return ESP_FAIL;
  }
  std::fprintf(stderr, "[http] open %s %s -> handle %d\n", methodName(client->method), client->url.c_str(),
               static_cast<int>(h));
  std::fflush(stderr);
  if (h < 0) {
    return ESP_FAIL;
  }
  client->handle = h;
  client->status = -1;
  client->contentLength = -1;
  client->consumed = 0;
  // With no body to write the response can be requested straight away; with a
  // body the caller writes first and fetch_headers closes the request.
  client->bodyPending = writeLen > 0;
  return ESP_OK;
}

int esp_http_client_write(const esp_http_client_handle_t client, const char* buffer, const int len) {
  if (client == nullptr || client->handle < 0 || buffer == nullptr || len <= 0) {
    return -1;
  }
  crosspoint::android::JniAttach attach;
  if (!attach || !g_bridge.ready) {
    return -1;
  }
  JNIEnv* env = attach.env();
  jbyteArray arr = env->NewByteArray(len);
  if (arr == nullptr) {
    return -1;
  }
  env->SetByteArrayRegion(arr, 0, len, reinterpret_cast<const jbyte*>(buffer));
  const jint written = env->CallStaticIntMethod(g_bridge.cls, g_bridge.write, client->handle, arr, len);
  env->DeleteLocalRef(arr);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return -1;
  }
  return written;
}

int64_t esp_http_client_fetch_headers(const esp_http_client_handle_t client) {
  if (client == nullptr || client->handle < 0) {
    return -1;
  }
  crosspoint::android::JniAttach attach;
  if (!attach || !g_bridge.ready) {
    return -1;
  }
  JNIEnv* env = attach.env();
  // This is where the request actually goes out: Kotlin's openConnection is
  // lazy and responseCode is what opens the socket, does the handshake and
  // reads the headers.
  client->status = env->CallStaticIntMethod(g_bridge.cls, g_bridge.finish, client->handle);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    std::fprintf(stderr, "[http] finish threw\n");
    std::fflush(stderr);
    return -1;
  }
  client->bodyPending = false;
  if (client->status < 0) {
    std::fprintf(stderr, "[http] finish returned an error (-1); see logcat for CrossPointHttp\n");
    std::fflush(stderr);
    return -1;
  }
  client->contentLength = env->CallStaticLongMethod(g_bridge.cls, g_bridge.contentLength, client->handle);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    client->contentLength = -1;
  }
  std::fprintf(stderr, "[http] status %d, content-length %lld\n", client->status,
               static_cast<long long>(client->contentLength));
  std::fflush(stderr);
  return client->contentLength;
}

int esp_http_client_get_status_code(const esp_http_client_handle_t client) {
  return client == nullptr ? -1 : client->status;
}

int64_t esp_http_client_get_content_length(const esp_http_client_handle_t client) {
  return client == nullptr ? -1 : client->contentLength;
}

int esp_http_client_read(const esp_http_client_handle_t client, char* buffer, const int len) {
  if (client == nullptr || client->handle < 0 || buffer == nullptr || len <= 0) {
    return -1;
  }
  crosspoint::android::JniAttach attach;
  if (!attach || !g_bridge.ready) {
    return -1;
  }
  JNIEnv* env = attach.env();
  jbyteArray arr = env->NewByteArray(len);
  if (arr == nullptr) {
    return -1;
  }
  const jint n = env->CallStaticIntMethod(g_bridge.cls, g_bridge.read, client->handle, arr);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    env->DeleteLocalRef(arr);
    return -1;
  }
  if (n > 0) {
    env->GetByteArrayRegion(arr, 0, n, reinterpret_cast<jbyte*>(buffer));
    client->consumed += n;
  }
  env->DeleteLocalRef(arr);
  return n;
}

esp_err_t esp_http_client_perform(const esp_http_client_handle_t client) {
  if (esp_http_client_open(client, 0) != ESP_OK) {
    return ESP_FAIL;
  }
  return esp_http_client_fetch_headers(client) < 0 && client->status < 0 ? ESP_FAIL : ESP_OK;
}

bool esp_http_client_is_complete_data_received(const esp_http_client_handle_t client) {
  if (client == nullptr) {
    return false;
  }
  // With no Content-Length (a chunked response) there is no way to assert it;
  // the caller already handles the end by read() returning 0.
  return client->contentLength < 0 || client->consumed >= client->contentLength;
}

esp_err_t esp_http_client_close(const esp_http_client_handle_t client) {
  if (client == nullptr || client->handle < 0) {
    return ESP_OK;
  }
  crosspoint::android::JniAttach attach;
  if (attach && g_bridge.ready) {
    attach.env()->CallStaticVoidMethod(g_bridge.cls, g_bridge.close, client->handle);
    if (attach.env()->ExceptionCheck()) {
      attach.env()->ExceptionClear();
    }
  }
  std::fprintf(stderr, "[http] close handle %d, %lld bytes\n", client->handle,
               static_cast<long long>(client->consumed));
  std::fflush(stderr);
  client->handle = -1;
  return ESP_OK;
}

esp_err_t esp_http_client_get_header(const esp_http_client_handle_t client, const char* key, char** value) {
  if (client == nullptr || key == nullptr || value == nullptr || client->handle < 0) {
    return ESP_FAIL;
  }
  *value = nullptr;
  crosspoint::android::JniAttach attach;
  if (!attach || !g_bridge.ready) {
    return ESP_FAIL;
  }
  JNIEnv* env = attach.env();
  jstring jkey = env->NewStringUTF(key);
  auto jval = static_cast<jstring>(env->CallStaticObjectMethod(g_bridge.cls, g_bridge.header, client->handle, jkey));
  env->DeleteLocalRef(jkey);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return ESP_FAIL;
  }
  if (jval == nullptr) {
    return ESP_OK;  // a missing header is not an error
  }
  const char* utf = env->GetStringUTFChars(jval, nullptr);
  // esp_http_client's contract hands back a pointer the caller does NOT free,
  // valid until the next call. A member of the client owns it.
  client->headerScratch = utf != nullptr ? utf : "";
  env->ReleaseStringUTFChars(jval, utf);
  env->DeleteLocalRef(jval);
  *value = client->headerScratch.data();
  return ESP_OK;
}

#endif  // FREEINK_DEVICE_HIBREAK
