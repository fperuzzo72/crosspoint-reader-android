#pragma once

// Display backend for the Bigme HiBreak Pro.
//
// On ESP32 targets FreeInk drives a raw panel over SPI/i80. On a Kindle the
// kernel's EPDC owns the panel and userspace gets a /dev/fb0. Here, neither:
// the surface comes from SurfaceFlinger, handed over by a Kotlin Activity
// through JNI, and the vendor's xrz framework decides the waveform.
//
// What that changes against the other two: the surface may NOT EXIST at any
// moment. An Android Activity is paused and destroyed out from under the
// process while CrossPoint's thread stays alive and painting. So every method
// here treats "no surface" as a normal state rather than an error, and the
// composed frame survives in `stage` to be re-presented when the surface
// returns.

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "hosted/HostedGray.h"

struct ANativeWindow;

namespace crosspoint::android {

// Measured on the device, not assumed: 824x1648 at 300 dpi.
// 824 / 8 = 103 exactly, so there is no row padding on the source side.
inline constexpr uint16_t HIBREAK_WIDTH = 824;
inline constexpr uint16_t HIBREAK_HEIGHT = 1648;
inline constexpr uint16_t HIBREAK_WIDTH_BYTES = HIBREAK_WIDTH / 8;
inline constexpr uint32_t HIBREAK_BUFFER_SIZE = static_cast<uint32_t>(HIBREAK_WIDTH_BYTES) * HIBREAK_HEIGHT;

// Mirrors HalDisplay::RefreshMode so the hosted HalDisplay can forward its
// argument straight through. In v1 the value is recorded and unused: the path
// is ordinary Android and the system picks the waveform. It stays because
// XrzEinkManager is one reflective call away and the mode ladder has already
// been surveyed (177/178/179/180).
enum class Waveform : uint8_t { Full, Half, Fast };

class AndroidPanel {
 public:
  AndroidPanel() = default;
  ~AndroidPanel();

  AndroidPanel(const AndroidPanel&) = delete;
  AndroidPanel& operator=(const AndroidPanel&) = delete;

  // Allocates the composition frame. Does not wait for a surface: it arrives
  // when the Activity decides, and until then everything composes normally and
  // nothing shows.
  bool begin();
  void end();
  bool isOpen() const { return stage != nullptr; }

  uint16_t width() const { return HIBREAK_WIDTH; }
  uint16_t height() const { return HIBREAK_HEIGHT; }

  bool display(const uint8_t* frame, Waveform waveform);
  // There is no completion marker on the Android side: the post is async but
  // there is nothing to wait for. Always returns false, which in HalDisplay's
  // contract means "finished inline, do not call waitComplete".
  bool displayStart(const uint8_t* frame, Waveform waveform);
  bool reopen();

  bool stageFrame(const uint8_t* frame);
  bool stageGrayOverlay(const uint8_t* lsbPlane, const uint8_t* msbPlane);
  bool refresh(Waveform waveform);

  // Nobody else paints our Surface: the compositor composes, and what it puts
  // on top does not replace our pixels in our buffer. The question this answers
  // on the Kindle (did the framework blank the screen we share?) does not exist
  // here.
  bool panelContentWasReplaced() const { return false; }

  uint8_t peekPixel(uint16_t x, uint16_t y) const;
  void waitComplete() {}
  void deepSleep() {}

  // Called from JNI, on the Activity's thread, not CrossPoint's.
  // They take the same mutex as presentation.
  void attachSurface(ANativeWindow* window);
  void detachSurface();

  // Single instance: JNI needs to reach the panel without threading a pointer
  // through the whole HalDisplay chain.
  static AndroidPanel& instance();

 private:
  // All three assume the mutex is already held. display() must compose and
  // present under ONE lock: between the two halves the Activity can swap the
  // surface, and the frame would go out half-written.
  bool stageFrameLocked(const uint8_t* frame);
  bool present();

  // THE STATE IS STATIC, and this is the fix for a bug that gave a black
  // screen.
  //
  // HalDisplay declares `crosspoint::hosted::Panel panel;` as a BY-VALUE
  // member, while JNI reaches the panel through instance(). They were two
  // objects: the Surface went to the singleton and the reader painted into the
  // member, so nothing reached the screen and nothing failed anywhere.
  //
  // There is exactly one panel in this process, so static state is the truth
  // rather than a trick: any AndroidPanel is a handle onto the same panel. The
  // alternative was making HalDisplay's member a reference, which changes a
  // signature shared with the Kindle branch over a problem only this target
  // has.
  static std::mutex mtx;
  static ANativeWindow* win;
  // The composed frame in 8bpp gray, panel-sized. 824*1648 = 1.36MB,
  // irrelevant on a phone, and what lets stageFrame and stageGrayOverlay reach
  // the panel in a single presentation, as on the Kindle.
  static uint8_t* stage;
  static Waveform lastWaveform;
  // The surface arrived after the last composed frame: the next attachSurface
  // re-presents rather than leaving the screen with whatever was there.
  static bool stageHasContent;
};

}  // namespace crosspoint::android
