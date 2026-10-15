#include "BibleTextInputActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HangulCompose.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>

#include "MappedInputManager.h"
#include "bible/BibleFonts.h"
#include "components/UITheme.h"

namespace {
constexpr unsigned long CANCEL_HOLD_MS = 700;
constexpr int MARGIN = 12;
constexpr int KEY_GAP = 4;
}  // namespace

const std::vector<BibleTextInputActivity::Row>& BibleTextInputActivity::layout() const {
  const auto chars = [](const char32_t* cps, uint8_t units = 1) {
    Row r;
    for (const char32_t* p = cps; *p; p++) r.push_back({nullptr, static_cast<uint32_t>(*p), Action::Char, units});
    return r;
  };
  const auto bottom = [](bool hangul) {
    // Labels of these keys come from keyLabel() at draw time (translated).
    return Row{{hangul ? "" : "Aa", 0, Action::Shift, 2},
               {hangul ? "A/1" : "한글", 0, Action::Lang, 2},
               {"", ' ', Action::Space, 3},
               {"", 0, Action::Delete, 2},
               {"", 0, Action::Done, 2}};
  };
  // Without a font that draws Hangul (every language but Korean) there is no Hangul key.
  const auto bottomLatinOnly = []() {
    return Row{
        {"Aa", 0, Action::Shift, 2}, {"", ' ', Action::Space, 5}, {"", 0, Action::Delete, 2}, {"", 0, Action::Done, 2}};
  };
  static const std::vector<Row> hangul = {chars(U"1234567890"), chars(U"ㅂㅈㄷㄱㅅㅛㅕㅑㅐㅔ"),
                                          chars(U"ㅁㄴㅇㄹㅎㅗㅓㅏㅣ"), chars(U"ㅋㅌㅊㅍㅠㅜㅡ"), bottom(true)};
  static const std::vector<Row> hangulShift = {chars(U"1234567890"), chars(U"ㅃㅉㄸㄲㅆㅛㅕㅑㅒㅖ"),
                                               chars(U"ㅁㄴㅇㄹㅎㅗㅓㅏㅣ"), chars(U"ㅋㅌㅊㅍㅠㅜㅡ"), bottom(true)};
  static const std::vector<Row> latin = {chars(U"1234567890"), chars(U"qwertyuiop"), chars(U"asdfghjkl"),
                                         chars(U"zxcvbnm-"), bottom(false)};
  static const std::vector<Row> latinShift = {chars(U"1234567890"), chars(U"QWERTYUIOP"), chars(U"ASDFGHJKL"),
                                              chars(U"ZXCVBNM-"), bottom(false)};
  static const std::vector<Row> latinOnly = {chars(U"1234567890"), chars(U"qwertyuiop"), chars(U"asdfghjkl"),
                                             chars(U"zxcvbnm-"), bottomLatinOnly()};
  static const std::vector<Row> latinOnlyShift = {chars(U"1234567890"), chars(U"QWERTYUIOP"), chars(U"ASDFGHJKL"),
                                                  chars(U"ZXCVBNM-"), bottomLatinOnly()};
  if (hangulMode) return shift ? hangulShift : hangul;
  if (!hangulAvailable) return shift ? latinOnlyShift : latinOnly;
  return shift ? latinShift : latin;
}

const BibleTextInputActivity::Key& BibleTextInputActivity::selectedKey() const {
  const auto& rows = layout();
  const int r = std::clamp(row, 0, static_cast<int>(rows.size()) - 1);
  return rows[r][std::clamp(col, 0, static_cast<int>(rows[r].size()) - 1)];
}

void BibleTextInputActivity::onEnter() {
  Activity::onEnter();
  hangulAvailable = renderer.hasGlyphs(BibleFonts::IDS[3], "ㅂㅈㄷㄱ한글");
  hangulMode = hangulAvailable;
  shift = false;
  row = 1;
  col = 0;
  requestUpdate();
}

std::string BibleTextInputActivity::text() const { return hangul::compose(typed); }

void BibleTextInputActivity::moveColumn(int delta) {
  const auto& rows = layout();
  const int n = static_cast<int>(rows[row].size());
  col = (col + delta + n) % n;
  requestUpdate();
}

void BibleTextInputActivity::moveRow(int delta) {
  const auto& rows = layout();
  // Keep the horizontal position: pick the key whose centre is nearest in the new row.
  const auto centre = [](const Row& r, int index) {
    int total = 0, before = 0;
    for (int i = 0; i < static_cast<int>(r.size()); i++) {
      if (i < index) before += r[i].units;
      total += r[i].units;
    }
    return (before + r[index].units / 2.0f) / total;
  };
  const float x = centre(rows[row], col);
  row = (row + delta + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
  int best = 0;
  float bestDist = 2;
  for (int i = 0; i < static_cast<int>(rows[row].size()); i++) {
    const float d = std::fabs(centre(rows[row], i) - x);
    if (d < bestDist) {
      bestDist = d;
      best = i;
    }
  }
  col = best;
  requestUpdate();
}

void BibleTextInputActivity::press(const Key& key) {
  switch (key.action) {
    case Action::Char:
    case Action::Space: {
      std::vector<uint32_t> next = typed;
      next.push_back(key.cp);
      if (hangul::compose(next).size() <= maxBytes) typed = std::move(next);
      // Double consonants are a one-shot shift.
      if (hangulMode && shift && key.action == Action::Char) shift = false;
      break;
    }
    case Action::Shift:
      shift = !shift;
      break;
    case Action::Lang:
      if (!hangulAvailable) break;
      hangulMode = !hangulMode;
      shift = false;
      col = std::min<int>(col, static_cast<int>(layout()[row].size()) - 1);
      break;
    case Action::Delete:
      if (!typed.empty()) typed.pop_back();
      break;
    case Action::Done:
      finishWithText();
      return;
  }
  requestUpdate();
}

void BibleTextInputActivity::finishWithText() {
  setResult(TextResult{text()});
  finish();
}

void BibleTextInputActivity::cancel() {
  ActivityResult r;
  r.isCancelled = true;
  setResult(std::move(r));
  finish();
}

void BibleTextInputActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasLongPressed(Button::Back, CANCEL_HOLD_MS)) return cancel();
  if (mappedInput.wasReleased(Button::Back)) {
    if (typed.empty()) return cancel();
    typed.pop_back();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) return press(selectedKey());
  const bool swap = mappedInput.isNavDirectionSwapped();
  if (mappedInput.wasReleased(swap ? Button::Right : Button::Left)) return moveColumn(-1);
  if (mappedInput.wasReleased(swap ? Button::Left : Button::Right)) return moveColumn(1);
  if (mappedInput.wasReleased(Button::Up)) return moveRow(-1);
  if (mappedInput.wasReleased(Button::Down)) return moveRow(1);
}

void BibleTextInputActivity::render(RenderLock&&) {
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int x0 = left + MARGIN;
  const int width = screenW - left - right - 2 * MARGIN;
  const int hintsHeight = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int labelFont = BibleFonts::LABEL_FONT_ID;
  const int textFont = BibleFonts::IDS[3];
  const auto& rows = layout();
  const std::string current = text();

  const auto drawAll = [&]() {
    int y = top + 10;
    renderer.drawText(labelFont, x0, y, title.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(labelFont) + 6;

    // Text field: the end of the text when it is wider than the box, then a cursor.
    const int fieldH = renderer.getLineHeight(textFont) + 16;
    renderer.drawRoundedRect(x0, y, width, fieldH, 2, 8, true);
    std::string shown = current;
    while (!shown.empty() && renderer.getTextWidth(textFont, shown.c_str()) > width - 30) {
      size_t cut = 1;
      while (cut < shown.size() && (static_cast<unsigned char>(shown[cut]) & 0xC0) == 0x80) cut++;
      shown.erase(0, cut);
    }
    renderer.drawText(textFont, x0 + 10, y + 8, shown.c_str());
    const int cursorX = x0 + 12 + renderer.getTextWidth(textFont, shown.c_str());
    renderer.fillRect(cursorX, y + 10, 3, fieldH - 20, true);
    y += fieldH + 10;

    std::string mode = hangulMode ? "한글" : "ABC";
    if (shift) mode += std::string(" · ") + tr(STR_BIBLE_KEY_SHIFTED);
    renderer.drawText(labelFont, x0, y, mode.c_str());
    y += renderer.getLineHeight(labelFont) + 6;

    const int keysBottom = screenH - bottom - hintsHeight - 8;
    const int keyH = std::min(84, (keysBottom - y) / static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); r++) {
      int units = 0;
      for (const auto& k : rows[r]) units += k.units;
      // Rows narrower than ten units are centred, like a physical keyboard.
      const int unitW = width / std::max(units, 10);
      int kx = x0 + (width - unitW * units) / 2;
      const int ky = y + r * keyH;
      for (int c = 0; c < static_cast<int>(rows[r].size()); c++) {
        const Key& k = rows[r][c];
        const int kw = unitW * k.units;
        const bool sel = r == row && c == col;
        const bool latched = (k.action == Action::Shift && shift);
        if (sel) {
          renderer.fillRoundedRect(kx + KEY_GAP / 2, ky + KEY_GAP / 2, kw - KEY_GAP, keyH - KEY_GAP, 8, Color::Black);
        } else {
          renderer.drawRoundedRect(kx + KEY_GAP / 2, ky + KEY_GAP / 2, kw - KEY_GAP, keyH - KEY_GAP, latched ? 3 : 1, 8,
                                   true);
        }
        std::string label;
        if (k.label && *k.label) {
          label = k.label;
        } else if (k.label) {
          switch (k.action) {
            case Action::Shift:
              label = tr(STR_BIBLE_KEY_SHIFT);
              break;
            case Action::Space:
              label = tr(STR_BIBLE_KEY_SPACE);
              break;
            case Action::Delete:
              label = tr(STR_BIBLE_KEY_DELETE);
              break;
            default:
              label = tr(STR_BIBLE_KEY_DONE);
              break;
          }
        } else {
          hangul::appendUtf8(label, k.cp);
        }
        // Largest font that fits the key.
        int font = BibleFonts::IDS[0];
        for (int i = 3; i >= 0; i--) {
          const int id = BibleFonts::IDS[i];
          if (renderer.getTextWidth(id, label.c_str(), EpdFontFamily::BOLD) <= kw - 10 &&
              renderer.getLineHeight(id) <= keyH - 8) {
            font = id;
            break;
          }
        }
        const int tw = renderer.getTextWidth(font, label.c_str(), EpdFontFamily::BOLD);
        const int th = renderer.getLineHeight(font);
        renderer.drawText(font, kx + (kw - tw) / 2, ky + (keyH - th) / 2, label.c_str(), !sel, EpdFontFamily::BOLD);
        kx += kw;
      }
    }
  };

  renderer.clearScreen();
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  drawAll();
  scope.endScanAndPrewarm();
  drawAll();

  const auto hints = mappedInput.mapLabels(typed.empty() ? tr(STR_CANCEL) : tr(STR_BIBLE_HINT_DELETE), tr(STR_SELECT),
                                           tr(STR_BIBLE_HINT_PREV), tr(STR_BIBLE_HINT_NEXT));
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
