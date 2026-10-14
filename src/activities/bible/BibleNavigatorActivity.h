#pragma once

#include <BiblePack.h>

#include <string>

#include "activities/Activity.h"

// Book -> chapter -> verse picker. Books are a grid of abbreviations (Old and
// New Testament), chapters and verses a grid of numbers. Finishes with a
// BibleRefResult, or cancelled when Back leaves the book step.
class BibleNavigatorActivity final : public Activity {
 public:
  BibleNavigatorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bible::Ref current)
      : Activity("BibleNavigator", renderer, mappedInput), current(current) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  enum class Step { Book, Chapter, Verse };

  int itemCount() const;
  int columns() const;
  int selection() const;
  void setSelection(int index);
  void move(int delta);
  void finishWith(int verse);

  void drawBookGrid(int top, int left, int width, int height);
  void drawNumberGrid(int top, int left, int width, int height);
  void drawCell(int x, int y, int w, int h, int fontId, const char* label, bool selected, bool marked);
  int fitFont(int cellWidth, int cellHeight, const char* widest) const;

  bible::Ref current;
  Step step = Step::Book;
  int book = 0;
  int chapter = 1;
  int verse = 1;
  int verseCount = 0;
  bool firstRender = true;
};
