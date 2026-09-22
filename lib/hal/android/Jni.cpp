// The JNI bridge: everything Kotlin calls, and nothing else.
//
// The division of labour is deliberate. Kotlin keeps what Android does better
// than we would: the Activity lifecycle, the Surface, GestureDetector with the
// device's own thresholds, and storage. C++ keeps the whole of CrossPoint,
// which knows none of that and does not need to.
//
// The boundary has a handful of crossings and none of them carries a Java
// object inward: a Surface becomes an ANativeWindow, a gesture becomes a
// struct, and that is all. Nothing here caches a JNIEnv, because JNIEnv is
// per-thread and CrossPoint's thread is not the thread that called.

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

// Defined in src/main.cpp, shared with every other target.
void setup();
void loop();

namespace {

std::atomic<bool> g_running{false};

// Reader thread stack size.
//
// bionic gives a pthread 1MB by default. That is not enough for this tree: on
// the ESP32 tasks have hand-sized stacks precisely because the XML parser,
// chapter layout and the render chain go deep, and CrossPoint already carries a
// TaskWatchdog and high water mark measurements because of it.
//
// A stack overflow here does not show up as an error: it shows up as SIGSEGV at
// some address, at a point in the code that MOVES with how deep you were. That
// was exactly the symptom observed: the same operation dying sometimes inside
// JNI and sometimes before reaching it.
//
// 8MB is what a process's main thread on Android already has, so it is not an
// invented number: it levels the reader with what the system considers
// ordinary.
constexpr size_t READER_STACK_BYTES = 8u * 1024u * 1024u;

void* readerTrampoline(void*);

// A log file, alongside logcat.
//
// Not redundancy: this device's adb drops after a few commands, so logcat is a
// channel you cannot count on. A file inside the app's directory is readable by
// any file manager, with no cable, no debugging enabled and nothing on the
// computer side.
//
// Everything the POSIX shim writes already goes to stderr, so redirecting
// stderr here captures the whole trunk for free, including the lines
// HalSystemHosted prints about the battery and suspend detection.
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

  // The PREVIOUS run is kept before truncating, and this is not fussiness: the
  // case where the log matters is when the process dies, and the only way to
  // read the file is to reopen the app, which was exactly what erased the
  // evidence. Two runs are enough, so there is no real rotation: the current
  // one and the one before it.
  std::remove(prev.c_str());
  std::rename(path.c_str(), prev.c_str());

  // "w" and not "a": a log that grows without bound on a device nobody will
  // clean up is worse than a log holding only the last run.
  if (std::freopen(path.c_str(), "w", stderr) == nullptr) {
    __android_log_print(ANDROID_LOG_WARN, "CrossPoint", "could not open %s", path.c_str());
    return;
  }
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  const std::time_t now = std::time(nullptr);
  logLine("=== CrossPoint hibreak-dev, %s", std::ctime(&now));
  __android_log_print(ANDROID_LOG_INFO, "CrossPoint", "log at %s", path.c_str());
}

// Capture the reason for death before it happens in silence.
//
// An Android process can vanish down two very different paths and the log did
// not distinguish them: SIGSEGV is a memory error in native code, SIGABRT is
// the runtime detecting a violation (JNI misuse, a C++ abort, an ART
// allocation failure). Knowing which is worth more than any amount of reasoning
// about the code, which had already been wrong twice by the time this was
// written.
//
// The handler does the bare minimum because it runs in signal context: it
// writes with write() straight to the descriptor, no fprintf and no malloc,
// then restores the default action and re-raises so the system still produces
// its tombstone.
void crashHandler(int sig, siginfo_t* info, void*) {
  char buf[256];
  const int n = snprintf(buf, sizeof(buf), "\n*** died with signal %d (%s), address %p ***\n", sig, strsignal(sig),
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
  logLine("[jni] reader thread starting, %zu MB stack", READER_STACK_BYTES / (1024u * 1024u));
  setup();
  logLine("[jni] setup() returned; entering the loop");
  unsigned long long iterations = 0;
  while (g_running.load(std::memory_order_relaxed)) {
    loop();
    // The first turns are the ones that say whether the reader is alive or
    // wedged. After that, silence: the log is for diagnosis, not telemetry.
    if (++iterations <= 3) {
      logLine("[jni] loop() turn %llu", iterations);
    }
  }
  logLine("[jni] reader thread finished");
}

}  // namespace

extern "C" {

// The Surface appears and disappears with the Activity, many times over the
// life of the process. Passing null here is the normal pause path, not an
// error.
JNIEXPORT void JNICALL Java_org_crosspoint_hibreak_CrossPointNative_nativeSetSurface(JNIEnv* env, jclass,
                                                                                     jobject surface) {
  auto& panel = crosspoint::android::AndroidPanel::instance();
  if (surface == nullptr) {
    logLine("[jni] surface withdrawn");
    panel.detachSurface();
    return;
  }
  ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
  logLine("[jni] surface handed over: %p", static_cast<void*>(win));
  // attachSurface does its own acquire; fromSurface already came with a
  // reference that is ours to release.
  panel.attachSurface(win);
  if (win != nullptr) {
    ANativeWindow_release(win);
  }
}

// A gesture already classified by Android's GestureDetector. kind matches
// crosspoint::hosted::Gesture: 0 none, 1 tap, 2 long press, 3 swipe.
// Coordinates normalised to 0..1; C++ never sees a pixel.
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

// Finger down or not, independent of any gesture. CrossPoint uses this to know
// a contact is in progress before it becomes anything.
JNIEXPORT void JNICALL Java_org_crosspoint_hibreak_CrossPointNative_nativeContact(JNIEnv*, jclass, jboolean down) {
  crosspoint::android::AndroidTouchDevice::instance().setContactDown(down == JNI_TRUE);
}

// The storage root. Must be called BEFORE nativeStart(): CrossPoint's setup()
// already builds the file browser and reads the library, and a wrong root at
// that moment is an empty library.
//
// Kotlin decides the path, because only the framework knows this app's
// directory.
JNIEXPORT void JNICALL Java_org_crosspoint_hibreak_CrossPointNative_nativeSetStorageRoot(JNIEnv* env, jclass,
                                                                                         jstring path) {
  if (path == nullptr) {
    return;
  }
  const char* utf = env->GetStringUTFChars(path, nullptr);
  if (utf != nullptr) {
    crosspoint_storage::setRoot(utf);
    openLogFile(utf);
    logLine("[jni] storage root: %s", utf);
    env->ReleaseStringUTFChars(path, utf);
  }
}

// Starts CrossPoint's thread. Idempotent: the Activity can be recreated
// without the process dying, and restarting the reader would lose the reading
// position.
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
    __android_log_print(ANDROID_LOG_ERROR, "CrossPoint", "pthread_create failed: %d", rc);
    g_running.store(false);
  }
}

}  // extern "C"
