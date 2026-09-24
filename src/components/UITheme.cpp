#include "UITheme.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <hal/android/DisplayInfo.h>

#include <algorithm>
#include <memory>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/roundedraff/RoundedRaffTheme.h"

UITheme UITheme::instance;

UITheme::UITheme() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::reload() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::setTheme(CrossPointSettings::UI_THEME type) {
  switch (type) {
    case CrossPointSettings::UI_THEME::CLASSIC:
      LOG_DBG("UI", "Using Classic theme");
      currentTheme = std::make_unique<BaseTheme>();
      currentMetrics = &BaseMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA:
      LOG_DBG("UI", "Using Lyra theme");
      currentTheme = std::make_unique<LyraTheme>();
      currentMetrics = &LyraMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::ROUNDEDRAFF:
      LOG_DBG("UI", "Using RoundedRaff theme");
      currentTheme = std::make_unique<RoundedRaffTheme>();
      currentMetrics = &RoundedRaffMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_3_COVERS:
      LOG_DBG("UI", "Using Lyra 3 Covers theme");
      currentTheme = std::make_unique<Lyra3CoversTheme>();
      currentMetrics = &Lyra3CoversMetrics::values;
      break;
  }
  metricsValid = false;
}

namespace {

// The metrics are written in pixels for a panel of about 160 dpi, which is what
// the X4 is and what Android calls a dp. Every number in them, a 45-pixel
// header, a 30-pixel row, a 20-pixel margin, describes a PHYSICAL size that
// happens to have been measured there. Carry those pixels to a 300 dpi panel
// unchanged and the whole interface shrinks to a little under half, which is
// exactly what it did: a header that took 9 percent of an X4 screen took 2.7 on
// a HiBreak and 2.4 on a Boox.
//
// So they are treated as dp and multiplied by the density the system reports.
// Sizes that are not lengths are left alone: percentages, counts, style enums,
// ratios and booleans mean the same thing at any density.
constexpr int BASE_DPI = 160;

int scaled(const int dp, const int dpi) {
  if (dpi <= 0 || dpi == BASE_DPI) return dp;
  // Rounded rather than truncated: a 1-pixel rule must not vanish, and a
  // truncating scale eats exactly the thin things that are already hardest to
  // see on e-ink.
  const long v = (static_cast<long>(dp) * dpi + BASE_DPI / 2) / BASE_DPI;
  if (dp > 0 && v < 1) return 1;
  return static_cast<int>(v);
}

void scaleMetrics(ThemeMetrics& m, const int dpi) {
  int* const lengths[] = {&m.batteryWidth,
                          &m.batteryHeight,
                          &m.topPadding,
                          &m.batteryBarHeight,
                          &m.headerHeight,
                          &m.verticalSpacing,
                          &m.previewPadding,
                          &m.contentSidePadding,
                          &m.listRowHeight,
                          &m.listWithSubtitleRowHeight,
                          &m.listRowGap,
                          &m.listRowRadius,
                          &m.listInset,
                          &m.listSidePadding,
                          &m.listScrollWidth,
                          &m.headerSidePadding,
                          &m.headerUnderlineSize,
                          &m.menuRowHeight,
                          &m.menuSpacing,
                          &m.tabSpacing,
                          &m.tabBarHeight,
                          &m.scrollBarWidth,
                          &m.scrollBarRightOffset,
                          &m.homeTopPadding,
                          &m.homeCoverHeight,
                          &m.homeCoverTileHeight,
                          &m.homeMenuTopOffset,
                          &m.buttonHintsHeight,
                          &m.sideButtonHintsWidth,
                          &m.progressBarHeight,
                          &m.progressBarMarginTop,
                          &m.statusBarHorizontalMargin,
                          &m.statusBarVerticalMargin,
                          &m.keyboardKeyHeight,
                          &m.keyboardKeySpacing,
                          &m.keyboardVerticalOffset,
                          &m.popupMarginX,
                          &m.popupMarginY,
                          &m.popupFrameThickness,
                          &m.popupCornerRadius,
                          &m.popupTextBaselineOffsetY,
                          &m.popupProgressBarHeight};
  for (int* const field : lengths) {
    *field = scaled(*field, dpi);
  }
}

}  // namespace

const ThemeMetrics& UITheme::getMetrics() const {
  // hasTouch() can flip once touch init completes after static construction, so the
  // cached copy is refreshed when the flag differs instead of copying the struct per call.
  const bool touch = gpio.hasTouch();
  if (!metricsValid || touch != metricsForTouch) {
    adjustedMetrics = *currentMetrics;
    scaleMetrics(adjustedMetrics, crosspoint::android::displayInfo().densityDpi);
    if (touch) {
      adjustedMetrics.buttonHintsHeight = 0;
    }
    metricsForTouch = touch;
    metricsValid = true;
  }
  return adjustedMetrics;
}

// Screen area excluding the button hints
Rect UITheme::getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints, bool hasSideButtonHints) {
  auto orientation = renderer.getOrientation();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  Rect safeArea = Rect{0, 0, screenWidth, screenHeight};
  const ThemeMetrics metrics = getMetrics();
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      if (hasFrontButtonHints) {
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      if (hasFrontButtonHints) {
        safeArea.x += metrics.buttonHintsHeight;
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      if (hasFrontButtonHints) {
        safeArea.y += metrics.buttonHintsHeight;
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      if (hasFrontButtonHints) {
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
  }
  return safeArea;
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(coverHeight));
  }
  return coverBmpPath;
}

UIIcon UITheme::getFileIcon(const std::string& filename) {
  if (filename.back() == '/') {
    return Folder;
  }
  if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
    return Book;
  }
  if (FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
    return Text;
  }
  if (FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename)) {
    return Image;
  }
  return File;
}

int UITheme::getStatusBarHeight() {
  const ThemeMetrics metrics = UITheme::getInstance().getMetrics();
  const auto sb = SETTINGS.statusBarSpec();

  // Layout reservation is hardware-agnostic: pass clockAvailable=true so the
  // reserved height does not depend on whether an RTC is present.
  return (sb.textLaneVisible(true) ? (metrics.statusBarVerticalMargin) : 0) +
         (sb.showsProgressBar() ? (sb.progressBarHeightPx + metrics.progressBarMarginTop) : 0);
}

int UITheme::getProgressBarHeight() {
  const ThemeMetrics metrics = UITheme::getInstance().getMetrics();
  const auto sb = SETTINGS.statusBarSpec();
  return sb.showsProgressBar() ? (sb.progressBarHeightPx + metrics.progressBarMarginTop) : 0;
}

// Centered text implementation that takes the safe area into account
void UITheme::drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black, EpdFontFamily::Style style) {
  const int x = screen.x + (screen.width - renderer.getTextWidth(fontId, text, style)) / 2;
  renderer.drawText(fontId, x, y, text, black, style);
}

void UITheme::drawCenteredWrappedText(const GfxRenderer& renderer, Rect bounds, int fontId, const char* text,
                                      int maxLines, bool black, EpdFontFamily::Style style,
                                      TextVerticalAlignment verticalAlignment) {
  if (!text || *text == '\0' || bounds.width <= 0 || bounds.height <= 0 || maxLines <= 0) return;

  const int lineHeight = renderer.getLineHeight(fontId);
  if (lineHeight <= 0) return;

  const int lineLimit = std::min(maxLines, bounds.height / lineHeight);
  if (lineLimit <= 0) return;

  const auto alignedTop = [&](const int textHeight) {
    switch (verticalAlignment) {
      case TextVerticalAlignment::CENTER:
        return bounds.y + (bounds.height - textHeight) / 2;
      case TextVerticalAlignment::BOTTOM:
        return bounds.y + bounds.height - textHeight;
      case TextVerticalAlignment::TOP:
      default:
        return bounds.y;
    }
  };

  if (renderer.getTextWidth(fontId, text, style) <= bounds.width) {
    drawCenteredText(renderer, bounds, fontId, alignedTop(lineHeight), text, black, style);
    return;
  }

  const auto lines = renderer.wrappedText(fontId, text, bounds.width, lineLimit, style);
  int y = alignedTop(static_cast<int>(lines.size()) * lineHeight);
  for (const auto& line : lines) {
    drawCenteredText(renderer, bounds, fontId, y, line.c_str(), black, style);
    y += lineHeight;
  }
}
