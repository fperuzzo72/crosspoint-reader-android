// esp_http_client sobre a ponte Kotlin.
//
// Substitui o HttpClientPosix.cpp neste alvo. Aquele fala HTTP sobre socket
// cru e recusa https explicitamente, o que deixa de fora quase todo catalogo
// OPDS real e a sincronizacao KOReader. Aqui a requisicao atravessa para o
// Kotlin, onde TLS, a loja de certificados do sistema, proxy e VPN ja existem.
//
// A forma da API nao muda em nada para quem chama: init, set_header, open,
// fetch_headers, read, close, cleanup. O CrossPoint nao sabe que atravessou
// uma fronteira de linguagem, e nao deveria mesmo saber.

#include <BoardConfig.h>

#if FREEINK_DEVICE_HIBREAK

#include <android/log.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "JniBridge.h"
#include "arduino-shim/esp_http_client.h"

namespace {

// Metodos do CrossPointHttp, resolvidos na primeira requisicao e mantidos.
// jmethodID e estavel enquanto a classe estiver carregada, e a nossa fica
// carregada enquanto o processo viver.
struct Bridge {
  jclass cls = nullptr;  // referencia GLOBAL
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
    __android_log_print(ANDROID_LOG_ERROR, "CrossPoint", "CrossPointHttp nao encontrada");
    return false;
  }
  g_bridge.cls = static_cast<jclass>(env->NewGlobalRef(local));
  env->DeleteLocalRef(local);

  // Uma assinatura errada aqui NAO e um retorno nulo silencioso: o
  // GetStaticMethodID lanca NoSuchMethodError e deixa a excecao PENDENTE. A
  // proxima chamada JNI feita com excecao pendente faz a VM abortar, e o
  // processo morre sem passar por nenhum tratamento de erro nosso.
  //
  // Por isso cada busca limpa a sua, em vez de uma limpeza no fim: a segunda
  // busca ja seria a "proxima chamada JNI" da primeira.
  auto lookup = [&](const char* name, const char* sig) -> jmethodID {
    jmethodID m = env->GetStaticMethodID(g_bridge.cls, name, sig);
    if (m == nullptr) {
      env->ExceptionClear();
      __android_log_print(ANDROID_LOG_ERROR, "CrossPoint", "CrossPointHttp.%s %s nao resolveu", name, sig);
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

// O handle que o CrossPoint carrega. Do lado Kotlin a conexao e um inteiro
// num mapa; aqui guardamos esse inteiro mais o que a API expoe por ponteiro.
struct esp_http_client {
  std::string url;
  std::string headers;  // linhas "Chave: Valor"
  esp_http_client_method_t method = HTTP_METHOD_GET;
  int timeoutMs = 15000;
  int handle = -1;
  int status = -1;
  int64_t contentLength = -1;
  int64_t consumed = 0;
  bool bodyPending = false;
  std::string headerScratch;  // dono do char* que get_header devolve
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
  // O Kotlin nao segue redirecionamento de proposito (ver CrossPointHttp.open):
  // quem decide e o CrossPoint, que ja tem a propria logica. Aqui so trocamos
  // a URL pelo Location e a proxima abertura vai para o lugar novo.
  char* location = nullptr;
  if (esp_http_client_get_header(client, "Location", &location) == ESP_OK && location != nullptr &&
      location[0] != '\0') {
    std::fprintf(stderr, "[http] redirecionando para %s\n", location);
    std::fflush(stderr);
    client->url = location;
    return ESP_OK;
  }
  std::fprintf(stderr, "[http] status de redirecionamento sem Location\n");
  std::fflush(stderr);
  return ESP_FAIL;
}

esp_err_t esp_http_client_open(const esp_http_client_handle_t client, const int writeLen) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  crosspoint::android::JniAttach attach;
  if (!attach || !ensureBridge(attach.env())) {
    std::fprintf(stderr, "[http] open: ponte JNI indisponivel\n");
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
  // Sem corpo para escrever, a resposta ja pode ser pedida; com corpo, quem
  // chama escreve antes e o fetch_headers fecha a requisicao.
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
  // Antes e depois da chamada, porque e aqui que a requisicao sai de verdade:
  // o openConnection do lado Kotlin e preguicoso e o responseCode e que abre o
  // socket, faz o handshake e le os cabecalhos. Sem as duas linhas, morrer
  // dentro da chamada e voltar com erro dela produzem o mesmo log.
  std::fprintf(stderr, "[http] finish(handle %d) chamando...\n", client->handle);
  std::fflush(stderr);
  client->status = env->CallStaticIntMethod(g_bridge.cls, g_bridge.finish, client->handle);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    std::fprintf(stderr, "[http] finish lancou excecao\n");
    std::fflush(stderr);
    return -1;
  }
  client->bodyPending = false;
  if (client->status < 0) {
    std::fprintf(stderr, "[http] finish devolveu erro (-1); ver logcat por CrossPointHttp\n");
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
  // Uma linha a cada 64 leituras, mais a primeira e o fim. O suficiente para
  // o log dizer ate onde a transferencia chegou antes de morrer, sem encher o
  // arquivo com uma linha por pedaco.
  static int reads = 0;
  if (++reads <= 1 || n <= 0 || reads % 64 == 0) {
    std::fprintf(stderr, "[http] read #%d n=%d total=%lld\n", reads, static_cast<int>(n),
                 static_cast<long long>(client->consumed));
    std::fflush(stderr);
  }
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
  // Sem Content-Length (resposta em chunks) nao ha como afirmar; o chamador
  // ja trata o fim pelo read() devolvendo 0.
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
    return ESP_OK;  // cabecalho ausente nao e erro
  }
  const char* utf = env->GetStringUTFChars(jval, nullptr);
  // O contrato do esp_http_client devolve um ponteiro que o chamador NAO
  // libera, valido ate a proxima chamada. Um membro do cliente e dono dele.
  client->headerScratch = utf != nullptr ? utf : "";
  env->ReleaseStringUTFChars(jval, utf);
  env->DeleteLocalRef(jval);
  *value = client->headerScratch.data();
  return ESP_OK;
}

#endif  // FREEINK_DEVICE_HIBREAK
