#include "BibleInfoActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "bible/BibleFonts.h"
#include "bible/BibleSession.h"
#include "components/UITheme.h"

namespace {
constexpr int MARGIN = 16;

std::string withCommas(uint32_t n) {
  std::string s = std::to_string(n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
  return s;
}

const char* labelFor(const std::string& key) {
  if (key == "edition") return tr(STR_BIBLE_INFO_EDITION);
  if (key == "copyright") return tr(STR_BIBLE_INFO_COPYRIGHT);
  if (key == "license") return tr(STR_BIBLE_INFO_LICENSE);
  if (key == "source") return tr(STR_BIBLE_INFO_SOURCE);
  if (key == "version") return tr(STR_BIBLE_INFO_VERSION);
  if (key == "note") return tr(STR_BIBLE_INFO_NOTE);
  return nullptr;
}

// Pack metadata the firmware reads itself (docs/languages.md): not shown as rows.
bool isInternal(const std::string& key) {
  static const char* const keys[] = {"language_name", "direction",   "wrap",     "hyphenation",
                                     "shaped",        "chapter_fmt", "psalm_fmt"};
  return std::find(std::begin(keys), std::end(keys), key) != std::end(keys);
}
}  // namespace

void BibleInfoActivity::onEnter() {
  Activity::onEnter();
  rows.clear();
  page = 0;
  if (BIBLE.ensureOpen()) {
    auto& pack = BIBLE.getPack();
    rows.emplace_back(tr(STR_BIBLE_INFO_NAME), pack.title());
    for (const auto& [key, value] : pack.metadata()) {
      if (isInternal(key)) continue;
      if (key == "edition" && value == pack.title()) continue;  // same as the name row
      const char* label = labelFor(key);
      rows.emplace_back(label ? label : key, value);
    }
    // "66권 · 1,189장 · 31,102절"
    std::string size = BibleSession::format(tr(STR_BIBLE_INFO_BOOKS_FMT), pack.bookCount());
    std::string chapters = tr(STR_BIBLE_INFO_CHAPTERS_FMT);
    std::string verses = tr(STR_BIBLE_INFO_VERSES_FMT);
    chapters.replace(chapters.find("%d"), 2, withCommas(pack.totalChapters()));
    verses.replace(verses.find("%d"), 2, withCommas(pack.totalVerses()));
    size += " · " + chapters + " · " + verses;
    rows.emplace_back(tr(STR_BIBLE_INFO_SIZE), size);
    if (pack.totalHeadings() > 0) {
      std::string headings = tr(STR_BIBLE_INFO_HEADINGS_FMT);
      headings.replace(headings.find("%d"), 2, withCommas(pack.totalHeadings()));
      rows.emplace_back(tr(STR_BIBLE_HEADINGS), headings);
    }
    const std::string languageName = BIBLE.meta("language_name");
    rows.emplace_back(tr(STR_BIBLE_INFO_LANGUAGE),
                      languageName.empty() ? pack.language() : languageName + " (" + pack.language() + ")");
    const std::string& path = BIBLE.packPath();
    rows.emplace_back(tr(STR_BIBLE_INFO_LOCATION),
                      path == BibleSession::BUILT_IN_PATH ? std::string(tr(STR_BIBLE_INFO_BUILT_IN)) : path);
  } else {
    rows.emplace_back(tr(STR_BIBLE_INFO_NAME), tr(STR_BIBLE_NO_PACK));
  }
  requestUpdate();
}

void BibleInfoActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back) || mappedInput.wasReleased(Button::Confirm)) {
    finish();
    return;
  }
  const bool next = mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::Right);
  const bool prev = mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Left);
  if (next && page + 1 < pageCount) {
    page++;
    requestUpdate();
  } else if (prev && page > 0) {
    page--;
    requestUpdate();
  }
}

void BibleInfoActivity::render(RenderLock&&) {
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int x = left + MARGIN;
  const int width = screenW - left - right - 2 * MARGIN;
  const int hintsHeight = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int titleFont = BibleFonts::IDS[3];
  const int labelFont = BibleFonts::LABEL_FONT_ID;
  const int valueFont = BibleFonts::IDS[1];
  const int labelH = renderer.getLineHeight(labelFont);
  const int valueH = renderer.getLineHeight(valueFont);

  const int listTop = top + 10 + renderer.getLineHeight(titleFont) + 16;
  const int listBottom = screenH - bottom - hintsHeight - 8;

  // Lay rows out into pages: label line + wrapped value lines + gap.
  struct Placed {
    int row;
    std::vector<std::string> lines;
    int page;
    int y;
  };
  std::vector<Placed> placed;
  int pg = 0;
  int y = listTop;
  for (int i = 0; i < static_cast<int>(rows.size()); i++) {
    auto lines = renderer.wrappedText(valueFont, rows[i].second.c_str(), width, 6);
    const int h = labelH + static_cast<int>(lines.size()) * valueH + 14;
    if (y + h > listBottom && y > listTop) {
      pg++;
      y = listTop;
    }
    placed.push_back({i, std::move(lines), pg, y});
    y += h;
  }
  pageCount = pg + 1;
  page = std::min(page, pageCount - 1);

  const auto drawAll = [&]() {
    renderer.drawText(titleFont, x, top + 10, tr(STR_BIBLE_INFO), true, EpdFontFamily::BOLD);
    renderer.drawLine(x, listTop - 8, x + width, listTop - 8, 2, true);
    for (const auto& p : placed) {
      if (p.page != page) continue;
      renderer.drawText(labelFont, x, p.y, rows[p.row].first.c_str(), true, EpdFontFamily::BOLD);
      int ly = p.y + labelH;
      for (const auto& line : p.lines) {
        renderer.drawText(valueFont, x, ly, line.c_str());
        ly += valueH;
      }
    }
    if (pageCount > 1) {
      const std::string text = std::to_string(page + 1) + " / " + std::to_string(pageCount);
      const int w = renderer.getTextWidth(labelFont, text.c_str());
      renderer.drawText(labelFont, x + width - w, top + 10, text.c_str());
    }
  };

  renderer.clearScreen();
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  drawAll();
  scope.endScanAndPrewarm();
  drawAll();

  const auto hints = mappedInput.mapLabels(tr(STR_BACK), "", pageCount > 1 ? tr(STR_BIBLE_HINT_PREV) : "",
                                           pageCount > 1 ? tr(STR_BIBLE_HINT_NEXT) : "");
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
