#include "BibleBookmarksActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "BibleTextInputActivity.h"
#include "MappedInputManager.h"
#include "bible/BibleFonts.h"
#include "bible/BibleSession.h"
#include "components/UITheme.h"

namespace {
constexpr unsigned long DELETE_HOLD_MS = 700;
constexpr int MARGIN = 14;
}  // namespace

void BibleBookmarksActivity::onEnter() {
  Activity::onEnter();
  BIBLE.bookmarks();
  level = Level::Folders;
  selected = 0;
  requestUpdate();
}

int BibleBookmarksActivity::rowCount() const {
  auto& store = BIBLE.bookmarks();
  if (level == Level::Folders) {
    // Last row: new folder (unless full).
    const int n = static_cast<int>(store.folderCount());
    return n + (store.folderCount() < bible::BookmarkStore::MAX_FOLDERS ? 1 : 0);
  }
  return static_cast<int>(store.folders()[folder].items.size());
}

void BibleBookmarksActivity::move(int delta) {
  const int n = rowCount();
  if (n <= 0) return;
  selected = (selected + delta + n) % n;
  requestUpdate();
}

void BibleBookmarksActivity::createFolder() {
  auto input = makeUniqueNoThrow<BibleTextInputActivity>(renderer, mappedInput, tr(STR_BIBLE_NEW_FOLDER_NAME),
                                                         bible::BookmarkStore::MAX_NAME_BYTES);
  if (!input) return;
  startActivityForResult(std::move(input), [this](const ActivityResult& result) {
    skipNextInput = true;
    if (result.isCancelled) return;
    const auto* text = std::get_if<TextResult>(&result.data);
    if (!text) return;
    auto& store = BIBLE.bookmarks();
    const int index = store.addFolder(text->text);
    if (index < 0) return;
    BIBLE.saveBookmarks();
    selected = index;
    if (mode == Mode::AddTo) activate();  // a new folder made while bookmarking takes the bookmark
  });
}

void BibleBookmarksActivity::activate() {
  auto& store = BIBLE.bookmarks();
  if (level == Level::Folders) {
    if (selected >= static_cast<int>(store.folderCount())) {
      createFolder();
      return;
    }
    if (mode == Mode::AddTo) {
      store.add(selected, toAdd);
      BIBLE.saveBookmarks();
      setResult(TextResult{store.folders()[selected].name});
      finish();
      return;
    }
    folder = selected;
    folderRow = selected;
    selected = 0;
    level = Level::Items;
    requestUpdate();
    return;
  }
  const auto& items = store.folders()[folder].items;
  if (selected >= static_cast<int>(items.size())) return;
  const bible::Bookmark& b = items[selected];
  BibleRefResult r;
  r.book = b.book;
  r.chapter = b.chapter;
  r.verse = b.verse;
  setResult(r);
  finish();
}

void BibleBookmarksActivity::askDelete() {
  auto& store = BIBLE.bookmarks();
  if (level == Level::Folders && selected >= static_cast<int>(store.folderCount())) return;
  if (level == Level::Items && selected >= static_cast<int>(store.folders()[folder].items.size())) return;
  confirmingDelete = true;
  requestUpdate();
}

void BibleBookmarksActivity::deleteConfirmed() {
  confirmingDelete = false;
  auto& store = BIBLE.bookmarks();
  if (level == Level::Folders) {
    store.removeFolder(selected);
    store.ensureDefaultFolder(tr(STR_BIBLE_DEFAULT_FOLDER));
  } else {
    store.remove(folder, selected);
  }
  BIBLE.saveBookmarks();
  selected = std::min(selected, std::max(0, rowCount() - 1));
  requestUpdate();
}

void BibleBookmarksActivity::loop() {
  if (skipNextInput) {
    skipNextInput = false;
    return;
  }
  using Button = MappedInputManager::Button;
  if (confirmingDelete) {
    if (mappedInput.wasReleased(Button::Confirm)) {
      deleteConfirmed();
    } else if (mappedInput.wasReleased(Button::Back)) {
      confirmingDelete = false;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    if (level == Level::Items) {
      level = Level::Folders;
      selected = folderRow;
      requestUpdate();
      return;
    }
    ActivityResult r;
    r.isCancelled = true;
    setResult(std::move(r));
    finish();
    return;
  }
  if (mappedInput.wasLongPressed(Button::Confirm, DELETE_HOLD_MS)) return askDelete();
  if (mappedInput.wasReleased(Button::Confirm)) return activate();
  const bool swap = mappedInput.isNavDirectionSwapped();
  if (mappedInput.wasReleased(swap ? Button::Right : Button::Left) || mappedInput.wasReleased(Button::Up)) {
    return move(-1);
  }
  if (mappedInput.wasReleased(swap ? Button::Left : Button::Right) || mappedInput.wasReleased(Button::Down)) {
    return move(1);
  }
}

std::string BibleBookmarksActivity::snippet(const bible::Bookmark& b) {
  auto& pack = BIBLE.getPack();
  std::string text;
  const int32_t vi = pack.verseIndex({b.book, b.chapter, b.verse});
  if (vi >= 0) pack.verseText(static_cast<uint32_t>(vi), text);
  return text;
}

void BibleBookmarksActivity::drawList(int top, int left, int width, int height) {
  auto& store = BIBLE.bookmarks();
  const int rowFont = BibleFonts::IDS[2];
  const int smallFont = BibleFonts::LABEL_FONT_ID;
  const bool items = level == Level::Items;
  const int rowH = renderer.getLineHeight(rowFont) + (items ? renderer.getLineHeight(smallFont) + 6 : 0) + 22;
  const int perPage = std::max(1, height / rowH);
  const int n = rowCount();
  const int first = (selected / perPage) * perPage;

  if (n == 0) {
    renderer.drawText(rowFont, left, top + 10, tr(STR_BIBLE_NO_BOOKMARKS));
    return;
  }
  for (int i = first; i < std::min(n, first + perPage); i++) {
    const int y = top + (i - first) * rowH;
    const bool sel = i == selected;
    if (sel) renderer.fillRoundedRect(left - 6, y, width + 12, rowH - 4, 8, Color::Black);
    const int ty = y + 10;
    if (!items) {
      std::string label;
      std::string count;
      if (i < static_cast<int>(store.folderCount())) {
        label = store.folders()[i].name;
        count = std::to_string(store.folders()[i].items.size());
      } else {
        label = std::string("+ ") + tr(STR_BIBLE_NEW_FOLDER);
      }
      const int cw = count.empty() ? 0 : renderer.getTextWidth(rowFont, count.c_str());
      const std::string shown = renderer.truncatedText(rowFont, label.c_str(), width - cw - 30, EpdFontFamily::BOLD);
      renderer.drawText(rowFont, left + 6, ty, shown.c_str(), !sel, EpdFontFamily::BOLD);
      if (!count.empty()) renderer.drawText(rowFont, left + width - 6 - cw, ty, count.c_str(), !sel);
    } else {
      const bible::Bookmark& b = store.folders()[folder].items[i];
      renderer.drawText(rowFont, left + 6, ty, BIBLE.referenceLabel(b).c_str(), !sel, EpdFontFamily::BOLD);
      const std::string text = renderer.truncatedText(smallFont, snippet(b).c_str(), width - 12);
      renderer.drawText(smallFont, left + 6, ty + renderer.getLineHeight(rowFont) + 4, text.c_str(), !sel);
    }
  }
  const int pages = (n + perPage - 1) / perPage;
  if (pages > 1) {
    const std::string p = std::to_string(first / perPage + 1) + " / " + std::to_string(pages);
    const int w = renderer.getTextWidth(smallFont, p.c_str());
    renderer.drawText(smallFont, left + (width - w) / 2, top + height - renderer.getLineHeight(smallFont), p.c_str());
  }
}

void BibleBookmarksActivity::drawConfirm(int left, int width, int screenH) {
  auto& store = BIBLE.bookmarks();
  std::string question;
  if (level == Level::Folders) {
    const auto& f = store.folders()[selected];
    question = BibleSession::format(tr(STR_BIBLE_DELETE_FOLDER_FMT), static_cast<int>(f.items.size()));
    question = "'" + f.name + "' " + question;
  } else {
    question = BIBLE.referenceLabel(store.folders()[folder].items[selected]) + " " + tr(STR_BIBLE_DELETE_BOOKMARK);
  }
  const int font = BibleFonts::IDS[1];
  const int boxH = renderer.getLineHeight(font) * 4 + 40;
  const int y = (screenH - boxH) / 2;
  renderer.fillRect(left - 8, y, width + 16, boxH, false);
  renderer.drawRoundedRect(left - 8, y, width + 16, boxH, 3, 10, true);
  UITheme::drawCenteredWrappedText(renderer, Rect(left, y + 10, width, boxH - 20), font, question.c_str(), 4);
}

void BibleBookmarksActivity::render(RenderLock&&) {
  auto& store = BIBLE.bookmarks();
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int x = left + MARGIN;
  const int width = screenW - left - right - 2 * MARGIN;
  const int hintsHeight = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int titleFont = BibleFonts::IDS[3];
  const int labelFont = BibleFonts::LABEL_FONT_ID;

  std::string title;
  std::string subtitle;
  if (mode == Mode::AddTo) {
    title = tr(STR_BIBLE_ADD_BOOKMARK);
    subtitle = BIBLE.referenceLabel(toAdd) + " · " + tr(STR_BIBLE_PICK_FOLDER);
  } else if (level == Level::Folders) {
    title = tr(STR_BIBLE_BOOKMARKS);
    subtitle = tr(STR_BIBLE_HOLD_TO_DELETE);
  } else {
    title = store.folders()[folder].name;
    subtitle = tr(STR_BIBLE_HOLD_TO_DELETE);
  }

  const auto drawAll = [&]() {
    int y = top + 10;
    const std::string shownTitle = renderer.truncatedText(titleFont, title.c_str(), width, EpdFontFamily::BOLD);
    renderer.drawText(titleFont, x, y, shownTitle.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(titleFont);
    renderer.drawText(labelFont, x, y, subtitle.c_str());
    y += renderer.getLineHeight(labelFont) + 6;
    renderer.drawLine(x, y, x + width, y, 2, true);
    y += 12;
    drawList(y, x, width, screenH - bottom - hintsHeight - 8 - y);
    if (confirmingDelete) drawConfirm(x, width, screenH);
  };

  renderer.clearScreen();
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  drawAll();
  scope.endScanAndPrewarm();
  drawAll();

  const auto hints = confirmingDelete
                         ? mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_BIBLE_DELETE), "", "")
                         : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_BIBLE_HINT_PREV),
                                                 tr(STR_BIBLE_HINT_NEXT));
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
