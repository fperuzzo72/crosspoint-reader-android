#include "AndroidTouchDevice.h"

namespace crosspoint::android {

AndroidTouchDevice& AndroidTouchDevice::instance() {
  static AndroidTouchDevice dev;
  return dev;
}

bool AndroidTouchDevice::begin(uint16_t, uint16_t, TouchTuning) {
  // Nem geometria nem tuning se aplicam: o Kotlin entrega coordenadas ja
  // normalizadas e os limiares sao os do sistema. Os parametros ficam na
  // assinatura porque o HalGPIO hospedado chama os dois alvos igual.
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
