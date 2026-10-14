#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Reader menu: go to a reference, and the reading preferences (font size,
// line spacing, layout, headings, orientation). Preference rows change in place
// on Confirm; the reader re-lays out the chapter when the menu closes.
class BibleMenuActivity final : public Activity {
 public:
  static constexpr int ACTION_GOTO = 1;

  BibleMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BibleMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  enum Row { GOTO, FONT_SIZE, LINE_SPACING, LAYOUT, HEADINGS, ORIENTATION, SETTINGS_ROW, ROW_COUNT };

  void activate(int row);
  std::string valueOf(int row) const;

  int selected = 0;
  bool firstRender = true;
  bool prefsChanged = false;
  ButtonNavigator buttonNavigator;
};
