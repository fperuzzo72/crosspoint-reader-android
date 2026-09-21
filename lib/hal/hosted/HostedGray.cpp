#include "HostedGray.h"

#include <cstddef>

namespace crosspoint::hosted {

void expand1bppToGray8(const uint8_t* src, uint8_t* dst, const uint16_t width, const uint16_t height,
                       const uint16_t srcRowBytes, const uint32_t dstRowBytes) {
  for (uint16_t y = 0; y < height; ++y) {
    const uint8_t* row = src + static_cast<size_t>(y) * srcRowBytes;
    uint8_t* out = dst + static_cast<size_t>(y) * dstRowBytes;
    for (uint16_t x = 0; x < width; ++x) {
      const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7u));
      out[x] = (row[x >> 3] & bit) ? GRAY_WHITE : GRAY_BLACK;
    }
  }
}

void overlayGrayPlanesOnGray8(const uint8_t* lsbPlane, const uint8_t* msbPlane, uint8_t* dst, const uint16_t width,
                              const uint16_t height, const uint16_t srcRowBytes, const uint32_t dstRowBytes) {
  if (lsbPlane == nullptr || msbPlane == nullptr || dst == nullptr) {
    return;
  }
  for (uint16_t y = 0; y < height; ++y) {
    const uint8_t* lsbRow = lsbPlane + static_cast<size_t>(y) * srcRowBytes;
    const uint8_t* msbRow = msbPlane + static_cast<size_t>(y) * srcRowBytes;
    uint8_t* out = dst + static_cast<size_t>(y) * dstRowBytes;
    for (uint16_t x = 0; x < width; ++x) {
      const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7u));
      if ((msbRow[x >> 3] & bit) == 0) {
        continue;
      }
      out[x] = (lsbRow[x >> 3] & bit) ? GRAY_DARK : GRAY_LIGHT;
    }
  }
}

}  // namespace crosspoint::hosted
