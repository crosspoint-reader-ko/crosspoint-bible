#include "BibleMenuActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "bible/BibleFonts.h"
#include "bible/BibleSession.h"
#include "components/UITheme.h"

namespace {
constexpr int MARGIN = 16;
}  // namespace

void BibleMenuActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  firstRender = true;
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
    case GOTO: {
      MenuResult r;
      r.action = ACTION_GOTO;
      setResult(r);
      finish();
      return;
    }
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
      firstRender = true;
      break;
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
    activate(selected);
    return;
  }
  buttonNavigator.onNextRelease([this] {
    selected = ButtonNavigator::nextIndex(selected, ROW_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selected = ButtonNavigator::previousIndex(selected, ROW_COUNT);
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
      StrId::STR_BIBLE_GOTO,        StrId::STR_BIBLE_FONT_SIZE, StrId::STR_BIBLE_LINE_SPACING,
      StrId::STR_BIBLE_LAYOUT,      StrId::STR_BIBLE_HEADINGS,  StrId::STR_ORIENTATION,
      StrId::STR_SETTINGS_TITLE,
  };

  const int titleFont = BibleFonts::IDS[3];
  const int rowFont = BibleFonts::IDS[2];
  const int titleH = renderer.getLineHeight(titleFont) + 16;
  const int available = screenH - top - bottom - hintsHeight - titleH - 10;
  const int rowH = std::min(renderer.getLineHeight(rowFont) + 28, available / ROW_COUNT);

  const auto drawAll = [&]() {
    int y = top + 10;
    renderer.drawText(titleFont, x, y, tr(STR_BIBLE_MENU), true, EpdFontFamily::BOLD);
    y += titleH;
    for (int i = 0; i < ROW_COUNT; i++) {
      const bool sel = i == selected;
      if (sel) renderer.fillRoundedRect(x - 6, y, width + 12, rowH - 4, 8, Color::Black);
      const int ty = y + (rowH - 4 - renderer.getLineHeight(rowFont)) / 2;
      renderer.drawText(rowFont, x + 6, ty, I18N.get(labels[i]), !sel, EpdFontFamily::BOLD);
      const std::string value = valueOf(i);
      if (!value.empty()) {
        const int w = renderer.getTextWidth(rowFont, value.c_str());
        renderer.drawText(rowFont, x + width - 6 - w, ty, value.c_str(), !sel);
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
  renderer.displayBuffer(firstRender ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstRender = false;
}
