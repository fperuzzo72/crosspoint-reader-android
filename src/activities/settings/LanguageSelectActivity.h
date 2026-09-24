#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "activities/UiListActivity.h"

class MappedInputManager;

/**
 * Activity for selecting UI language
 */
class LanguageSelectActivity final : public UiListActivity {
 public:
  explicit LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override { return visibleCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  constexpr static uint8_t totalItems = getLanguageCount();

  // Row storage: totalItems is a compile-time constant, so a fixed-capacity
  // array avoids any heap allocation for the row list. Built once in
  // onEnter() — activateIndex() finishes the activity immediately on
  // selection, so buildScreen() never needs to see a different "Selected"
  // row within one visit.
  freeink::ui::ListItem rowItems[totalItems]{};

  // Only the languages whose own name the UI font can draw are offered, so the
  // array is sized for the worst case and filled to visibleCount. Row i shows
  // the language visibleIndices[i].
  uint8_t visibleIndices[totalItems]{};
  int visibleCount = 0;
};
