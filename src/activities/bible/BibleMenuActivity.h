#pragma once

#include "activities/Activity.h"
#include <string>
#include <vector>

#include "util/ButtonNavigator.h"

// Reader menu: go to a reference, and the reading preferences (font size,
// line spacing, layout, headings, orientation). Preference rows change in place
// on Confirm; the reader re-lays out the chapter when the menu closes.
class BibleMenuActivity final : public Activity {
 public:
  static constexpr int ACTION_GOTO = 1;
  static constexpr int ACTION_ADD_BOOKMARK = 2;
  static constexpr int ACTION_BOOKMARKS = 3;

  BibleMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BibleMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  enum Row {
    GOTO,
    ADD_BOOKMARK,
    BOOKMARKS,
    LANGUAGE,
    BIBLE_CHOICE,  // only when the language has more than one Bible
    FONT_FAMILY,   // only when the language has more than one reader font
    FONT_SIZE,
    LINE_SPACING,
    LAYOUT,
    HEADINGS,
    SMOOTHING,
    ORIENTATION,
    INFO,
    SETTINGS_ROW,
    ROW_COUNT
  };

  void activate(int row);
  std::string valueOf(int row) const;
  void buildRows();

  std::vector<int> rows;  // visible Row values, in order
  int selected = 0;       // index into rows
  bool prefsChanged = false;
  ButtonNavigator buttonNavigator;
};
