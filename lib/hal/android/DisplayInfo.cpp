#include "DisplayInfo.h"

#include <cstdio>

namespace crosspoint::android {
namespace {

constexpr uint16_t roundUpToMultipleOfEight(const uint16_t w) { return static_cast<uint16_t>(((w + 7) / 8) * 8); }

DisplayInfo makeInfo(const uint16_t w, const uint16_t h, const uint16_t dpi) {
  const uint16_t frameW = roundUpToMultipleOfEight(w);
  return DisplayInfo{w,
                     h,
                     frameW,
                     static_cast<uint16_t>(frameW / 8),
                     static_cast<uint32_t>(frameW / 8) * h,
                     dpi};
}

DisplayInfo g_info = makeInfo(DEFAULT_PANEL_WIDTH, DEFAULT_PANEL_HEIGHT, DEFAULT_DENSITY_DPI);

}  // namespace

const DisplayInfo& displayInfo() { return g_info; }

bool setDisplayInfo(const int width, const int height, const int densityDpi) {
  const int frameW = ((width + 7) / 8) * 8;
  if (width <= 0 || height <= 0 || frameW > MAX_FRAME_WIDTH || height > MAX_FRAME_HEIGHT) {
    std::fprintf(stderr, "[display] %dx%d recusado (frame %d, maximo %dx%d); seguindo com %dx%d\n", width, height,
                 frameW, MAX_FRAME_WIDTH, MAX_FRAME_HEIGHT, g_info.panelWidth, g_info.panelHeight);
    std::fflush(stderr);
    return false;
  }
  // A density of zero is survivable in a way a bad size is not: it only costs
  // the UI its scale, so it falls back instead of refusing the geometry.
  const uint16_t dpi = densityDpi > 0 ? static_cast<uint16_t>(densityDpi) : DEFAULT_DENSITY_DPI;
  g_info = makeInfo(static_cast<uint16_t>(width), static_cast<uint16_t>(height), dpi);
  std::fprintf(stderr, "[display] painel %ux%u, frame %ux%u (%u B/linha), %u dpi\n", g_info.panelWidth,
               g_info.panelHeight, g_info.frameWidth, g_info.panelHeight, g_info.frameWidthBytes, g_info.densityDpi);
  std::fflush(stderr);
  return true;
}

}  // namespace crosspoint::android
