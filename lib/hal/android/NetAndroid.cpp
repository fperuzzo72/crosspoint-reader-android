#include "NetAndroid.h"

#include <android/log.h>

#include "JniBridge.h"

namespace crosspoint::android {

namespace {

struct Bridge {
  jclass cls = nullptr;  // referencia GLOBAL
  jmethodID isOnline = nullptr;
  jmethodID localIp = nullptr;
  bool ready = false;
};

Bridge g;

bool ensure(JNIEnv* env) {
  if (g.ready) {
    return true;
  }
  jclass local = findAppClass(env, "org/crosspoint/hibreak/CrossPointNet");
  if (local == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, "CrossPoint", "CrossPointNet not found");
    return false;
  }
  g.cls = static_cast<jclass>(env->NewGlobalRef(local));
  env->DeleteLocalRef(local);
  g.isOnline = env->GetStaticMethodID(g.cls, "isOnline", "()Z");
  g.localIp = env->GetStaticMethodID(g.cls, "localIpV4", "()I");
  g.ready = g.isOnline != nullptr && g.localIp != nullptr;
  if (!g.ready) {
    env->ExceptionClear();
  }
  return g.ready;
}

}  // namespace

bool netIsOnline() {
  JniAttach attach;
  if (!attach || !ensure(attach.env())) {
    return false;
  }
  const jboolean r = attach.env()->CallStaticBooleanMethod(g.cls, g.isOnline);
  if (attach.env()->ExceptionCheck()) {
    attach.env()->ExceptionClear();
    return false;
  }
  return r == JNI_TRUE;
}

uint32_t netLocalIpV4() {
  JniAttach attach;
  if (!attach || !ensure(attach.env())) {
    return 0;
  }
  const jint r = attach.env()->CallStaticIntMethod(g.cls, g.localIp);
  if (attach.env()->ExceptionCheck()) {
    attach.env()->ExceptionClear();
    return 0;
  }
  return static_cast<uint32_t>(r);
}

}  // namespace crosspoint::android
