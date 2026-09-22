#include "AndroidPanel.h"

#include <android/log.h>
#include <android/native_window.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace crosspoint::android {

namespace {
constexpr size_t STAGE_BYTES = static_cast<size_t>(HIBREAK_WIDTH) * HIBREAK_HEIGHT;
}  // namespace

// Definition of the static members. See the header: the state belongs to the
// process, not the object, because there is one panel and two objects pointing
// at it is what gave a black screen.
std::mutex AndroidPanel::mtx;
ANativeWindow* AndroidPanel::win = nullptr;
uint8_t* AndroidPanel::stage = nullptr;
Waveform AndroidPanel::lastWaveform = Waveform::Half;
bool AndroidPanel::stageHasContent = false;

AndroidPanel& AndroidPanel::instance() {
  static AndroidPanel panel;
  return panel;
}

AndroidPanel::~AndroidPanel() { end(); }

bool AndroidPanel::begin() {
  std::fprintf(stderr, "[panel] begin()\n");
  std::fflush(stderr);
  std::lock_guard<std::mutex> lock(mtx);
  if (stage == nullptr) {
    stage = static_cast<uint8_t*>(std::malloc(STAGE_BYTES));
    if (stage == nullptr) {
      return false;
    }
    std::memset(stage, hosted::GRAY_WHITE, STAGE_BYTES);
    stageHasContent = false;
  }
  return true;
}

void AndroidPanel::end() {
  std::lock_guard<std::mutex> lock(mtx);
  if (win != nullptr) {
    ANativeWindow_release(win);
    win = nullptr;
  }
  std::free(stage);
  stage = nullptr;
  stageHasContent = false;
}

bool AndroidPanel::reopen() {
  // There is no connection to re-establish: the surface comes and goes on its
  // own through attachSurface. This exists so the hosted HalDisplay can call
  // the same thing on both targets.
  std::lock_guard<std::mutex> lock(mtx);
  return stage != nullptr;
}

void AndroidPanel::attachSurface(ANativeWindow* window) {
  std::lock_guard<std::mutex> lock(mtx);
  if (win != nullptr) {
    ANativeWindow_release(win);
  }
  win = window;
  if (win == nullptr) {
    return;
  }
  ANativeWindow_acquire(win);
  // The geometry is pinned to the panel size so the compositor does not scale:
  // a page of 1bpp text resampled loses exactly the sharpness e-ink exists to
  // give.
  const int32_t geo = ANativeWindow_setBuffersGeometry(win, HIBREAK_WIDTH, HIBREAK_HEIGHT, WINDOW_FORMAT_RGBX_8888);
  std::fprintf(stderr, "[panel] superficie anexada, geometria %dx%d -> %d\n", HIBREAK_WIDTH, HIBREAK_HEIGHT, geo);
  std::fflush(stderr);
  // The surface may have arrived after the frame. Re-present rather than
  // leaving the new buffer with whatever the compositor put in it.
  if (stageHasContent) {
    present();
  }
}

void AndroidPanel::detachSurface() {
  std::lock_guard<std::mutex> lock(mtx);
  if (win != nullptr) {
    ANativeWindow_release(win);
    win = nullptr;
  }
}

bool AndroidPanel::stageFrameLocked(const uint8_t* frame) {
  if (frame == nullptr || stage == nullptr) {
    return false;
  }
  hosted::expand1bppToGray8(frame, stage, HIBREAK_WIDTH, HIBREAK_HEIGHT, HIBREAK_WIDTH_BYTES, HIBREAK_WIDTH);
  stageHasContent = true;
  return true;
}

bool AndroidPanel::stageFrame(const uint8_t* frame) {
  std::lock_guard<std::mutex> lock(mtx);
  return stageFrameLocked(frame);
}

bool AndroidPanel::stageGrayOverlay(const uint8_t* lsbPlane, const uint8_t* msbPlane) {
  std::lock_guard<std::mutex> lock(mtx);
  if (stage == nullptr) {
    return false;
  }
  hosted::overlayGrayPlanesOnGray8(lsbPlane, msbPlane, stage, HIBREAK_WIDTH, HIBREAK_HEIGHT, HIBREAK_WIDTH_BYTES,
                                   HIBREAK_WIDTH);
  return true;
}

bool AndroidPanel::refresh(const Waveform waveform) {
  std::lock_guard<std::mutex> lock(mtx);
  lastWaveform = waveform;
  return present();
}

bool AndroidPanel::display(const uint8_t* frame, const Waveform waveform) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!stageFrameLocked(frame)) {
    return false;
  }
  lastWaveform = waveform;
  return present();
}

bool AndroidPanel::displayStart(const uint8_t* frame, const Waveform waveform) {
  display(frame, waveform);
  // Nothing in flight worth waiting for; see the declaration's comment.
  return false;
}

uint8_t AndroidPanel::peekPixel(const uint16_t x, const uint16_t y) const {
  std::lock_guard<std::mutex> lock(mtx);
  if (stage == nullptr || x >= HIBREAK_WIDTH || y >= HIBREAK_HEIGHT) {
    return 0;
  }
  return stage[static_cast<size_t>(y) * HIBREAK_WIDTH + x];
}

bool AndroidPanel::present() {
  // The first presentations are the ones that say whether the pixel path is
  // closed. After that they go quiet, or a page turn fills the log.
  static int reported = 0;
  if (reported < 5) {
    ++reported;
    std::fprintf(stderr, "[panel] present #%d win=%p stage=%p temConteudo=%d\n", reported, static_cast<void*>(win),
                 static_cast<void*>(stage), stageHasContent ? 1 : 0);
    std::fflush(stderr);
  }
  if (win == nullptr || stage == nullptr) {
    // No surface is not an error: the Activity may be paused, and the frame is
    // kept for when it returns.
    return true;
  }
  ANativeWindow_Buffer buf;
  if (ANativeWindow_lock(win, &buf, nullptr) != 0) {
    return false;
  }
  // If the compositor handed back a different geometry than requested, honour
  // its own and paint only the intersection: writing at the size we asked for
  // would overrun the buffer it gave us.
  const int32_t w = buf.width < HIBREAK_WIDTH ? buf.width : HIBREAK_WIDTH;
  const int32_t h = buf.height < HIBREAK_HEIGHT ? buf.height : HIBREAK_HEIGHT;
  auto* pixels = static_cast<uint8_t*>(buf.bits);
  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* src = stage + static_cast<size_t>(y) * HIBREAK_WIDTH;
    // buf.stride is in PIXELS for 32-bit formats, not bytes.
    uint32_t* out = reinterpret_cast<uint32_t*>(pixels + static_cast<size_t>(y) * buf.stride * 4);
    for (int32_t x = 0; x < w; ++x) {
      const uint32_t g = src[x];
      // RGBX_8888 little endian: R in the low byte, X in the high one. Gray
      // goes into all three channels; X is ignored but filled with 0xFF for
      // hygiene.
      out[x] = 0xFF000000u | (g << 16) | (g << 8) | g;
    }
  }
  ANativeWindow_unlockAndPost(win);
  return true;
}

}  // namespace crosspoint::android
