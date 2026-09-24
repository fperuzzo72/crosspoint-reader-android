#pragma once

#include <cstdint>

// The panel, measured instead of compiled in.
//
// Every size here used to be a constexpr sized for one device, which is why a
// build ran on one phone and painted a corner of any other. The Activity knows
// the real numbers before the reader starts, so it hands them over the same way
// it hands over the storage root: once, before nativeStart().
//
// Two widths, and they are only the same number when the panel's width divides
// by 8. `panelWidth` is the window's own, and it is what the compositor must be
// asked for, or it resamples every frame to fit. `frameWidth` is the 1bpp
// composition buffer, rounded up, because every index in the paint path divides
// by 8. The spare columns live in the buffer and never reach the glass.
namespace crosspoint::android {

struct DisplayInfo {
  uint16_t panelWidth;
  uint16_t panelHeight;
  uint16_t frameWidth;
  uint16_t frameWidthBytes;
  uint32_t frameBytes;
  // Physical density, for anything that should stay the same SIZE rather than
  // the same number of pixels. The UI metrics were written in pixels for a
  // 160 dpi-ish panel and shrink on a denser one without it.
  uint16_t densityDpi;
};

// The HiBreak Pro, which is what this defaults to when the Activity says
// nothing: a build that loses the handshake keeps the behaviour it had.
inline constexpr uint16_t DEFAULT_PANEL_WIDTH = 824;
inline constexpr uint16_t DEFAULT_PANEL_HEIGHT = 1648;
inline constexpr uint16_t DEFAULT_DENSITY_DPI = 300;

// Sized for the largest panel this build expects to meet, because the frame is
// allocated before any Activity has spoken. 1408x1872 covers a 1404-wide Boox
// and every phone panel smaller than it; 330 KB of 1bpp frame is nothing on a
// device with gigabytes, and the alternative is an allocation that cannot grow.
inline constexpr uint16_t MAX_FRAME_WIDTH = 1408;
inline constexpr uint16_t MAX_FRAME_HEIGHT = 1872;
inline constexpr uint32_t MAX_FRAME_BYTES = static_cast<uint32_t>(MAX_FRAME_WIDTH / 8) * MAX_FRAME_HEIGHT;

const DisplayInfo& displayInfo();

// Called from the Activity through JNI, before the reader thread exists.
// Out-of-range values are refused rather than clamped: a zero or a number past
// the frame this build allocated means the handshake is wrong, and running on
// the default is better than painting outside the buffer.
bool setDisplayInfo(int width, int height, int densityDpi);

}  // namespace crosspoint::android
