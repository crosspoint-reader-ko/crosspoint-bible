#include "BibleCover.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "bible/BibleFonts.h"
#include "images/BibleCover.h"

namespace {
// Title baseline area, in cover coordinates (below the foot of the cross at y 555).
constexpr int TITLE_TOP = 612;
}  // namespace

void drawBibleCover(const GfxRenderer& renderer, const char* status) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  renderer.fillRect(0, 0, screenW, screenH, true);

  int titleTop = screenH / 2 - renderer.getLineHeight(BibleFonts::IDS[3]);
  if (screenW >= BIBLE_COVER_WIDTH && screenH >= BIBLE_COVER_HEIGHT) {
    const int ox = (screenW - BIBLE_COVER_WIDTH) / 2;
    const int oy = (screenH - BIBLE_COVER_HEIGHT) / 2;
    constexpr int rowBytes = (BIBLE_COVER_WIDTH + 7) / 8;
    for (int y = 0; y < BIBLE_COVER_HEIGHT; y++) {
      const uint8_t* row = BibleCover + y * rowBytes;
      for (int xb = 0; xb < rowBytes; xb++) {
        const uint8_t bits = row[xb];
        if (!bits) continue;
        for (int k = 0; k < 8; k++) {
          if (bits & (0x80 >> k)) renderer.drawPixel(ox + xb * 8 + k, oy + y, false);
        }
      }
    }
    titleTop = oy + TITLE_TOP;
  }

  const int titleFont = BibleFonts::IDS[3];
  const int statusFont = BibleFonts::LABEL_FONT_ID;
  const char* title = tr(STR_CROSSPOINT);
  int w = renderer.getTextWidth(titleFont, title, EpdFontFamily::BOLD);
  renderer.drawText(titleFont, (screenW - w) / 2, titleTop, title, false, EpdFontFamily::BOLD);
  if (status && *status) {
    w = renderer.getTextWidth(statusFont, status);
    renderer.drawText(statusFont, (screenW - w) / 2, titleTop + renderer.getLineHeight(titleFont) + 14, status, false);
  }
}
