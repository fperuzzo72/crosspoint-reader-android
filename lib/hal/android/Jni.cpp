// A ponte JNI: tudo o que o Kotlin chama, e nada mais.
//
// A divisao de trabalho e deliberada. O Kotlin fica com o que o Android faz
// melhor do que nos: ciclo de vida da Activity, a Surface, o GestureDetector
// com os limiares do proprio aparelho, e mais para frente o scoped storage. O
// C++ fica com o CrossPoint inteiro, que nao sabe nada disso e nao precisa.
//
// A fronteira tem quatro travessias e nenhuma delas carrega objeto Java para
// dentro: Surface vira ANativeWindow, gesto vira struct, e acabou. Nada aqui
// guarda JNIEnv, porque JNIEnv e por thread e a thread do CrossPoint nao e a
// thread que chamou.

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <atomic>
#include <thread>

#include "AndroidPanel.h"
#include "AndroidTouchDevice.h"

// Definidos em src/main.cpp, compartilhado com todos os outros alvos.
void setup();
void loop();

namespace {

std::atomic<bool> g_running{false};

void readerThread() {
  __android_log_print(ANDROID_LOG_INFO, "CrossPoint", "reader thread iniciando");
  setup();
  while (g_running.load(std::memory_order_relaxed)) {
    loop();
  }
  __android_log_print(ANDROID_LOG_INFO, "CrossPoint", "reader thread encerrada");
}

}  // namespace

extern "C" {

// A Surface aparece e some com a Activity, varias vezes na vida do processo.
// Passar null aqui e o caminho normal de pausa, nao um erro.
JNIEXPORT void JNICALL Java_org_crosspoint_hibreak_CrossPointNative_nativeSetSurface(JNIEnv* env, jclass,
                                                                                     jobject surface) {
  auto& panel = crosspoint::android::AndroidPanel::instance();
  if (surface == nullptr) {
    panel.detachSurface();
    return;
  }
  ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
  // attachSurface faz o proprio acquire; o fromSurface ja veio com uma
  // referencia que e nossa para soltar.
  panel.attachSurface(win);
  if (win != nullptr) {
    ANativeWindow_release(win);
  }
}

// Um gesto ja classificado pelo GestureDetector do Android. kind casa com
// crosspoint::hosted::Gesture: 0 nenhum, 1 toque, 2 toque longo, 3 swipe.
// Coordenadas normalizadas em 0..1; o C++ nunca ve pixel.
JNIEXPORT void JNICALL Java_org_crosspoint_hibreak_CrossPointNative_nativeGesture(JNIEnv*, jclass, jint kind, jfloat nx,
                                                                                 jfloat ny, jfloat nxEnd, jfloat nyEnd,
                                                                                 jint heldMs) {
  crosspoint::hosted::GestureResult g;
  g.kind = static_cast<crosspoint::hosted::Gesture>(kind);
  g.nx = nx;
  g.ny = ny;
  g.nxEnd = nxEnd;
  g.nyEnd = nyEnd;
  g.heldMs = static_cast<uint32_t>(heldMs < 0 ? 0 : heldMs);
  crosspoint::android::AndroidTouchDevice::instance().postGesture(g);
}

// Dedo encostado ou nao, independente de gesto. O CrossPoint usa isto para
// saber que ha um contato em andamento antes de ele virar alguma coisa.
JNIEXPORT void JNICALL Java_org_crosspoint_hibreak_CrossPointNative_nativeContact(JNIEnv*, jclass, jboolean down) {
  crosspoint::android::AndroidTouchDevice::instance().setContactDown(down == JNI_TRUE);
}

// Sobe a thread do CrossPoint. Idempotente: a Activity pode ser recriada sem
// o processo morrer, e reiniciar o leitor perderia a posicao de leitura.
JNIEXPORT void JNICALL Java_org_crosspoint_hibreak_CrossPointNative_nativeStart(JNIEnv*, jclass) {
  bool expected = false;
  if (!g_running.compare_exchange_strong(expected, true)) {
    return;
  }
  std::thread(readerThread).detach();
}

}  // extern "C"
