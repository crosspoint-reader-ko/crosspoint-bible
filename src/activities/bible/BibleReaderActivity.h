#pragma once

#include <BibleLayout.h>

#include <string>

#include "activities/Activity.h"

// Reads the Bible chapter by chapter. Page turns run across chapter and book
// boundaries; Confirm opens book/chapter/verse navigation, Back the menu.
class BibleReaderActivity final : public Activity {
 public:
  BibleReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BibleReader", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool isReaderActivity() const override { return true; }
  bool isHomeActivity() const override { return true; }

 private:
  // Load chapter (book, chapter) and show the page holding `verse`
  // (verse <= 0: the first page, verse == LAST_PAGE: the last page).
  static constexpr int LAST_PAGE = -1;
  bool openChapter(int book, int chapter, int verse);
  void relayout();
  bool turnPage(bool forward);
  bool skipChapter(bool forward);
  void openNavigator();
  void openMenu();
  void rememberPosition();

  void drawPage(int top, int left, bool grayPass = false);
  void drawChrome(int top, int left, int width, int bottom);

  bible::ChapterLayout layout;
  int book = 0;
  int chapter = 1;
  int page = 0;
  int highlightVerse = 0;  // verse number boxed after a jump, cleared on the next turn
  bool packMissing = false;

  int fontId = 0;
  int numberFontId = 0;
  bool cleanScreenNext = false;
  int lineHeight = 0;
  int pagesUntilFullRefresh = 0;
  bool skipNextInput = false;
};
