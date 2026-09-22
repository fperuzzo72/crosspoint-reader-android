#pragma once
#include <cstdint>

// Touch vocabulary for the hosted targets.
//
// Identical to what the Kindle backend already used, moved here because there
// is nothing Kindle about it: it is how HalGPIO speaks of gestures, and both
// devices speak the same language even though they classify by completely
// different routes (raw evdev on one side, Android's GestureDetector on the
// other).

namespace crosspoint::hosted {

enum class Gesture : uint8_t {
  None,
  // Fires on release: a brief contact that stayed put.
  Tap,
  // Fires WHILE the finger is still down, once the threshold passes. The rest
  // of that contact is suppressed, or the release would also read as a tap and
  // dismiss whatever the long press just opened.
  LongPress,
  // Fires on release: a contact that travelled far enough to be a drag.
  Swipe,
};

struct GestureResult {
  Gesture kind = Gesture::None;
  // Normalised to 0..1 so the caller never sees a panel pixel. For a swipe,
  // start is where the finger landed and end is where it left.
  float nx = 0.0F;
  float ny = 0.0F;
  float nxEnd = 0.0F;
  float nyEnd = 0.0F;
  uint32_t heldMs = 0;

  explicit operator bool() const { return kind != Gesture::None; }
};

struct TouchTuning {
  uint16_t slopPx = 20;
  uint16_t swipeMinPx = 45;
  uint32_t longPressMs = 550;
  uint32_t tapMaxMs = 500;
};

}  // namespace crosspoint::hosted
