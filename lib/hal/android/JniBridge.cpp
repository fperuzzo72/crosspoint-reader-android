#include "JniBridge.h"

#include <android/log.h>

#include <string>

namespace crosspoint::android {

namespace {
JavaVM* g_vm = nullptr;

// The application's class loader, captured in JNI_OnLoad.
//
// FindClass resolves against the class loader ASSOCIATED WITH THE THREAD. On
// the thread that loads the library that is the app's loader and everything is
// found; on a native thread attached later, JNI offers the SYSTEM loader, which
// only knows java.* and android.*. One of our classes becomes
// ClassNotFoundException.
//
// The standard way out is to keep the loader from when it worked and call
// loadClass on it explicitly.
jobject g_classLoader = nullptr;
jmethodID g_loadClass = nullptr;
}  // namespace

void setJavaVM(JavaVM* vm) { g_vm = vm; }
JavaVM* javaVM() { return g_vm; }

JniAttach::JniAttach() {
  if (g_vm == nullptr) {
    return;
  }
  const jint status = g_vm->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);
  if (status == JNI_OK) {
    // The thread was already known; we do not own the attach.
    return;
  }
  if (status != JNI_EDETACHED) {
    env_ = nullptr;
    return;
  }
  JavaVMAttachArgs args{JNI_VERSION_1_6, "crosspoint-reader", nullptr};
  if (g_vm->AttachCurrentThread(&env_, &args) != JNI_OK) {
    env_ = nullptr;
    return;
  }
  attachedHere_ = true;
}

JniAttach::~JniAttach() {
  if (attachedHere_ && g_vm != nullptr) {
    g_vm->DetachCurrentThread();
  }
}

jclass findAppClass(JNIEnv* env, const char* name) {
  if (env == nullptr) {
    return nullptr;
  }
  // Fast path: works on the thread that loaded the library.
  if (jclass direct = env->FindClass(name); direct != nullptr) {
    return direct;
  }
  env->ExceptionClear();
  if (g_classLoader == nullptr || g_loadClass == nullptr) {
    return nullptr;
  }
  // loadClass wants a dotted name, FindClass wants slashes.
  std::string dotted(name);
  for (char& c : dotted) {
    if (c == '/') c = '.';
  }
  jstring jname = env->NewStringUTF(dotted.c_str());
  auto cls = static_cast<jclass>(env->CallObjectMethod(g_classLoader, g_loadClass, jname));
  env->DeleteLocalRef(jname);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return nullptr;
  }
  return cls;
}

void captureClassLoader(JNIEnv* env) {
  jclass anyAppClass = env->FindClass("org/crosspoint/hibreak/CrossPointNative");
  if (anyAppClass == nullptr) {
    env->ExceptionClear();
    __android_log_print(ANDROID_LOG_ERROR, "CrossPoint", "JNI_OnLoad: app class not found");
    return;
  }
  jclass classClass = env->GetObjectClass(anyAppClass);
  jmethodID getLoader = env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject loader = env->CallObjectMethod(anyAppClass, getLoader);
  if (loader == nullptr) {
    return;
  }
  g_classLoader = env->NewGlobalRef(loader);
  jclass loaderClass = env->GetObjectClass(loader);
  g_loadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
}

}  // namespace crosspoint::android

// Called by the JVM when System.loadLibrary("crosspoint") completes, on the
// thread that called it, which is the Activity's. It is the only window where
// the application's class loader is reachable without a trick.
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  crosspoint::android::setJavaVM(vm);
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
    crosspoint::android::captureClassLoader(env);
  }
  return JNI_VERSION_1_6;
}
