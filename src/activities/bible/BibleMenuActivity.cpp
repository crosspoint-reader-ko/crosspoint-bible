#include "BibleMenuActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "BibleInfoActivity.h"
#include "BibleLanguageActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "bible/BibleFonts.h"
#include "bible/BibleSession.h"
#include "components/UITheme.h"

namespace {
constexpr int MARGIN = 16;
}  // namespace

void BibleMenuActivity::buildRows() {
  rows.clear();
  const LanguageInfo* lang = BIBLE.currentLanguage();
  for (int r = 0; r < ROW_COUNT; r++) {
    if (r == BIBLE_CHOICE && (!lang || lang->bibles.size() < 2)) continue;
    if (r == FONT_FAMILY && (!lang || lang->fontFamilies.size() < 2)) continue;
    rows.push_back(r);
  }
  selected = std::min(selected, static_cast<int>(rows.size()) - 1);
}

void BibleMenuActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  buildRows();
  requestUpdate();
}

std::string BibleMenuActivity::valueOf(int row) const {
  const auto& prefs = BIBLE.prefs;
  switch (row) {
    case FONT_SIZE:
      return std::to_string(BibleFonts::POINTS[prefs.fontSize]) + "pt";
    case LINE_SPACING: {
      static constexpr StrId names[] = {StrId::STR_BIBLE_SPACING_TIGHT, StrId::STR_BIBLE_SPACING_NORMAL,
                                        StrId::STR_BIBLE_SPACING_WIDE};
      return I18N.get(names[prefs.lineSpacing]);
    }
    case LAYOUT:
      return prefs.versePerLine ? tr(STR_BIBLE_LAYOUT_VERSES) : tr(STR_BIBLE_LAYOUT_PARAGRAPHS);
    case HEADINGS:
      return prefs.showHeadings ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case LANGUAGE: {
      // The language's own name (한국어) when the menu font draws it, else the English one.
      const LanguageInfo* lang = BIBLE.currentLanguage();
      if (lang && !lang->name.empty() && renderer.hasGlyphs(BibleFonts::ROW_FONT_ID, lang->name.c_str())) {
        return lang->name;
      }
      return BibleLanguageActivity::englishName(BIBLE.languageCode());
    }
    case BIBLE_CHOICE:
      return BIBLE.isOpen() ? BIBLE.getPack().title() : "";
    case FONT_FAMILY: {
      const LanguageInfo* lang = BIBLE.currentLanguage();
      const std::string& chosen = BIBLE.chosenFontFamily();
      if (!lang || lang->fontFamilies.empty()) return "Noto Serif";
      for (const auto& f : lang->fontFamilies) {
        if (f == chosen) return f;
      }
      return lang->fontFamilies.front();
    }
    case SMOOTHING:
      return SETTINGS.textAntiAliasing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case ORIENTATION: {
      static constexpr StrId names[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                        StrId::STR_LANDSCAPE_CCW};
      return I18N.get(names[std::min<uint8_t>(SETTINGS.orientation, 3)]);
    }
    default:
      return "";
  }
}

void BibleMenuActivity::activate(int row) {
  auto& prefs = BIBLE.prefs;
  switch (row) {
    case GOTO:
    case ADD_BOOKMARK:
    case BOOKMARKS: {
      MenuResult r;
      r.action = row == GOTO ? ACTION_GOTO : row == ADD_BOOKMARK ? ACTION_ADD_BOOKMARK : ACTION_BOOKMARKS;
      setResult(r);
      finish();
      return;
    }
    case SMOOTHING:
      // Off: one display refresh per page instead of two (no grayscale pass).
      SETTINGS.textAntiAliasing = SETTINGS.textAntiAliasing ? 0 : 1;
      SETTINGS.saveToFile();
      break;
    case FONT_SIZE:
      prefs.fontSize = static_cast<uint8_t>((prefs.fontSize + 1) % BibleFonts::COUNT);
      break;
    case LINE_SPACING:
      prefs.lineSpacing = static_cast<uint8_t>((prefs.lineSpacing + 1) % BibleSession::LINE_SPACING_COUNT);
      break;
    case LAYOUT:
      prefs.versePerLine = prefs.versePerLine ? 0 : 1;
      break;
    case HEADINGS:
      prefs.showHeadings = prefs.showHeadings ? 0 : 1;
      break;
    case ORIENTATION:
      SETTINGS.orientation = static_cast<uint8_t>((SETTINGS.orientation + 1) % CrossPointSettings::ORIENTATION_COUNT);
      SETTINGS.saveToFile();
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
      break;
    case LANGUAGE:
      if (auto screen = makeUniqueNoThrow<BibleLanguageActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(screen), [this](const ActivityResult&) {
          BIBLE.ensureOpen();  // the new language's Bible, for the menu's own values
          buildRows();
          requestUpdate();
        });
      }
      return;
    case BIBLE_CHOICE:
    case FONT_FAMILY: {
      // Cycle to the next Bible / reader font of the current language.
      const LanguageInfo* lang = BIBLE.currentLanguage();
      if (!lang) return;
      if (row == BIBLE_CHOICE) {
        const std::string current = BIBLE.isOpen() ? BIBLE.packPath() : "";
        size_t i = 0;
        while (i < lang->bibles.size() && lang->bibles[i].path != current) i++;
        BIBLE.setBible(lang->bibles[(i + 1) % lang->bibles.size()].path);
        BIBLE.ensureOpen();
      } else {
        const std::string current = valueOf(FONT_FAMILY);
        size_t i = 0;
        while (i < lang->fontFamilies.size() && lang->fontFamilies[i] != current) i++;
        const std::string next = lang->fontFamilies[(i + 1) % lang->fontFamilies.size()];
        BIBLE.setFontFamily(next);
        BibleFonts::apply(renderer, lang, next);
      }
      prefsChanged = true;
      requestUpdate();
      return;
    }
    case INFO:
      if (auto info = makeUniqueNoThrow<BibleInfoActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(info), [](const ActivityResult&) {});
      }
      return;
    case SETTINGS_ROW:
      activityManager.goToSettings();
      return;
    default:
      return;
  }
  prefsChanged = true;
  BIBLE.saveState();
  requestUpdate();
}

void BibleMenuActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    activate(rows[selected]);
    return;
  }
  const int n = static_cast<int>(rows.size());
  buttonNavigator.onNextRelease([this, n] {
    selected = ButtonNavigator::nextIndex(selected, n);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, n] {
    selected = ButtonNavigator::previousIndex(selected, n);
    requestUpdate();
  });
}

void BibleMenuActivity::render(RenderLock&&) {
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int x = left + MARGIN;
  const int width = screenW - left - right - 2 * MARGIN;
  const int hintsHeight = UITheme::getInstance().getMetrics().buttonHintsHeight;

  static constexpr StrId labels[ROW_COUNT] = {
      StrId::STR_BIBLE_GOTO,   StrId::STR_BIBLE_ADD_BOOKMARK, StrId::STR_BIBLE_BOOKMARKS, StrId::STR_LANGUAGE,
      StrId::STR_BIBLE_CHOICE, StrId::STR_BIBLE_FONT,         StrId::STR_BIBLE_FONT_SIZE, StrId::STR_BIBLE_LINE_SPACING,
      StrId::STR_BIBLE_LAYOUT, StrId::STR_BIBLE_HEADINGS,     StrId::STR_BIBLE_SMOOTHING, StrId::STR_ORIENTATION,
      StrId::STR_BIBLE_INFO,   StrId::STR_SETTINGS_TITLE,
  };

  const int titleFont = BibleFonts::TITLE_FONT_ID;
  const int titleH = renderer.getLineHeight(titleFont) + 16;
  const int available = screenH - top - bottom - hintsHeight - titleH - 10;
  // Rows in pages: the 16 pt font (14 pt in landscape) with room around each row.
  const int rowFont = available >= 8 * (renderer.getLineHeight(BibleFonts::ROW_FONT_ID) + 20)
                          ? BibleFonts::ROW_FONT_ID
                          : BibleFonts::LABEL_FONT_ID;
  const int rowH = renderer.getLineHeight(rowFont) + 20;
  const int perPage = std::max(1, available / rowH);
  const int count = static_cast<int>(rows.size());
  const int first = (selected / perPage) * perPage;
  const int pages = (count + perPage - 1) / perPage;

  const auto drawAll = [&]() {
    int y = top + 10;
    renderer.drawText(titleFont, x, y, tr(STR_BIBLE_MENU), true, EpdFontFamily::BOLD);
    if (pages > 1) {
      const std::string p = std::to_string(first / perPage + 1) + " / " + std::to_string(pages);
      const int w = renderer.getTextWidth(BibleFonts::LABEL_FONT_ID, p.c_str());
      renderer.drawText(BibleFonts::LABEL_FONT_ID, x + width - w, y + 6, p.c_str());
    }
    y += titleH;
    for (int i = first; i < std::min(count, first + perPage); i++) {
      const int row = rows[i];
      const bool sel = i == selected;
      if (sel) renderer.fillRoundedRect(x - 6, y, width + 12, rowH - 4, 8, Color::Black);
      const int ty = y + (rowH - 4 - renderer.getLineHeight(rowFont)) / 2;
      renderer.drawText(rowFont, x + 6, ty, I18N.get(labels[row]), !sel, EpdFontFamily::BOLD);
      const std::string value = valueOf(row);
      if (!value.empty()) {
        const int labelW = renderer.getTextWidth(rowFont, I18N.get(labels[row]), EpdFontFamily::BOLD);
        const std::string shown = renderer.truncatedText(rowFont, value.c_str(), width - labelW - 30);
        const int w = renderer.getTextWidth(rowFont, shown.c_str());
        renderer.drawText(rowFont, x + width - 6 - w, ty, shown.c_str(), !sel);
      }
      y += rowH;
    }
  };

  renderer.clearScreen();
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  drawAll();
  scope.endScanAndPrewarm();
  drawAll();

  const auto hints =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_BIBLE_HINT_PREV), tr(STR_BIBLE_HINT_NEXT));
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  // Differential refresh only: a HALF refresh is three drive phases (three flashes) on the X3.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
