#include "AndroidTouchDevice.h"

namespace crosspoint::android {

std::mutex AndroidTouchDevice::mtx;
GestureResult AndroidTouchDevice::pending{};
bool AndroidTouchDevice::open = false;
bool AndroidTouchDevice::contactDown = false;
bool AndroidTouchDevice::suppressed = false;

AndroidTouchDevice& AndroidTouchDevice::instance() {
  static AndroidTouchDevice dev;
  return dev;
}

bool AndroidTouchDevice::begin(uint16_t, uint16_t, TouchTuning) {
  // Neither geometry nor tuning apply: Kotlin delivers already-normalised
  // coordinates and the thresholds are the system's. The parameters stay in the
  // signature because the hosted HalGPIO calls both targets the same way.
  std::lock_guard<std::mutex> lock(mtx);
  open = true;
  return true;
}

void AndroidTouchDevice::end() {
  std::lock_guard<std::mutex> lock(mtx);
  open = false;
  pending = {};
  contactDown = false;
  suppressed = false;
}

GestureResult AndroidTouchDevice::update(int) {
  std::lock_guard<std::mutex> lock(mtx);
  const GestureResult g = pending;
  pending = {};
  return g;
}

void AndroidTouchDevice::postGesture(const GestureResult& g) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!open) {
    return;
  }
  if (suppressed && g.kind != Gesture::LongPress) {
    return;
  }
  if (g.kind == Gesture::LongPress) {
    suppressed = true;
  }
  pending = g;
}

void AndroidTouchDevice::setContactDown(const bool down) {
  std::lock_guard<std::mutex> lock(mtx);
  contactDown = down;
  if (!down) {
    suppressed = false;
  }
}

void AndroidTouchDevice::suppressContact() {
  std::lock_guard<std::mutex> lock(mtx);
  suppressed = true;
  pending = {};
}

bool AndroidTouchDevice::isContactDown() const {
  std::lock_guard<std::mutex> lock(mtx);
  return contactDown;
}

}  // namespace crosspoint::android
