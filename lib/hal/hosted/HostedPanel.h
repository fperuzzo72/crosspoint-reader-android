#pragma once
#include <BoardConfig.h>

// Panel selector for the hosted targets.
//
// The Kindle and the HiBreak share nothing in hardware: one talks to the
// kernel's EPDC through /dev/fb0, the other hands pixels to SurfaceFlinger. But
// the SHAPE HalDisplay needs is the same (compose a frame, stage it, present),
// which is why both share HalDisplayHosted.cpp instead of carrying 345 lines
// each.
//
// A new hosted target goes here and implements the same method surface. There
// is deliberately no virtual base class: the choice is made at compile time,
// one binary per device, and a vtable would only pay for indirection on a
// decision the preprocessor already made.

#include "HostedTouch.h"

#if FREEINK_DEVICE_KINDLE
#include "kindle/KindleFrameBuffer.h"
#include "kindle/KindleTouch.h"
namespace crosspoint::hosted {
using Panel = kindle::KindleFrameBuffer;
using Waveform = kindle::Waveform;
// PENDING in the Kindle repository: KindleTouch.h still declares Gesture,
// GestureResult and TouchTuning inside crosspoint::kindle, while HostedTouch.h
// declares the same three in crosspoint::hosted. Until the backport moves the
// Kindle's here, this branch does not compile. It is deliberate rather than
// papered over with aliases: the two targets must speak ONE gesture language,
// not two identical ones.
using Touch = kindle::KindleTouchDevice;
inline constexpr uint16_t PANEL_WIDTH = kindle::KT3_WIDTH;
inline constexpr uint16_t PANEL_HEIGHT = kindle::KT3_HEIGHT;
}  // namespace crosspoint::hosted

#elif FREEINK_DEVICE_HIBREAK
#include "android/AndroidPanel.h"
#include "android/AndroidTouchDevice.h"
namespace crosspoint::hosted {
using Panel = android::AndroidPanel;
using Waveform = android::Waveform;
using Touch = android::AndroidTouchDevice;
inline constexpr uint16_t PANEL_WIDTH = android::HIBREAK_WIDTH;
inline constexpr uint16_t PANEL_HEIGHT = android::HIBREAK_HEIGHT;
}  // namespace crosspoint::hosted
#endif
