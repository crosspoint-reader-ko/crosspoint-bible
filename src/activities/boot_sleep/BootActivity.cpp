#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "bible/BibleCover.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  (void)pageWidth;
  drawBibleCover(renderer, tr(STR_BOOTING));
  // Version in white, inside the cover's border rule.
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 52, CROSSPOINT_VERSION, false);
  renderer.displayBuffer();
}
