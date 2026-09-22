#pragma once

// HiBreak touch: a mailbox, not a classifier.
//
// On the Kindle the backend reads the raw evdev stream and classifies taps,
// long presses and swipes itself, because nothing else will. On Android that
// piece already exists and is better than the one we would write: the
// framework's GestureDetector knows the device's slop, the system's long-press
// threshold and its fling velocity. So classification happens in Kotlin and
// what crosses JNI is a finished gesture.
//
// What remains here is the thread handoff: the Activity delivers gestures on
// the UI thread and CrossPoint consumes them on its own. One gesture at a time,
// most recent wins, which is the same edge contract HalGPIO already expects
// from update().

#include <cstdint>
#include <mutex>

#include "hosted/HostedTouch.h"

namespace crosspoint::android {

using hosted::Gesture;
using hosted::GestureResult;
using hosted::TouchTuning;

class AndroidTouchDevice {
 public:
  bool begin(uint16_t panelWidth, uint16_t panelHeight, TouchTuning tuning = {});
  void end();
  bool isOpen() const { return open; }
  bool reopen() { return open; }

  // Consumes the pending gesture, if any. timeoutMs is ignored: there is no
  // descriptor to wait on, Kotlin pushes when it happens.
  GestureResult update(int timeoutMs = 0);

  void suppressContact();
  bool isContactDown() const;

  // Called from JNI, on the UI thread.
  void postGesture(const GestureResult& g);
  void setContactDown(bool down);

  static AndroidTouchDevice& instance();

 private:
  // Static for the same reason as AndroidPanel: HalGPIO declares
  // `crosspoint::hosted::Touch touchDevice;` by value while JNI reaches it
  // through instance(). They were two objects, and gestures went to the one
  // nobody read.
  static std::mutex mtx;
  static GestureResult pending;
  static bool open;
  static bool contactDown;
  // A long press has already been delivered for this contact: everything up to
  // release is discarded, or the release would read as a tap too.
  static bool suppressed;
};

}  // namespace crosspoint::android
