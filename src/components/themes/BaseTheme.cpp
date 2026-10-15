#include "BaseTheme.h"

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "I18n.h"
#include "components/HeaderBackTapTarget.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "components/icons/headerIcons.h"
#include "fontIds.h"

// Internal constants
void BaseTheme::drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight) {
  // Top line
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  // Bottom line
  renderer.drawLine(x + 1, y + rectHeight - 1, x + battWidth - 3, y + rectHeight - 1);
  // Left line
  renderer.drawLine(x, y + 1, x, y + rectHeight - 2);
  // Battery end
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rectHeight - 2);
  renderer.drawPixel(x + battWidth - 1, y + 3);
  renderer.drawPixel(x + battWidth - 1, y + rectHeight - 4);
  renderer.drawLine(x + battWidth - 0, y + 4, x + battWidth - 0, y + rectHeight - 5);
}

void BaseTheme::drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY) {
  // Draw lightning bolt (white/inverted on black fill for visibility)
  renderer.drawLine(boltX + 4, boltY + 0, boltX + 5, boltY + 0, false);
  renderer.drawLine(boltX + 3, boltY + 1, boltX + 4, boltY + 1, false);
  renderer.drawLine(boltX + 2, boltY + 2, boltX + 5, boltY + 2, false);
  renderer.drawLine(boltX + 3, boltY + 3, boltX + 4, boltY + 3, false);
  renderer.drawLine(boltX + 2, boltY + 4, boltX + 3, boltY + 4, false);
  renderer.drawLine(boltX + 1, boltY + 5, boltX + 4, boltY + 5, false);
  renderer.drawLine(boltX + 2, boltY + 6, boltX + 3, boltY + 6, false);
  renderer.drawLine(boltX + 1, boltY + 7, boltX + 2, boltY + 7, false);
}

void BaseTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  const bool charging = gpio.isUsbConnected();

  const int maxFillWidth = rect.width - 5;
  const int fillHeight = rect.height - 4;
  if (maxFillWidth <= 0 || fillHeight <= 0) {
    return;
  }
  // +1 to round up so we always fill at least one pixel
  int filledWidth = percentage * maxFillWidth / 100 + 1;
  if (filledWidth > maxFillWidth) {
    filledWidth = maxFillWidth;
  }

  // When charging, ensure minimum fill so lightning bolt is fully visible
  constexpr int minFillForBolt = 8;
  if (charging && filledWidth < minFillForBolt) {
    filledWidth = std::min(minFillForBolt, maxFillWidth);
  }

  renderer.fillRect(rect.x + 2, rect.y + 2, filledWidth, fillHeight);

  if (charging) {
    drawBatteryLightningBolt(renderer, rect.x + 4, rect.y + 2);
  }
}

void BaseTheme::drawBatteryLeft(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Left aligned: icon on left, percentage on right (reader mode)
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const int y = rect.y + 6;

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    renderer.drawText(SMALL_FONT_ID, rect.x + batteryPercentSpacing + rect.width, rect.y, percentageText.c_str());
  }

  const Rect iconRect{rect.x, y, rect.width, rect.height};
  drawBatteryOutline(renderer, rect.x, y, rect.width, rect.height);
  fillBatteryIcon(renderer, iconRect, percentage);
}

void BaseTheme::drawProgressBar(const GfxRenderer& renderer, Rect rect, const size_t current, const size_t total) {
  if (total == 0) {
    return;
  }

  // Use 64-bit arithmetic to avoid overflow for large files
  const int percent = static_cast<int>((static_cast<uint64_t>(current) * 100) / total);

  LOG_DBG("UI", "Drawing progress bar: current=%u, total=%u, percent=%d", current, total, percent);
  // Draw outline
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  // Draw filled portion
  const int fillWidth = (rect.width - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, rect.height - 4);
  }

  // Draw percentage text centered below bar
  const std::string percentText = std::to_string(percent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, rect.y + rect.height + 15, percentText.c_str());
}

// Centre a button-hint label inside its box. A label that fits is drawn on the
// single baseline it always was; one too wide used to overflow the button border
// and run into the neighbouring hint, and now wraps to at most two centred lines
// (wrappedText() ellipsises anything that still doesn't fit). Shared so every
// theme's drawButtonHints() gets the same behaviour.
void BaseTheme::drawHintLabel(const GfxRenderer& renderer, const int fontId, const char* label, const int x,
                              const int boxWidth, const int boxTop, const int boxHeight, const int singleLineYOffset) {
  constexpr int textPadding = 4;  // keeps a wrapped label off the button's border
  const int maxTextWidth = boxWidth - (textPadding * 2);

  const int textWidth = renderer.getTextWidth(fontId, label);
  if (textWidth <= maxTextWidth) {
    renderer.drawText(fontId, x + (boxWidth - 1 - textWidth) / 2, boxTop + singleLineYOffset, label);
    return;
  }

  // Spaced by the glyph height, not getLineHeight() — that returns the font's
  // full advanceY (leading included), which stacks two lines taller than the
  // button and clips the second one.
  constexpr int lineGap = 2;
  const int step = renderer.getTextHeight(fontId) + lineGap;
  const auto lines = renderer.wrappedText(fontId, label, maxTextWidth, 2);
  const int block = static_cast<int>(lines.size()) * step - lineGap;
  int lineY = boxTop + std::max(1, (boxHeight - block) / 2);
  for (const auto& line : lines) {
    const int lineWidth = renderer.getTextWidth(fontId, line.c_str());
    renderer.drawText(fontId, x + (boxWidth - 1 - lineWidth) / 2, lineY, line.c_str());
    lineY += step;
  }
}

void BaseTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = BaseMetrics::values.buttonHintsHeight;
  constexpr int buttonY = BaseMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  // Keyed to the portrait panel width: the 528-wide X3 gets more spacing than
  // the 480-wide boards (X4, X4 Pro, and the other 800x480 panels).
  constexpr int narrowButtonPositions[] = {25, 130, 245, 350};
  constexpr int wideButtonPositions[] = {38, 154, 268, 384};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool grayscale = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();

  for (int i = 0; i < 4; i++) {
    // Only draw if the label is non-empty
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int x = buttonPositions[i];
      // Zero gray-plane bits leave the monochrome hint from the base pass intact.
      renderer.fillRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, grayscale);
      if (grayscale) continue;
      renderer.drawRect(x, pageHeight - buttonY, buttonWidth, buttonHeight);
      drawHintLabel(renderer, UI_10_FONT_ID, labels[i], x, buttonWidth, pageHeight - buttonY, buttonHeight,
                    textYOffset);
    }
  }

  renderer.setOrientation(orig_orientation);
}

// Slightly inside the side padding: the battery's boxed glyph and the clock
// digits read wider than text/cover ink on the same line, so flush placement
// looks like it overhangs the content columns.
int BaseTheme::headerStatusInset() { return UITheme::getInstance().getMetrics().headerSidePadding + 4; }

void BaseTheme::applyHeaderStatus(const GfxRenderer& renderer, freeink::ui::HeaderProps& props) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  auto& status = props.status;

  // Status text stays at the fixed small font on every screen: FONT_LABEL is
  // bound to SMALL_FONT_ID by makeUiTarget() (FUI screens) and drawHeader()
  // (passive frames), while the uiScale FONT_SMALL is for list subtitles.
  status.battery.text.font = freeink::ui::GfxRendererTarget::FONT_LABEL;

  status.showBattery = true;
  const uint16_t percentage = powerManager.getBatteryPercentage();
  status.battery.percent = static_cast<uint8_t>(percentage > 100 ? 100 : percentage);
  status.battery.charging = gpio.isUsbConnected();
  // Static label buffers: headers draw on the single render task, and the
  // strings only need to outlive the fui::header() call that consumes them.
  static char percentText[8];
  if (SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS) {
    snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(percentage));
    status.battery.label = percentText;
  }
  status.battery.glyphWidth = static_cast<int16_t>(metrics.batteryWidth);
  status.battery.glyphHeight = static_cast<int16_t>(metrics.batteryHeight);
  status.battery.gap = batteryPercentSpacing;
  status.batteryLeft = metrics.headerBatterySide == 1;
  status.edgeInset = static_cast<int16_t>(headerStatusInset());

  // Header chrome geometry. Status lives on the theme's thin top strip in
  // fixed corners — battery top-right, a corner clock (headerClockCentered =
  // false) top-left, a centered clock top-center — and never repositions.
  // The content row (title, back arrow, trailing action buttons) centers on
  // the region between the strip and the band bottom, so its padding reads
  // as balanced under the status line rather than against the full band.
  // Text hangs low in its line cell by the font's internal leading; the icon
  // buttons drop by that amount to align with the glyphs the user sees.
  // Buttons keep a standard square (a band-8 button dwarfs its 24px icon);
  // the boxes are invisible and minTouchSize pads the tap target.
  constexpr int16_t headerButtonSize = 48;
  const int16_t bandHeight = static_cast<int16_t>(metrics.headerHeight);
  const int16_t strip = static_cast<int16_t>(metrics.batteryBarHeight);
  const int titleFontId = uiScaleSpec().titleFontId;
  const int16_t opticalDrop =
      static_cast<int16_t>((renderer.getLineHeight(titleFontId) - renderer.getTextHeight(titleFontId)) / 2);
  props.leadingSize = headerButtonSize;
  props.trailingSize = headerButtonSize;
  props.actionOffsetY = static_cast<int16_t>(strip + (bandHeight - strip - headerButtonSize) / 2 - 4 + opticalDrop);
  // Shift the title's band-centered box down by half the strip: its center
  // lands on the below-strip region's midline with the buttons.
  props.titleOffsetY = static_cast<int16_t>(strip / 2);
  status.stripHeight = strip;
  status.clockCentered = metrics.headerClockCentered;

  // Header clock, opposite the battery, on every screen that draws this band
  // (SETTINGS.clockShowInHeader). Themes whose title layout has no room for
  // the clock's left reserve opt out via headerShowsClock.
  static char clockText[10];
  if (metrics.headerShowsClock && SETTINGS.clockShowInHeader && halClock.isAvailable() &&
      halClock.formatTime(clockText, sizeof(clockText), SETTINGS.clockFormat == 1)) {
    status.clockText = clockText;
  }
}

void BaseTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                           const bool backButton) const {
  // Every activity header renders through the FreeInkUI header + battery
  // indicator components, styled by the active theme's tokens (padding,
  // centering, underline). Non-interactive frame: no hit rects registered.
  namespace fui = freeink::ui;
  const auto spec = uiScaleSpec();
  fui::GfxRendererFrame<1> ui(renderer, spec.smallFontId, spec.bodyFontId, spec.titleFontId);
  // Refresh the app-wide shared tokens instead of copying ~1.5KB of
  // ThemeTokens onto this render-path stack frame; the values derived here
  // are identical to what every FreeInkApp screen derives. Goes through the
  // same publish-a-fresh-slot path applySharedUiTheme() uses (see
  // UiAppHelpers.h) rather than overwriting the previously-published
  // instance in place, since some other FreeInkApp could be mid-read of it.
  const fui::ThemeTokens& tokens = refreshSharedUiThemeTokens(ui.target);
  // Header status text (battery percent, right label) stays at the fixed
  // small font like the legacy headers; the uiScale small font is for list
  // subtitles.
  ui.target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  ui.target.setFont(fui::GfxRendererTarget::FONT_LABEL, SMALL_FONT_ID);
  const fui::Rect band{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
                       static_cast<int16_t>(rect.height)};

  fui::HeaderProps props;
  props.title = title;
  props.rightLabel = subtitle;  // firmware headers right-align the secondary text
  // Battery + clock chrome and their title reserves live in the FreeInkUI
  // header component; this only fills the values from settings and metrics.
  applyHeaderStatus(renderer, props);
  if (rect.height < UITheme::getInstance().getMetrics().headerHeight) {
    // Short bands (home) are not split into strip + content row: the title
    // centers on the band, clear of the band's bottom edge.
    props.titleOffsetY = 0;
  }
  // Tappable back button leading the band on touch boards, so every pushed
  // screen offers a visible way out beside the edge-swipe gesture. This frame
  // registers no hit rects, so the rect is recorded in HeaderBackTapTarget and
  // MappedInputManager folds taps on it into Button::Back. Same geometry as
  // the FUI header's leading slot (applyHeaderStatus set the size/offset) so
  // the recorded rect matches the drawn button.
  const int16_t backBtnSize = props.leadingSize;
  const bool showBackButton = backButton && title != nullptr && gpio.hasTouch();
  if (showBackButton) {
    props.leadingIcon = fui::bitmapFromIcon(icon_header_back_32);
    props.leadingAction = 1;  // any non-NO_ACTION id: paints the button, routing is via HeaderBackTapTarget
    HeaderBackTapTarget::set(band.x + 4, band.y + 4 + props.actionOffsetY, backBtnSize, backBtnSize);
  } else {
    HeaderBackTapTarget::clear();
  }
  props.borderEdges = fui::EdgeBottom;
  props.titleText = tokens.titleText;
  props.titleText.align = tokens.headerTitleAlign;
  props.subtitleText = tokens.smallText;
  props.styles = tokens.popup;
  props.sidePadding = tokens.headerSidePadding;
  // Underline only under a titled header: an untitled band (Lyra home screen)
  // historically drew no rule, and the old themes keyed the line on the title.
  if (title != nullptr && props.styles.normal.border.kind == fui::PaintKind::None && tokens.headerUnderline > 0) {
    props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    props.styles.normal.borderWidth = tokens.headerUnderline;
  }
  fui::header(ui.frame, band, props);
}

void BaseTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  constexpr int labelGap = 10;
  const int contentWidth = std::max(0, rect.width - BaseMetrics::values.contentSidePadding * 2);

  int labelWidth = contentWidth;
  if (rightLabel) {
    auto truncatedRightLabel = renderer.truncatedText(SMALL_FONT_ID, rightLabel, contentWidth, EpdFontFamily::REGULAR);
    const int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - BaseMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    labelWidth = std::max(0, contentWidth - rightLabelWidth - labelGap);
  }

  if (labelWidth > 0) {
    auto truncatedLabel = renderer.truncatedText(UI_12_FONT_ID, label, labelWidth, EpdFontFamily::REGULAR);
    renderer.drawText(UI_12_FONT_ID, rect.x + BaseMetrics::values.contentSidePadding, rect.y, truncatedLabel.c_str(),
                      true, EpdFontFamily::REGULAR);
  }
}

Rect BaseTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int marginX = metrics.popupMarginX;
  const int marginY = metrics.popupMarginY;
  const int frameThickness = metrics.popupFrameThickness;
  const EpdFontFamily::Style popupFontFamily = metrics.popupTextBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  // Scale y position proportionally to screen height
  const int y = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, popupFontFamily);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + marginX * 2;
  const int h = textHeight + marginY * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  const bool useRoundedPopup = metrics.popupCornerRadius > 0;
  if (useRoundedPopup) {
    renderer.fillRoundedRect(x - frameThickness, y - frameThickness, w + frameThickness * 2, h + frameThickness * 2,
                             metrics.popupCornerRadius + frameThickness, Color::White);
    renderer.fillRoundedRect(x, y, w, h, metrics.popupCornerRadius, Color::Black);
  } else {
    renderer.fillRect(x - frameThickness, y - frameThickness, w + frameThickness * 2, h + frameThickness * 2, true);
    renderer.fillRect(x, y, w, h, false);
  }

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + marginY + metrics.popupTextBaselineOffsetY;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, metrics.popupTextInverted, popupFontFamily);
  renderer.displayBuffer();
  return Rect{x, y, w, h};
}

void BaseTheme::drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  auto truncatedLabel =
      renderer.truncatedText(SMALL_FONT_ID, label, rect.width - metrics.contentSidePadding * 2, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, rect.y, truncatedLabel.c_str());
}
