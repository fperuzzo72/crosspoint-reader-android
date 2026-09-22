#pragma once
#include <cstdint>

// The 1bpp to 8bpp gray expansion, and the composition of the two overlay
// planes.
//
// Neither depends on any hardware, which is why they are the one part of a
// hosted backend a host test can hold to account. They live here rather than
// inside one device's backend because the Kindle and Android need exactly the
// same two functions.
//
// Framebuffer contract, matching HalDisplay: CrossPoint composes into a packed
// 1bpp buffer, rows byte-aligned at widthBytes each, MSB = leftmost pixel, a
// SET bit meaning WHITE (clearScreen fills with 0xFF).

namespace crosspoint::hosted {

inline constexpr uint8_t GRAY_BLACK = 0x00;
inline constexpr uint8_t GRAY_WHITE = 0xFF;
// The two intermediate levels the renderer's gray planes can express.
inline constexpr uint8_t GRAY_DARK = 0x55;
inline constexpr uint8_t GRAY_LIGHT = 0xAA;

// dstRowBytes is the destination stride, which can be wider than the width: on
// the KT3 the framebuffer reports 608 bytes per row for a 600px panel, and
// stepping by width instead of stride shears the image progressively.
//
// Bits beyond `width` in each row's final source byte are padding and are not
// emitted, so a width that is not a multiple of 8 stays correct.
void expand1bppToGray8(const uint8_t* src, uint8_t* dst, uint16_t width, uint16_t height, uint16_t srcRowBytes,
                       uint32_t dstRowBytes);

// Paints the two 1bpp gray planes over an 8bpp frame that already holds the
// black-and-white base. The renderer encodes a gray pixel as a pair of bits,
// one from each plane, as (LSB, MSB):
//
//   (0,0)  not gray; whatever the base painted stands
//   (1,1)  dark
//   (0,1)  light
//   (1,0)  the encoding never produces this; left to the base rather than
//          guessed
//
// Pixels the planes do not claim are not written, which is why `dst` is
// in-out.
void overlayGrayPlanesOnGray8(const uint8_t* lsbPlane, const uint8_t* msbPlane, uint8_t* dst, uint16_t width,
                              uint16_t height, uint16_t srcRowBytes, uint32_t dstRowBytes);

}  // namespace crosspoint::hosted
