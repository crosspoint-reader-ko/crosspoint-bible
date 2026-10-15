#include "BibleReaderActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Hyphenation.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "BibleBookmarksActivity.h"
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
// Space between the header line (book, chapter, verse range) and the text.
constexpr int HEADER_GAP = 18;
constexpr float LINE_SPACING[BibleSession::LINE_SPACING_COUNT] = {1.0f, 1.2f, 1.45f};

bool isVerseNumber(const std::string& token) { return !token.empty() && token[0] == bible::VERSE_NUMBER_MARK; }
}  // namespace

void BibleReaderActivity::onEnter() {
  Activity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
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
  style.height = renderer.getScreenHeight() - top - bottom - 2 * labelHeight - HEADER_GAP - 8;
  style.lineHeight = lineHeight;
  style.titleLineHeight = lineHeight * 3 / 2;
  style.blockGap = lineHeight / 2;
  style.versePerLine = BIBLE.prefs.versePerLine != 0;
  // Per-language line breaking (pack metadata, docs/languages.md): character wrap for
  // Korean when justified and always for Chinese/Japanese (no spaces); word wrap with
  // hyphenation where the firmware has patterns; right-to-left lines for Arabic, Persian, Hebrew.
  const std::string lang = BIBLE.getPack().language();
  const bool cjk = lang == "ja" || lang == "zh";
  style.charWrap = cjk || (BIBLE.meta("wrap") == "char" || lang == "ko") && BIBLE.prefs.justify != 0;
  style.rtl = BIBLE.meta("direction") == "rtl";
  std::string hyph = BIBLE.meta("hyphenation");
  if (hyph.empty() && lang == "en") hyph = "en";
  if (!style.charWrap && !hyph.empty() && hyphenation::hasLanguage(hyph.c_str())) {
    style.hyphenate = [hyph](const std::string& word) { return hyphenation::breakPoints(hyph.c_str(), word); };
  }
  style.spaceWidth = renderer.getSpaceWidth(fontId);
  const int fid = fontId;
  // Pen advances, not ink widths: drawText starts each word at the pen position, so the
  // layout must measure what the pen moves, or word gaps vary with the glyphs' bearings.
  style.measure = [this, fid](const std::string& token, bool bold) {
    return renderer.getTextAdvanceX(fid, token.c_str(), bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  };
  const int nid = numberFontId;
  style.measureNumber = [this, nid](const std::string& digits) {
    return renderer.getTextAdvanceX(nid, digits.c_str(), EpdFontFamily::BOLD);
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
  notice.clear();
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
  notice.clear();
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
    if (result.isCancelled) return;
    if (const auto* ref = std::get_if<BibleRefResult>(&result.data)) {
      if (openChapter(ref->book, ref->chapter, ref->verse)) highlightVerse = ref->verse > 1 ? ref->verse : 0;
    }
  });
}

void BibleReaderActivity::openBookmarks(bool add) {
  // The bookmarked verse: the one just jumped to, else the first verse on the page.
  const uint16_t verse = highlightVerse > 0 ? highlightVerse : std::max<uint16_t>(1, layout.pageFirstVerse(page));
  const bible::Bookmark here{static_cast<uint8_t>(book), static_cast<uint16_t>(chapter), verse};
  auto screen = makeUniqueNoThrow<BibleBookmarksActivity>(
      renderer, mappedInput, add ? BibleBookmarksActivity::Mode::AddTo : BibleBookmarksActivity::Mode::Browse, here);
  if (!screen) return;
  startActivityForResult(std::move(screen), [this](const ActivityResult& result) {
    skipNextInput = true;
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    if (result.isCancelled) return;
    if (const auto* folderName = std::get_if<TextResult>(&result.data)) {
      notice = std::string(tr(STR_BIBLE_BOOKMARKED)) + " " + folderName->text;
    } else if (const auto* ref = std::get_if<BibleRefResult>(&result.data)) {
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
    if (packMissing) {
      if (BIBLE.ensureOpen()) onEnter();
      return;
    }
    // A language or Bible switch in the menu closed the pack: open the new one.
    if (!BIBLE.isOpen() && !BIBLE.ensureOpen()) {
      packMissing = true;
      return;
    }
    const bible::Ref pos = BIBLE.getPack().clamp(bible::Ref{static_cast<uint8_t>(book), static_cast<uint16_t>(chapter),
                                                            layout.pageFirstVerse(page)});
    book = pos.book;
    chapter = pos.chapter;
    relayout();
    if (const auto* menuResult = std::get_if<MenuResult>(&result.data)) {
      switch (menuResult->action) {
        case BibleMenuActivity::ACTION_GOTO:
          openNavigator();
          break;
        case BibleMenuActivity::ACTION_ADD_BOOKMARK:
          openBookmarks(true);
          break;
        case BibleMenuActivity::ACTION_BOOKMARKS:
          openBookmarks(false);
          break;
        default:
          break;
      }
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
  if (!notice.empty()) {
    // One-time confirmation (e.g. "북마크 추가: 감사") in place of the verse range.
    const std::string shown = renderer.truncatedText(labelFont, notice.c_str(), width / 2);
    const int w = renderer.getTextWidth(labelFont, shown.c_str());
    renderer.drawText(labelFont, left + width - w, top, shown.c_str(), true, EpdFontFamily::BOLD);
  } else if (first > 0) {
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
  const bool justified = BIBLE.prefs.justify != 0;
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

    const int numberShift = renderer.getFontAscenderSize(fontId) - renderer.getFontAscenderSize(numberFontId);
    const auto drawNumber = [&](const char* num, int x, int w) {
      if (highlightVerse > 0 && atoi(num) == highlightVerse) {
        // The boxed number is pure black and white; the grayscale pass leaves it alone.
        if (!grayPass) {
          renderer.fillRoundedRect(x - 4, y + 2, w + 8, lineHeight - 4, 6, Color::Black);
          renderer.drawText(numberFontId, x, y + numberShift, num, false, EpdFontFamily::BOLD);
        }
      } else {
        renderer.drawText(numberFontId, x, y + numberShift, num, true, EpdFontFamily::BOLD);
      }
    };

    // Units are drawn left to right (right to left on RTL lines). Widths are pen advances, as
    // the layout measured them. Letter spacing stays the font's own: justified lines widen the
    // spaces only, except CJK lines (spreadChars), which have few spaces and widen every gap
    // between characters.
    struct Unit {
      std::string text;
      int width;
      bool number;
      bool spaceAfter;
    };
    const bool spread = justified && line.justify && line.spreadChars;
    std::vector<Unit> units;
    int natural = 0;
    for (size_t k = 0; k < tokens.size(); k++) {
      const std::string& t = tokens[k];
      const bool last = k + 1 == tokens.size();
      if (isVerseNumber(t)) {
        units.push_back({t.substr(1), renderer.getTextAdvanceX(numberFontId, t.c_str() + 1, EpdFontFamily::BOLD),
                         true, !last});
        natural += units.back().width;
      } else if (spread) {
        for (size_t p = 0; p < t.size();) {
          const auto c = static_cast<unsigned char>(t[p]);
          size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
          n = std::min(n, t.size() - p);
          std::string ch = t.substr(p, n);
          const int w = renderer.getTextAdvanceX(fontId, ch.c_str(), style);
          p += n;
          units.push_back({std::move(ch), w, false, p >= t.size() && !last});
          natural += w;
        }
      } else {
        units.push_back({t, renderer.getTextAdvanceX(fontId, t.c_str(), style), false, !last});
        natural += units.back().width;
      }
      if (!last) natural += spaceWidth;
    }
    const int unitCount = static_cast<int>(units.size());
    const int gaps = spread ? unitCount - 1 : static_cast<int>(tokens.size()) - 1;
    int extra = 0;
    if (justified && line.justify && line.kind == bible::LineKind::Text && gaps > 0) {
      extra = std::max(0, width - natural);
      // A line of a few long words (Malayalam, a long English word wrapped) would open gaps
      // several spaces wide; such a line stays ragged instead.
      if (!spread && extra > gaps * spaceWidth * 5 / 2) extra = 0;
    }
    int gapIndex = 0;
    int pos = 0;  // distance travelled from the line start edge
    for (int k = 0; k < unitCount; k++) {
      const Unit& u = units[k];
      const int x = line.rtl ? left + width - pos - u.width : left + pos;
      if (u.number) {
        drawNumber(u.text.c_str(), x, u.width);
      } else {
        renderer.drawText(fontId, x, y, u.text.c_str(), true, style);
      }
      pos += u.width + (u.spaceAfter ? spaceWidth : 0);
      // One share of the leftover per stretchable gap: after every unit when spreading,
      // after a space otherwise.
      if (k + 1 < unitCount && extra > 0 && (spread || u.spaceAfter)) {
        pos += extra / gaps + (gapIndex < extra % gaps ? 1 : 0);
        gapIndex++;
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
  const int textTop = chromeTop + labelHeight + HEADER_GAP;
  const int chromeBottom = screenH - bottom - 2;

  renderer.clearScreen();
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

  // Returning from the navigator or menu is a page turn like any other: the differential
  // refresh replaces the whole screen. Forcing a clean refresh there flashed three times on
  // the X3 (its HALF waveform runs three drive phases); the periodic cleanup every
  // SETTINGS.getRefreshFrequency() pages handles ghosting.
  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
    ReaderUtils::renderAntiAliased(renderer, [&]() { drawPage(textTop, contentLeft, true); });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
}
