#include "LanguageSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

// A language whose own name the UI font cannot draw is a trap rather than an
// option. Choosing it replaces every string on screen with glyphs the font
// misses, including the ones naming the way back, so the only recovery is
// remembering where the Language row sat in a menu that is now blank.
//
// Asked of the font instead of hardcoded, so this follows whatever the fonts
// are built to cover. hasCodepoint() consults the coverage table and never
// touches storage.
bool nameIsRenderable(const GfxRenderer& renderer, const char* name) {
  if (name == nullptr || *name == '\0') return false;
  const auto& fonts = renderer.getFontMap();
  const auto it = fonts.find(UI_12_FONT_ID);
  if (it == fonts.end()) return true;  // no font to ask; hide nothing

  const auto* cursor = reinterpret_cast<const unsigned char*>(name);
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&cursor)) != 0) {
    if (cp == ' ' || cp == '-' || cp == '(' || cp == ')') continue;
    if (!it->second.hasCodepoint(cp, EpdFontFamily::REGULAR)) return false;
  }
  return true;
}

}  // namespace

LanguageSelectActivity::LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("LanguageSelect", renderer, mappedInput) {}

void LanguageSelectActivity::onEnter() {
  UiListActivity::onEnter();

  // Opens on the current language, which may sit past the first page; the first
  // screen build pulls the viewport to it (ListNav follow-on-build). nav.selected
  // is set inside the loop below, because it indexes the filtered list.
  nav.selected = 0;
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());

  // Built once here rather than every buildScreen() call: labels are static,
  // and the "Selected" marker can't go stale mid-visit since activateIndex()
  // finishes the activity immediately on selection.
  visibleCount = 0;
  for (int i = 0; i < totalItems; ++i) {
    const uint8_t langIndex = SORTED_LANGUAGE_INDICES[i];
    const char* name = I18N.getLanguageName(static_cast<Language>(langIndex));
    // The active language always stays listed, even if something has gone wrong
    // with the fonts: it is the row carrying "Selected" and the proof of where
    // you are.
    if (langIndex != currentLang && !nameIsRenderable(renderer, name)) continue;

    fui::ListItem item;
    item.label = name;
    if (langIndex == currentLang) {
      item.value = tr(STR_SELECTED);
    }
    item.actionValue = static_cast<int16_t>(visibleCount);
    visibleIndices[visibleCount] = langIndex;
    if (langIndex == currentLang) nav.selected = visibleCount;
    rowItems[visibleCount] = item;
    ++visibleCount;
  }
}

const char* LanguageSelectActivity::headerTitle() const { return tr(STR_LANGUAGE); }

void LanguageSelectActivity::activateIndex(const int index) {
  // The activated row leaves this screen; a lingering flash would gray an
  // unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  const uint8_t langIndex = visibleIndices[index];

  {
    RenderLock lock(*this);
    I18N.setLanguage(static_cast<Language>(langIndex));
  }

  SETTINGS.language = langIndex;
  SETTINGS.saveToFile();

  // Return to previous page
  finish();
}

void LanguageSelectActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems was built once in onEnter() and is reused here on every repaint.
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(totalItems);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
