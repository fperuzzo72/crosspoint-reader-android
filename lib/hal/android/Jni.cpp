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
#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

namespace crosspoint_storage {
void setRoot(const char* path);
}

#include "AndroidPanel.h"
#include "AndroidTouchDevice.h"

// Definidos em src/main.cpp, compartilhado com todos os outros alvos.
void setup();
void loop();

namespace {

std::atomic<bool> g_running{false};

// Pilha da thread do leitor.
//
// O bionic da 1MB por padrao a uma pthread. Isso e pouco para esta arvore: no
// ESP32 as tarefas tem pilha dimensionada a mao justamente porque o parser de
// XML, o layout de capitulo e a cadeia de render descem fundo, e o CrossPoint
// ja carrega um TaskWatchdog e medicoes de high water mark por causa disso.
//
// Um estouro de pilha aqui nao aparece como erro: aparece como SIGSEGV num
// endereco qualquer, em um ponto do codigo que MUDA conforme a profundidade em
// que se estava. Foi exatamente o sintoma observado: a mesma operacao morrendo
// ora dentro do JNI, ora antes dele.
//
// 8MB e o que a thread principal de um processo Android ja tem, entao nao e
// numero inventado: e igualar o leitor ao que o sistema considera normal.
constexpr size_t READER_STACK_BYTES = 8u * 1024u * 1024u;

void* readerTrampoline(void*);

// Log em arquivo, alem do logcat.
//
// Nao e redundancia: o adb deste aparelho cai depois de poucos comandos, entao
// logcat e um canal que nao da para contar. Um arquivo dentro do diretorio do
// aplicativo e legivel por qualquer gerenciador de arquivos, sem cabo, sem
// depuracao ligada e sem nada do lado do computador.
//
// Tudo o que o shim POSIX escreve ja vai para stderr, entao redirecionar
// stderr para ca pega o tronco inteiro de graca, incluindo as linhas que o
// HalSystemHosted imprime sobre bateria e deteccao de suspensao.
void logLine(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

void openLogFile(const std::string& root) {
  const std::string path = root + "/crosspoint.log";
  const std::string prev = root + "/crosspoint.log.anterior";

  // A execucao ANTERIOR e guardada antes de truncar, e isto nao e zelo: o
  // caso em que o log importa e quando o processo morre, e a unica forma de
  // ler o arquivo e reabrindo o aplicativo, que era exatamente o que apagava
  // a evidencia. Duas execucoes bastam, entao nao ha rotacao de verdade: a
  // atual e a de antes dela.
  std::remove(prev.c_str());
  std::rename(path.c_str(), prev.c_str());

  // "w" e nao "a": um log que cresce sem limite num aparelho que ninguem vai
  // limpar e pior do que um log que so tem a ultima execucao.
  if (std::freopen(path.c_str(), "w", stderr) == nullptr) {
    __android_log_print(ANDROID_LOG_WARN, "CrossPoint", "nao consegui abrir %s", path.c_str());
    return;
  }
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  const std::time_t now = std::time(nullptr);
  logLine("=== CrossPoint hibreak-dev, %s", std::ctime(&now));
  __android_log_print(ANDROID_LOG_INFO, "CrossPoint", "log em %s", path.c_str());
}

// Captura o motivo da morte antes que ela aconteca em silencio.
//
// Um processo Android pode sumir por dois caminhos muito diferentes e o log
// nao distinguia: SIGSEGV e erro de memoria no codigo nativo, SIGABRT e a
// runtime detectando uma violacao (erro de uso do JNI, abort do C++, falta de
// memoria na ART). Saber qual dos dois vale mais do que qualquer deducao
// minha sobre o codigo, e ja errei duas vezes tentando deduzir.
//
// O handler faz o minimo possivel porque esta num contexto de sinal: escreve
// com write() direto no descritor, sem fprintf nem malloc, e depois restaura
// a acao padrao e se mata de novo para o sistema ainda gerar a lapide.
void crashHandler(int sig, siginfo_t* info, void*) {
  char buf[256];
  const int n = snprintf(buf, sizeof(buf), "\n*** morreu com sinal %d (%s), endereco %p ***\n", sig, strsignal(sig),
                         info != nullptr ? info->si_addr : nullptr);
  if (n > 0) {
    write(fileno(stderr), buf, static_cast<size_t>(n));
    fsync(fileno(stderr));
  }
  signal(sig, SIG_DFL);
  raise(sig);
}

void installCrashHandler() {
  struct sigaction sa{};
  sa.sa_sigaction = crashHandler;
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigemptyset(&sa.sa_mask);
  for (const int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE}) {
    sigaction(sig, &sa, nullptr);
  }
}

void readerThread();

void* readerTrampoline(void*) {
  readerThread();
  return nullptr;
}

void readerThread() {
  installCrashHandler();
  logLine("[jni] thread do leitor iniciando, pilha de %zu MB", READER_STACK_BYTES / (1024u * 1024u));
  setup();
  logLine("[jni] setup() retornou; entrando no loop");
  unsigned long long iterations = 0;
  while (g_running.load(std::memory_order_relaxed)) {
    loop();
    // As primeiras voltas sao as que dizem se o leitor esta vivo ou travado.
    // Depois disso, calar: o log e para diagnostico, nao para telemetria.
    if (++iterations <= 3) {
      logLine("[jni] loop() volta %llu", iterations);
    }
  }
  logLine("[jni] thread do leitor encerrada");
}

}  // namespace

extern "C" {

// A Surface aparece e some com a Activity, varias vezes na vida do processo.
// Passar null aqui e o caminho normal de pausa, nao um erro.
JNIEXPORT void JNICALL Java_org_crosspoint_hibreak_CrossPointNative_nativeSetSurface(JNIEnv* env, jclass,
                                                                                     jobject surface) {
  auto& panel = crosspoint::android::AndroidPanel::instance();
  if (surface == nullptr) {
    logLine("[jni] superficie retirada");
    panel.detachSurface();
    return;
  }
  ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
  logLine("[jni] superficie entregue: %p", static_cast<void*>(win));
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

// A raiz do armazenamento. Tem de ser chamada ANTES do nativeStart(): o
// setup() do CrossPoint ja monta o navegador de arquivos e le a biblioteca,
// e uma raiz errada nessa hora e uma biblioteca vazia.
//
// Quem decide o caminho e o Kotlin, porque so o framework sabe qual e o
// diretorio deste aplicativo. Na v1 e o getExternalFilesDir(), que e
// gravavel sem permissao nenhuma e visivel num gerenciador de arquivos. A
// pasta de ebooks escolhida pelo usuario, sob scoped storage, e outro
// problema e vem depois.
JNIEXPORT void JNICALL Java_org_crosspoint_hibreak_CrossPointNative_nativeSetStorageRoot(JNIEnv* env, jclass,
                                                                                         jstring path) {
  if (path == nullptr) {
    return;
  }
  const char* utf = env->GetStringUTFChars(path, nullptr);
  if (utf != nullptr) {
    crosspoint_storage::setRoot(utf);
    openLogFile(utf);
    logLine("[jni] raiz do armazenamento: %s", utf);
    env->ReleaseStringUTFChars(path, utf);
  }
}

// Sobe a thread do CrossPoint. Idempotente: a Activity pode ser recriada sem
// o processo morrer, e reiniciar o leitor perderia a posicao de leitura.
JNIEXPORT void JNICALL Java_org_crosspoint_hibreak_CrossPointNative_nativeStart(JNIEnv*, jclass) {
  bool expected = false;
  if (!g_running.compare_exchange_strong(expected, true)) {
    return;
  }
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, READER_STACK_BYTES);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t tid;
  const int rc = pthread_create(&tid, &attr, readerTrampoline, nullptr);
  pthread_attr_destroy(&attr);
  if (rc != 0) {
    __android_log_print(ANDROID_LOG_ERROR, "CrossPoint", "pthread_create falhou: %d", rc);
    g_running.store(false);
  }
}

}  // extern "C"
