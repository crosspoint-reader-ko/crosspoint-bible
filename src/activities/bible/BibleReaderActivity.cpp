#include "BibleReaderActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "BibleMenuActivity.h"
#include "BibleNavigatorActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "bible/BibleFonts.h"
#include "bible/BibleSession.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long CHAPTER_HOLD_MS = 700;
constexpr int SIDE_MARGIN = 18;
constexpr float LINE_SPACING[BibleSession::LINE_SPACING_COUNT] = {1.0f, 1.2f, 1.45f};

bool isVerseNumber(const std::string& token) { return !token.empty() && token[0] == bible::VERSE_NUMBER_MARK; }
}  // namespace

void BibleReaderActivity::onEnter() {
  Activity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  cleanScreenNext = true;
  if (!BIBLE.ensureOpen()) {
    packMissing = true;
    requestUpdate();
    return;
  }
  packMissing = false;
  const bible::Ref pos = BIBLE.getPack().clamp(BIBLE.position);
  openChapter(pos.book, pos.chapter, pos.verse);
  requestUpdate();
}

void BibleReaderActivity::onExit() {
  if (!packMissing) rememberPosition();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  Activity::onExit();
}

bool BibleReaderActivity::openChapter(int newBook, int newChapter, int verse) {
  bible::ChapterContent content;
  if (!BIBLE.loadChapter(newBook, newChapter, content)) {
    LOG_ERR("BIBLE", "Cannot load %d:%d", newBook, newChapter);
    return false;
  }
  book = newBook;
  chapter = newChapter;

  fontId = BibleFonts::fontId(BIBLE.prefs.fontSize);
  numberFontId = BibleFonts::fontId(BIBLE.prefs.fontSize - 2);
  lineHeight = static_cast<int>(renderer.getLineHeight(fontId) * LINE_SPACING[BIBLE.prefs.lineSpacing]);

  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int labelHeight = renderer.getLineHeight(BibleFonts::LABEL_FONT_ID);

  bible::LayoutStyle style;
  style.width = renderer.getScreenWidth() - left - right - 2 * SIDE_MARGIN;
  style.height = renderer.getScreenHeight() - top - bottom - 2 * labelHeight - 12;
  style.lineHeight = lineHeight;
  style.titleLineHeight = lineHeight * 3 / 2;
  style.blockGap = lineHeight / 2;
  style.versePerLine = BIBLE.prefs.versePerLine != 0;
  style.spaceWidth = renderer.getSpaceWidth(fontId);
  const int fid = fontId;
  style.measure = [this, fid](const std::string& token, bool bold) {
    return renderer.getTextWidth(fid, token.c_str(), bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  };
  const int nid = numberFontId;
  style.measureNumber = [this, nid](const std::string& digits) {
    return renderer.getTextWidth(nid, digits.c_str(), EpdFontFamily::BOLD);
  };
  layout.build(content, style);

  if (verse == LAST_PAGE) {
    page = layout.pageCount() - 1;
  } else if (verse <= 1) {
    page = 0;
  } else {
    page = layout.pageOfVerse(verse);
  }
  rememberPosition();
  return true;
}

void BibleReaderActivity::relayout() {
  const int verse = layout.pageFirstVerse(page);
  openChapter(book, chapter, verse > 0 ? verse : 1);
}

void BibleReaderActivity::rememberPosition() {
  BIBLE.position.book = static_cast<uint8_t>(book);
  BIBLE.position.chapter = static_cast<uint16_t>(chapter);
  const uint16_t v = layout.pageFirstVerse(page);
  BIBLE.position.verse = v > 0 ? v : 1;
  BIBLE.saveState();
}

bool BibleReaderActivity::turnPage(bool forward) {
  highlightVerse = 0;
  if (forward) {
    if (page + 1 < layout.pageCount()) {
      page++;
      rememberPosition();
      return true;
    }
    return skipChapter(true);
  }
  if (page > 0) {
    page--;
    rememberPosition();
    return true;
  }
  // Back from the first page: last page of the previous chapter.
  auto& pack = BIBLE.getPack();
  if (chapter > 1) return openChapter(book, chapter - 1, LAST_PAGE);
  if (book > 0) return openChapter(book - 1, pack.book(book - 1).chapterCount, LAST_PAGE);
  return false;
}

bool BibleReaderActivity::skipChapter(bool forward) {
  highlightVerse = 0;
  auto& pack = BIBLE.getPack();
  if (forward) {
    if (chapter < pack.book(book).chapterCount) return openChapter(book, chapter + 1, 1);
    if (book + 1 < pack.bookCount()) return openChapter(book + 1, 1, 1);
    return false;
  }
  if (page > 0) return openChapter(book, chapter, 1);  // first, back to the chapter start
  if (chapter > 1) return openChapter(book, chapter - 1, 1);
  if (book > 0) return openChapter(book - 1, pack.book(book - 1).chapterCount, 1);
  return false;
}

void BibleReaderActivity::openNavigator() {
  auto nav = makeUniqueNoThrow<BibleNavigatorActivity>(renderer, mappedInput, bible::Ref{static_cast<uint8_t>(book),
                                                                                         static_cast<uint16_t>(chapter),
                                                                                         layout.pageFirstVerse(page)});
  if (!nav) return;
  startActivityForResult(std::move(nav), [this](const ActivityResult& result) {
    skipNextInput = true;
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    cleanScreenNext = true;
    if (result.isCancelled) return;
    if (const auto* ref = std::get_if<BibleRefResult>(&result.data)) {
      if (openChapter(ref->book, ref->chapter, ref->verse)) highlightVerse = ref->verse > 1 ? ref->verse : 0;
    }
  });
}

void BibleReaderActivity::openMenu() {
  auto menu = makeUniqueNoThrow<BibleMenuActivity>(renderer, mappedInput);
  if (!menu) return;
  startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
    skipNextInput = true;
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    cleanScreenNext = true;
    if (packMissing) {
      if (BIBLE.ensureOpen()) onEnter();
      return;
    }
    relayout();
    if (const auto* menuResult = std::get_if<MenuResult>(&result.data)) {
      if (menuResult->action == BibleMenuActivity::ACTION_GOTO) openNavigator();
    }
  });
}

void BibleReaderActivity::loop() {
  if (skipNextInput) {
    skipNextInput = false;
    return;
  }
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    openMenu();
    return;
  }
  if (packMissing) {
    if (mappedInput.wasReleased(Button::Confirm) && BIBLE.ensureOpen()) onEnter();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    openNavigator();
    return;
  }

  const bool swap = mappedInput.isNavDirectionSwapped();
  const Button prevButtons[] = {Button::PageBack, swap ? Button::Right : Button::Left};
  const Button nextButtons[] = {Button::PageForward, swap ? Button::Left : Button::Right};
  bool changed = false;
  for (const Button b : prevButtons) {
    if (mappedInput.wasLongPressed(b, CHAPTER_HOLD_MS)) {
      changed |= skipChapter(false);
    } else if (mappedInput.wasReleased(b)) {
      changed |= turnPage(false);
    }
  }
  for (const Button b : nextButtons) {
    if (mappedInput.wasLongPressed(b, CHAPTER_HOLD_MS)) {
      changed |= skipChapter(true);
    } else if (mappedInput.wasReleased(b)) {
      changed |= turnPage(true);
    }
  }
  if (changed) requestUpdate();
}

void BibleReaderActivity::drawChrome(int top, int left, int width, int bottom) {
  const int labelFont = BibleFonts::LABEL_FONT_ID;
  const std::string title = BIBLE.chapterLabel(book, chapter);
  renderer.drawText(labelFont, left, top, title.c_str(), true, EpdFontFamily::BOLD);

  const uint16_t first = layout.pageFirstVerse(page);
  const uint16_t last = layout.pageLastVerse(page);
  if (first > 0) {
    std::string range = std::to_string(chapter) + ":" + std::to_string(first);
    if (last > first) range += "-" + std::to_string(last);
    const int w = renderer.getTextWidth(labelFont, range.c_str());
    renderer.drawText(labelFont, left + width - w, top, range.c_str());
  }

  const std::string pageText = std::to_string(page + 1) + " / " + std::to_string(layout.pageCount());
  const int labelHeight = renderer.getLineHeight(labelFont);
  const int w = renderer.getTextWidth(labelFont, pageText.c_str());
  renderer.drawText(labelFont, left + (width - w) / 2, bottom - labelHeight, pageText.c_str());
}

void BibleReaderActivity::drawPage(int top, int left, bool grayPass) {
  int top0, right0, bottom0, left0;
  renderer.getOrientedViewableTRBL(&top0, &right0, &bottom0, &left0);
  const int width = renderer.getScreenWidth() - left0 - right0 - 2 * SIDE_MARGIN;
  const auto& lines = layout.lines();
  const int first = layout.pageFirstLine(page);
  const int end = layout.pageEndLine(page);
  const int spaceWidth = renderer.getSpaceWidth(fontId);
  int y = top;
  for (int i = first; i < end; i++) {
    const bible::LayoutLine& line = lines[i];
    if (i != first) y += line.gapBefore;
    const auto tokens = bible::splitWords(line.text);
    const bool boldLine = line.kind != bible::LineKind::Text;
    const auto style = boldLine ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;

    if (line.kind == bible::LineKind::ChapterTitle) {
      const int w = renderer.getTextWidth(fontId, line.text.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(fontId, left + (width - w) / 2, y + (layout.lineAdvance(line) - lineHeight) / 2,
                        line.text.c_str(), true, EpdFontFamily::BOLD);
      y += layout.lineAdvance(line);
      continue;
    }

    // Ragged right: justified Korean at large sizes opens wide gaps between words.
    const int numberShift = renderer.getFontAscenderSize(fontId) - renderer.getFontAscenderSize(numberFontId);
    int x = left;
    for (const auto& t : tokens) {
      if (isVerseNumber(t)) {
        const char* num = t.c_str() + 1;
        const int w = renderer.getTextWidth(numberFontId, num, EpdFontFamily::BOLD);
        if (highlightVerse > 0 && atoi(num) == highlightVerse) {
          // The boxed number is pure black and white; the grayscale pass leaves it alone.
          if (!grayPass) {
            renderer.fillRoundedRect(x - 4, y + 2, w + 8, lineHeight - 4, 6, Color::Black);
            renderer.drawText(numberFontId, x, y + numberShift, num, false, EpdFontFamily::BOLD);
          }
        } else {
          renderer.drawText(numberFontId, x, y + numberShift, num, true, EpdFontFamily::BOLD);
        }
        x += w + spaceWidth;
      } else {
        renderer.drawText(fontId, x, y, t.c_str(), true, style);
        x += renderer.getTextWidth(fontId, t.c_str(), style) + spaceWidth;
      }
    }
    y += layout.lineAdvance(line);
  }
}

void BibleReaderActivity::render(RenderLock&&) {
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int contentLeft = left + SIDE_MARGIN;
  const int contentWidth = screenW - left - right - 2 * SIDE_MARGIN;
  const int labelHeight = renderer.getLineHeight(BibleFonts::LABEL_FONT_ID);
  const int chromeTop = top + 4;
  const int textTop = chromeTop + labelHeight + 6;
  const int chromeBottom = screenH - bottom - 2;

  renderer.clearScreen();
  if (cleanScreenNext) {
    // Coming from another screen: a full refresh clears what it left in the ink
    // (and on the X3 it is faster than a half refresh, ~770 ms vs 1720 ms).
    cleanScreenNext = false;
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  }
  if (packMissing) {
    const Rect area(contentLeft, top, contentWidth, screenH - top - bottom);
    UITheme::drawCenteredWrappedText(renderer, area, BibleFonts::LABEL_FONT_ID, tr(STR_BIBLE_NO_PACK), 8);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  LOG_DBG("BIBLE", "render %d:%d page %d/%d, heap free %u max %u", book + 1, chapter, page + 1,
          layout.pageCount(), static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  const auto drawAll = [&]() {
    drawChrome(chromeTop, contentLeft, contentWidth, chromeBottom);
    drawPage(textTop, contentLeft);
  };
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  drawAll();  // scan pass
  scope.endScanAndPrewarm();
  drawAll();

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
    ReaderUtils::renderAntiAliased(renderer, [&]() { drawPage(textTop, contentLeft, true); });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
}
