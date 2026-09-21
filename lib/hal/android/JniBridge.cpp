#include "JniBridge.h"

#include <android/log.h>

#include <string>

namespace crosspoint::android {

namespace {
JavaVM* g_vm = nullptr;

// O class loader do aplicativo, capturado no JNI_OnLoad.
//
// FindClass resolve contra o class loader ASSOCIADO A THREAD. Na thread que
// carrega a biblioteca isso e o loader do aplicativo e tudo e encontrado; numa
// thread nativa anexada depois, o JNI oferece o loader do SISTEMA, que so
// conhece java.* e android.*. Uma classe nossa vira ClassNotFoundException.
//
// A saida padrao e guardar o loader de quando dava certo e chamar loadClass
// nele explicitamente.
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
    // A thread ja era conhecida; nao somos donos do anexo.
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
  // Caminho rapido: funciona na thread que carregou a biblioteca.
  if (jclass direct = env->FindClass(name); direct != nullptr) {
    return direct;
  }
  env->ExceptionClear();
  if (g_classLoader == nullptr || g_loadClass == nullptr) {
    return nullptr;
  }
  // loadClass espera nome com pontos, FindClass espera com barras.
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
    __android_log_print(ANDROID_LOG_ERROR, "CrossPoint", "JNI_OnLoad: classe do app nao encontrada");
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

// Chamado pela JVM quando o System.loadLibrary("crosspoint") termina, na
// thread que o chamou, que e a da Activity. E a unica janela em que o class
// loader do aplicativo esta acessivel sem truque.
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  crosspoint::android::setJavaVM(vm);
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
    crosspoint::android::captureClassLoader(env);
  }
  return JNI_VERSION_1_6;
}
