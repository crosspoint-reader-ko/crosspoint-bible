#include "LyraTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "components/UITheme.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
}  // namespace

void LyraTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  const bool charging = gpio.isUsbConnected();

  if (charging) {
    // Solid fill when charging so lightning bolt is visible
    renderer.fillRect(rect.x + 2, rect.y + 2, rect.width - 5, rect.height - 4);
    drawBatteryLightningBolt(renderer, rect.x + 4, rect.y + 2);
  } else {
    if (percentage > 10) {
      renderer.fillRect(rect.x + 2, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 40) {
      renderer.fillRect(rect.x + 6, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 70) {
      renderer.fillRect(rect.x + 10, rect.y + 2, 3, rect.height - 4);
    }
  }
}

void LyraTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  const int contentWidth = std::max(0, rect.width - LyraMetrics::values.contentSidePadding * 2);

  int labelWidth = contentWidth;
  if (rightLabel) {
    auto truncatedRightLabel = renderer.truncatedText(SMALL_FONT_ID, rightLabel, contentWidth, EpdFontFamily::REGULAR);
    const int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - LyraMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    labelWidth = std::max(0, contentWidth - rightLabelWidth - hPaddingInSelection);
  }

  if (labelWidth > 0) {
    auto truncatedLabel = renderer.truncatedText(UI_10_FONT_ID, label, labelWidth, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding, rect.y + 6,
                      truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void LyraTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LyraMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  // Keyed to the portrait panel width: the 528-wide X3 gets more spacing than
  // the 480-wide boards (X4, X4 Pro, and the other 800x480 panels).
  constexpr int narrowButtonPositions[] = {58, 146, 254, 342};
  constexpr int wideButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool grayscale = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      // Draw the filled background and border for a FULL-sized button
      renderer.fillRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, cornerRadius,
                               grayscale ? Color::Black : Color::White);
      if (grayscale) continue;
      renderer.drawRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false,
                               false, true);
      drawHintLabel(renderer, SMALL_FONT_ID, labels[i], x, buttonWidth, pageHeight - buttonY, buttonHeight,
                    textYOffset);
    } else {
      // Draw the filled background and border for a SMALL-sized button
      renderer.fillRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, cornerRadius,
                               grayscale ? Color::Black : Color::White);
      if (grayscale) continue;
      renderer.drawRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, 1, cornerRadius, true,
                               true, false, false, true);
    }
  }

  renderer.setOrientation(orig_orientation);
}
