#pragma once
#include <hal/android/DisplayInfo.h>

#include "fontIds.h"

// FreeInkUI font slots. Row heights, header height, and touch sizes are not
// chosen here: FreeInkApp derives all metric tokens from the body font's line
// height (themeTokensForLineHeight). One fixed tier for every board — the
// user-facing UI-scale setting was removed.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
  // The body font's point size, which is the one number the whole interface
  // scales by. FreeInkApp derives its rows, header and touch targets from this
  // font's line height, and UITheme scales the theme metrics by the same ratio,
  // so both halves of the interface grow together instead of by two rules.
  int bodyPointSize;
};

// What the theme metrics were written against: an X4 at roughly 160 dpi with a
// 12 point body font. A 45-pixel header and a 30-pixel row are that font's
// proportions, not arbitrary numbers.
inline constexpr int REFERENCE_BODY_POINT_SIZE = 12;

inline UIScaleSpec uiScaleSpec() {
  UIScaleSpec spec{};
  spec.smallFontId = UI_10_FONT_ID;
  spec.bodyFontId = UI_12_FONT_ID;
  spec.bodyPointSize = 12;

  // A denser panel wants a physically comparable interface, so the tier follows
  // the density the system reported. The ideal at 300 dpi is 12 * 300 / 160,
  // which is 22, and the ladder takes the biggest tier that does not
  // exceed it, so 300 dpi lands on 20 and a denser panel would reach 24.
  const int dpi = crosspoint::android::displayInfo().densityDpi;
  const int ideal = dpi > 0 ? (REFERENCE_BODY_POINT_SIZE * dpi + 80) / 160 : REFERENCE_BODY_POINT_SIZE;
  if (ideal >= 24) {
    spec.smallFontId = UI_16_FONT_ID;
    spec.bodyFontId = UI_24_FONT_ID;
    spec.bodyPointSize = 24;
  } else if (ideal >= 20) {
    spec.smallFontId = UI_14_FONT_ID;
    spec.bodyFontId = UI_20_FONT_ID;
    spec.bodyPointSize = 20;
  } else if (ideal >= 16) {
    spec.smallFontId = UI_12_FONT_ID;
    spec.bodyFontId = UI_16_FONT_ID;
    spec.bodyPointSize = 16;
  } else if (ideal >= 14) {
    spec.smallFontId = UI_10_FONT_ID;
    spec.bodyFontId = UI_14_FONT_ID;
    spec.bodyPointSize = 14;
  }
  // Titles use the UI font, not a reader font: fui headers draw book and
  // directory titles, and the built-in Ubuntu UI fonts cover Hebrew (plus the
  // size-matched SD CJK fallback) where the NotoSans reader subsets do not.
  // Same font develop's drawHeader used, so script coverage matches develop.
  spec.titleFontId = spec.bodyFontId;
  return spec;
}
