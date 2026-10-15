#include "BibleLanguageActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "bible/BibleFonts.h"
#include "bible/BibleSession.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MARGIN = 16;
struct Name {
  const char* code;
  const char* english;
};
constexpr Name NAMES[] = {
    {"ar", "Arabic"},  {"da", "Danish"},     {"de", "German"},  {"en", "English"},    {"es", "Spanish"},
    {"fa", "Persian"}, {"fi", "Finnish"},    {"fr", "French"},  {"he", "Hebrew"},     {"hi", "Hindi"},
    {"id", "Indonesian"}, {"is", "Icelandic"}, {"it", "Italian"}, {"ja", "Japanese"},  {"ko", "Korean"},
    {"ml", "Malayalam"}, {"nb", "Norwegian"}, {"nl", "Dutch"},   {"pt", "Portuguese"}, {"ru", "Russian"},
    {"so", "Somali"},  {"sv", "Swedish"},    {"sw", "Swahili"}, {"zh", "Chinese"},
};
}  // namespace

const char* BibleLanguageActivity::englishName(const std::string& code) {
  for (const auto& n : NAMES) {
    if (code == n.code) return n.english;
  }
  return code.c_str();
}

void BibleLanguageActivity::onEnter() {
  Activity::onEnter();
  BIBLE.rescanLanguages();  // a bundle may have been copied since boot
  const auto& langs = BIBLE.languages();
  selected = 0;
  for (int i = 0; i < static_cast<int>(langs.size()); i++) {
    if (langs[i].code == BIBLE.languageCode()) selected = i;
  }
  requestUpdate();
}

void BibleLanguageActivity::loop() {
  using Button = MappedInputManager::Button;
  const auto& langs = BIBLE.languages();
  const int n = static_cast<int>(langs.size());
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm) && n > 0) {
    if (langs[selected].code != BIBLE.languageCode()) {
      BIBLE.setLanguage(langs[selected].code);
      BIBLE.applyLanguage(renderer);
      SETTINGS.saveToFile();
    }
    finish();
    return;
  }
  const bool swap = mappedInput.isNavDirectionSwapped();
  if (n > 0 && (mappedInput.wasReleased(swap ? Button::Right : Button::Left) || mappedInput.wasReleased(Button::Up))) {
    selected = (selected + n - 1) % n;
    requestUpdate();
  }
  if (n > 0 && (mappedInput.wasReleased(swap ? Button::Left : Button::Right) || mappedInput.wasReleased(Button::Down))) {
    selected = (selected + 1) % n;
    requestUpdate();
  }
}

void BibleLanguageActivity::render(RenderLock&&) {
  const auto& langs = BIBLE.languages();
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int x = left + MARGIN;
  const int width = screenW - left - right - 2 * MARGIN;
  const int hintsHeight = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int titleFont = BibleFonts::IDS[3];
  const int rowFont = BibleFonts::IDS[1];
  const int nativeFont = UI_FONT_ID;
  const int labelFont = BibleFonts::LABEL_FONT_ID;
  const int titleH = renderer.getLineHeight(titleFont) + renderer.getLineHeight(labelFont) + 18;
  const int rowH = renderer.getLineHeight(rowFont) + 22;
  const int listTop = top + 10 + titleH;
  const int perPage = std::max(1, (screenH - bottom - hintsHeight - 8 - listTop) / rowH);
  const int first = (selected / perPage) * perPage;

  const auto drawAll = [&]() {
    renderer.drawText(titleFont, x, top + 10, tr(STR_LANGUAGE), true, EpdFontFamily::BOLD);
    renderer.drawText(labelFont, x, top + 10 + renderer.getLineHeight(titleFont), tr(STR_BIBLE_LANGUAGE_HINT));
    renderer.drawLine(x, listTop - 8, x + width, listTop - 8, 2, true);
    for (int i = first; i < std::min<int>(static_cast<int>(langs.size()), first + perPage); i++) {
      const auto& l = langs[i];
      const int y = listTop + (i - first) * rowH;
      const bool sel = i == selected;
      if (sel) renderer.fillRoundedRect(x - 6, y, width + 12, rowH - 4, 8, Color::Black);
      const int ty = y + (rowH - 4 - renderer.getLineHeight(rowFont)) / 2;
      std::string label = englishName(l.code);
      if (l.code == BIBLE.languageCode()) label = "• " + label;
      renderer.drawText(rowFont, x + 6, ty, label.c_str(), !sel, EpdFontFamily::BOLD);
      // The native name only when the current UI font can draw it (e.g. not Arabic in an English UI).
      if (l.name != l.code && renderer.hasGlyphs(nativeFont, l.name.c_str())) {
        const int w = renderer.getTextWidth(nativeFont, l.name.c_str());
        const int ny = y + (rowH - 4 - renderer.getLineHeight(nativeFont)) / 2;
        renderer.drawText(nativeFont, x + width - 6 - w, ny, l.name.c_str(), !sel);
      }
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
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
