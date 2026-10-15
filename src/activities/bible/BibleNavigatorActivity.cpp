#include "BibleNavigatorActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "bible/BibleFonts.h"
#include "bible/BibleSession.h"
#include "components/UITheme.h"

namespace {
constexpr int BOOK_COLUMNS = 6;
constexpr int NUMBER_COLUMNS = 5;
constexpr int NUMBER_ROWS = 8;
constexpr int OLD_TESTAMENT_BOOKS = 39;
constexpr int MARGIN = 14;
constexpr int CELL_GAP = 4;
constexpr unsigned long PAGE_HOLD_MS = 600;
constexpr unsigned long DIRECT_HOLD_MS = 600;
}  // namespace

void BibleNavigatorActivity::onEnter() {
  Activity::onEnter();
  auto& pack = BIBLE.getPack();
  const bible::Ref ref = pack.clamp(current);
  current = ref;
  book = ref.book;
  chapter = ref.chapter;
  verse = 1;
  step = Step::Book;
  requestUpdate();
}

int BibleNavigatorActivity::itemCount() const {
  auto& pack = BIBLE.getPack();
  switch (step) {
    case Step::Book:
      return pack.bookCount();
    case Step::Chapter:
      return pack.book(book).chapterCount;
    case Step::Verse:
      return verseCount;
  }
  return 0;
}

int BibleNavigatorActivity::columns() const { return step == Step::Book ? BOOK_COLUMNS : NUMBER_COLUMNS; }

int BibleNavigatorActivity::selection() const {
  switch (step) {
    case Step::Book:
      return book;
    case Step::Chapter:
      return chapter - 1;
    case Step::Verse:
      return verse - 1;
  }
  return 0;
}

void BibleNavigatorActivity::setSelection(int index) {
  const int count = itemCount();
  if (count <= 0) return;
  index = std::clamp(index, 0, count - 1);
  switch (step) {
    case Step::Book:
      book = index;
      break;
    case Step::Chapter:
      chapter = index + 1;
      break;
    case Step::Verse:
      verse = index + 1;
      break;
  }
}

void BibleNavigatorActivity::move(int delta) {
  const int count = itemCount();
  if (count <= 0) return;
  int index = selection() + delta;
  // Single steps wrap around; row and page jumps stop at the ends.
  if (delta == 1 || delta == -1) {
    index = (index + count) % count;
  } else if (step == Step::Book) {
    // Rows of the two testament grids: keep within the column when crossing the gap.
    const int old = selection();
    const bool oldNt = old >= OLD_TESTAMENT_BOOKS;
    if (delta > 0 && !oldNt && index >= OLD_TESTAMENT_BOOKS) {
      const int col = old % BOOK_COLUMNS;
      const int lastRowStart = ((OLD_TESTAMENT_BOOKS - 1) / BOOK_COLUMNS) * BOOK_COLUMNS;
      index = old < lastRowStart ? std::min(old + BOOK_COLUMNS, OLD_TESTAMENT_BOOKS - 1) : OLD_TESTAMENT_BOOKS + col;
    } else if (delta < 0 && oldNt && index < OLD_TESTAMENT_BOOKS) {
      const int col = (old - OLD_TESTAMENT_BOOKS) % BOOK_COLUMNS;
      const int lastRowStart = ((OLD_TESTAMENT_BOOKS - 1) / BOOK_COLUMNS) * BOOK_COLUMNS;
      index = old - OLD_TESTAMENT_BOOKS < BOOK_COLUMNS ? std::min(lastRowStart + col, OLD_TESTAMENT_BOOKS - 1) : index;
    }
  }
  LOG_DBG("BNAV", "move %d: %d -> %d", delta, selection(), index);
  setSelection(index);
  requestUpdate();
}

void BibleNavigatorActivity::finishWith(int v) {
  BibleRefResult r;
  r.book = static_cast<uint8_t>(book);
  r.chapter = static_cast<uint16_t>(chapter);
  r.verse = static_cast<uint16_t>(v);
  setResult(r);
  finish();
}

void BibleNavigatorActivity::loop() {
  using Button = MappedInputManager::Button;
  auto& pack = BIBLE.getPack();

  if (mappedInput.wasReleased(Button::Back)) {
    if (step == Step::Book) {
      ActivityResult r;
      r.isCancelled = true;
      setResult(std::move(r));
      finish();
      return;
    }
    step = step == Step::Verse ? Step::Chapter : Step::Book;
    requestUpdate();
    return;
  }

  // Holding Confirm on a chapter opens it at verse 1 without the verse step.
  if (step == Step::Chapter && mappedInput.wasLongPressed(Button::Confirm, DIRECT_HOLD_MS)) {
    finishWith(1);
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    switch (step) {
      case Step::Book:
        chapter = (book == current.book) ? current.chapter : 1;
        step = Step::Chapter;
        break;
      case Step::Chapter:
        verseCount = pack.verseCountOf(book, chapter);
        verse = 1;
        step = Step::Verse;
        break;
      case Step::Verse:
        finishWith(verse);
        return;
    }
    requestUpdate();
    return;
  }

  // Release-driven steps, no auto-repeat: the C3 has one core, so a tap that lands during a
  // render is seen late and would pass for a hold. Holding a side button jumps a page.
  const int cols = columns();
  const int page = step == Step::Book ? OLD_TESTAMENT_BOOKS : cols * NUMBER_ROWS;
  const bool swap = mappedInput.isNavDirectionSwapped();
  const Button prev = swap ? Button::Right : Button::Left;
  const Button next = swap ? Button::Left : Button::Right;
  if (mappedInput.wasLongPressed(Button::Up, PAGE_HOLD_MS)) return move(-page);
  if (mappedInput.wasLongPressed(Button::Down, PAGE_HOLD_MS)) return move(page);
  if (mappedInput.wasLongPressed(prev, PAGE_HOLD_MS)) return move(-cols);
  if (mappedInput.wasLongPressed(next, PAGE_HOLD_MS)) return move(cols);
  if (mappedInput.wasReleased(prev)) return move(-1);
  if (mappedInput.wasReleased(next)) return move(1);
  if (mappedInput.wasReleased(Button::Up)) return move(-cols);
  if (mappedInput.wasReleased(Button::Down)) return move(cols);
}

int BibleNavigatorActivity::fitFont(int cellWidth, int cellHeight, const char* widest) const {
  for (int i = BibleFonts::COUNT - 1; i > 0; i--) {
    const int id = BibleFonts::IDS[i];
    if (renderer.getLineHeight(id) <= cellHeight - 6 &&
        renderer.getTextWidth(id, widest, EpdFontFamily::BOLD) <= cellWidth - 10) {
      return id;
    }
  }
  return BibleFonts::IDS[0];
}

void BibleNavigatorActivity::drawCell(int x, int y, int w, int h, int fontId, const char* label, bool selected,
                                      bool marked) {
  if (selected) {
    renderer.fillRoundedRect(x, y, w, h, 8, Color::Black);
  } else if (marked) {
    renderer.drawRoundedRect(x, y, w, h, 2, 8, true);
  }
  const int tw = renderer.getTextWidth(fontId, label, EpdFontFamily::BOLD);
  const int th = renderer.getLineHeight(fontId);
  renderer.drawText(fontId, x + (w - tw) / 2, y + (h - th) / 2, label, !selected, EpdFontFamily::BOLD);
}

void BibleNavigatorActivity::drawBookGrid(int top, int left, int width, int height) {
  auto& pack = BIBLE.getPack();
  const int labelFont = BibleFonts::LABEL_FONT_ID;
  const int labelHeight = renderer.getLineHeight(labelFont) + 4;
  const int otRows = (OLD_TESTAMENT_BOOKS + BOOK_COLUMNS - 1) / BOOK_COLUMNS;
  const int ntBooks = pack.bookCount() - OLD_TESTAMENT_BOOKS;
  const int ntRows = (ntBooks + BOOK_COLUMNS - 1) / BOOK_COLUMNS;
  const int cellW = width / BOOK_COLUMNS;
  const int cellH = std::min(80, (height - 2 * labelHeight - 8) / (otRows + ntRows));

  // One font for every cell, sized to the widest abbreviation.
  const char* widest = "";
  int widestW = 0;
  for (int i = 0; i < pack.bookCount(); i++) {
    const int w = renderer.getTextWidth(BibleFonts::IDS[0], pack.book(i).abbr, EpdFontFamily::BOLD);
    if (w > widestW) {
      widestW = w;
      widest = pack.book(i).abbr;
    }
  }
  const int fontId = fitFont(cellW - CELL_GAP, cellH - CELL_GAP, widest);

  int y = top;
  for (int t = 0; t < 2; t++) {
    renderer.drawText(labelFont, left, y, t == 0 ? tr(STR_BIBLE_OLD_TESTAMENT) : tr(STR_BIBLE_NEW_TESTAMENT), true,
                      EpdFontFamily::BOLD);
    y += labelHeight;
    const int first = t == 0 ? 0 : OLD_TESTAMENT_BOOKS;
    const int count = t == 0 ? OLD_TESTAMENT_BOOKS : ntBooks;
    for (int i = 0; i < count; i++) {
      const int idx = first + i;
      const int cx = left + (i % BOOK_COLUMNS) * cellW;
      const int cy = y + (i / BOOK_COLUMNS) * cellH;
      drawCell(cx + CELL_GAP / 2, cy + CELL_GAP / 2, cellW - CELL_GAP, cellH - CELL_GAP, fontId, pack.book(idx).abbr,
               idx == book, idx == current.book);
    }
    y += ((count + BOOK_COLUMNS - 1) / BOOK_COLUMNS) * cellH + 8;
  }
}

void BibleNavigatorActivity::drawNumberGrid(int top, int left, int width, int height) {
  const int count = itemCount();
  const int perPage = NUMBER_COLUMNS * NUMBER_ROWS;
  const int pageCount = std::max(1, (count + perPage - 1) / perPage);
  const int page = selection() / perPage;
  const int labelFont = BibleFonts::LABEL_FONT_ID;
  const int footer = pageCount > 1 ? renderer.getLineHeight(labelFont) + 6 : 0;
  const int cellW = width / NUMBER_COLUMNS;
  const int cellH = std::min(90, (height - footer) / NUMBER_ROWS);
  const int fontId = fitFont(cellW - CELL_GAP, cellH - CELL_GAP, count >= 100 ? "888" : "88");
  const int marked = step == Step::Chapter && book == current.book ? current.chapter : 0;

  for (int i = page * perPage; i < std::min(count, (page + 1) * perPage); i++) {
    const int slot = i - page * perPage;
    const int cx = left + (slot % NUMBER_COLUMNS) * cellW;
    const int cy = top + (slot / NUMBER_COLUMNS) * cellH;
    const std::string label = std::to_string(i + 1);
    drawCell(cx + CELL_GAP / 2, cy + CELL_GAP / 2, cellW - CELL_GAP, cellH - CELL_GAP, fontId, label.c_str(),
             i == selection(), i + 1 == marked);
  }
  if (pageCount > 1) {
    const std::string text = std::to_string(page + 1) + " / " + std::to_string(pageCount);
    const int w = renderer.getTextWidth(labelFont, text.c_str());
    renderer.drawText(labelFont, left + (width - w) / 2, top + height - footer + 4, text.c_str());
  }
}

void BibleNavigatorActivity::render(RenderLock&&) {
  auto& pack = BIBLE.getPack();
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int x = left + MARGIN;
  const int width = screenW - left - right - 2 * MARGIN;
  const int hintsHeight = UITheme::getInstance().getMetrics().buttonHintsHeight;

  // Header: the selection in words, large.
  const int titleFont = BibleFonts::IDS[3];
  const int labelFont = BibleFonts::LABEL_FONT_ID;
  std::string title = pack.book(book).name;
  std::string subtitle;
  switch (step) {
    case Step::Book:
      subtitle = BIBLE.chapterCountLabel(book);
      break;
    case Step::Chapter:
      title = BIBLE.chapterLabel(book, chapter);
      subtitle = tr(STR_BIBLE_PICK_CHAPTER);
      break;
    case Step::Verse:
      title = BIBLE.chapterLabel(book, chapter) + " " + BibleSession::format(tr(STR_BIBLE_VERSE_FMT), verse);
      subtitle = tr(STR_BIBLE_PICK_VERSE);
      break;
  }

  const auto drawAll = [&]() {
    int y = top + 10;
    renderer.drawText(titleFont, x, y, title.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(titleFont);
    renderer.drawText(labelFont, x, y, subtitle.c_str());
    y += renderer.getLineHeight(labelFont) + 6;
    renderer.drawLine(x, y, x + width, y, 2, true);
    y += 10;
    const int gridHeight = screenH - bottom - hintsHeight - 6 - y;
    if (step == Step::Book) {
      drawBookGrid(y, x, width, gridHeight);
    } else {
      drawNumberGrid(y, x, width, gridHeight);
    }
  };

  renderer.clearScreen();
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  drawAll();
  scope.endScanAndPrewarm();
  drawAll();

  const auto labels = mappedInput.mapLabels(step == Step::Book ? tr(STR_CANCEL) : tr(STR_BACK), tr(STR_SELECT),
                                            tr(STR_BIBLE_HINT_PREV), tr(STR_BIBLE_HINT_NEXT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // Differential refresh only: a HALF refresh is three drive phases (three flashes) on the X3.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
