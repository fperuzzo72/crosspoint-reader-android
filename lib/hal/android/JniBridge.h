#pragma once

// Calling the JVM from C++, which is the opposite direction from Jni.cpp.
//
// Until now the bridge was one-way: Kotlin called native functions and C++
// obeyed. Each of those calls arrives with a ready JNIEnv*, valid on that
// thread and for that call, and nothing needs to be kept.
//
// Networking inverts that. CrossPoint asks for a GET from inside the reader
// loop, which runs on a std::thread we created: a NATIVE thread the JVM has
// never heard of. It has no JNIEnv, and getting one requires attaching first.
//
// Two rules this file exists so nobody forgets:
//
//   - JNIEnv is PER THREAD and must never be cached across threads. What you
//     cache is the JavaVM*, which belongs to the whole process.
//   - A jclass or jobject obtained in a call is a LOCAL reference and dies when
//     that call ends. What survives is an explicit global reference.

#include <jni.h>

namespace crosspoint::android {

// Captured in JNI_OnLoad; see the .cpp for why FindClass alone is not enough.
void captureClassLoader(JNIEnv* env);

// Cached in JNI_OnLoad. The one point where the JVM introduces itself without
// anyone having to ask.
void setJavaVM(JavaVM* vm);
JavaVM* javaVM();

// Attaches the current thread to the JVM for its lifetime, and detaches on
// scope exit IF this object is the one that attached it.
//
// The condition matters: detaching a thread the JVM already knew (the UI
// thread, say) would break the caller above us. So whoever attached is who
// detaches, and nobody else.
class JniAttach {
 public:
  JniAttach();
  ~JniAttach();

  JniAttach(const JniAttach&) = delete;
  JniAttach& operator=(const JniAttach&) = delete;

  // nullptr when the JVM is unavailable or the attach failed. Every caller must
  // check: a network request without a JVM is an ordinary error here, not an
  // impossible condition.
  JNIEnv* env() const { return env_; }
  explicit operator bool() const { return env_ != nullptr; }

 private:
  JNIEnv* env_ = nullptr;
  bool attachedHere_ = false;
};

// A global reference to the class, resolved once and kept alive. Resolving via
// FindClass on every call would be slow and, worse, would FAIL on the reader
// thread: the class loader JNI offers an attached thread cannot see the
// application's classes, only the system's. Hence resolution happens in
// JNI_OnLoad, which runs on the thread that loaded the library.
jclass findAppClass(JNIEnv* env, const char* name);

}  // namespace crosspoint::android
