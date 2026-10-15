#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "HomeButtonSettings.h"
#include "activities/settings/SettingsActivity.h"
#include "bible/BibleSession.h"
#include "components/UITheme.h"

// Sleep screens offered in CrossPoint Bible, in menu order. The setting persists the
// CrossPointSettings::SLEEP_SCREEN_MODE value; the custom-image and book-cover modes keep
// their numbers but are no longer offered (CrossPointSettings::loadFromFile maps them to Dark).
inline constexpr uint8_t SLEEP_SCREEN_MENU[] = {CrossPointSettings::DARK, CrossPointSettings::LIGHT,
                                                CrossPointSettings::BLANK, CrossPointSettings::QUICK_RESUME};

inline SettingInfo buildSleepScreenSetting() {
  SettingInfo s = SettingInfo::DynamicEnum(
      StrId::STR_SLEEP_SCREEN,
      {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_NONE_OPT, StrId::STR_QUICK_RESUME},
      [] {
        const auto it = std::find(std::begin(SLEEP_SCREEN_MENU), std::end(SLEEP_SCREEN_MENU), SETTINGS.sleepScreen);
        return static_cast<uint8_t>(it == std::end(SLEEP_SCREEN_MENU) ? 0 : it - std::begin(SLEEP_SCREEN_MENU));
      },
      [](const uint8_t v) {
        SETTINGS.sleepScreen = v < std::size(SLEEP_SCREEN_MENU) ? SLEEP_SCREEN_MENU[v] : CrossPointSettings::DARK;
      },
      "sleepScreen", StrId::STR_CAT_DISPLAY);
  return s;
}

// Shared settings list: the on-device Settings screen shows the entries that have a
// category, and CrossPointSettings persists every entry with a key.
//
// The static list is constructed exactly once so the per-entry SettingInfo cost is
// paid once; every call then copies it and applies the board filters.
inline std::vector<SettingInfo> getSettingsList() {
  static const std::vector<SettingInfo> baseList = [] {
    std::vector<SettingInfo> v = {
        // --- Display ---
        buildSleepScreenSetting(),
        SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                          {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
                          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                           StrId::STR_PAGES_30, StrId::STR_NEVER},
                          "refreshFrequency", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                            StrId::STR_CAT_DISPLAY),
#if FREEINK_CAP_FRONTLIGHT
        SettingInfo::Toggle(StrId::STR_RESTORE_LIGHT_ON_WAKE, &CrossPointSettings::frontlightRestoreOnWake,
                            "frontlightRestoreOnWake", StrId::STR_CAT_DISPLAY),
#endif
        // Night mode = inverted output polarity everywhere (ActivityManager
        // applies it to every activity), so it lives in the Display category.
        SettingInfo::Toggle(StrId::STR_NIGHT_MODE, &CrossPointSettings::screenInverted, "screenInverted",
                            StrId::STR_CAT_DISPLAY),

        // --- Reader ---
        // Font size, line spacing, layout and headings live in the Bible reader menu
        // (BibleSession state), not here.
        SettingInfo::Enum(
            StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
            {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW},
            "orientation", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                            StrId::STR_CAT_READER),
        // Bible reader preference (BibleSession saves it, not settings.json).
        SettingInfo::DynamicEnum(
            StrId::STR_BIBLE_ALIGN, {StrId::STR_BIBLE_ALIGN_LEFT, StrId::STR_BIBLE_ALIGN_JUSTIFY},
            [] { return BIBLE.prefs.justify; },
            [](const uint8_t v) {
              BIBLE.prefs.justify = v ? 1 : 0;
              BIBLE.saveState();
            },
            nullptr, StrId::STR_CAT_READER),

        // --- Controls ---
        SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                          {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED, StrId::STR_NEXT_NEXT,
                           StrId::STR_PREV_PREV},
                          "sideButtonLayout", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, &CrossPointSettings::frontButtonFollowOrientation,
                            "frontButtonFollowOrientation", StrId::STR_CAT_CONTROLS),
        // Erased below unless the board is an X4 Pro.
        SettingInfo::Toggle(StrId::STR_DBL_CLICK_PWR_LIGHT, &CrossPointSettings::doubleClickPwrLight,
                            "doubleClickPwrLight", StrId::STR_CAT_CONTROLS),
    // Short power press is not offered: it always does nothing in CrossPoint Bible
    // (the reader pages with the front and side buttons), see loadFromFile.

        // --- System ---
        SettingInfo::Value(
            StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
            {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
            "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM),

        // Frontlight quick-panel state: persisted, but hidden from the on-device
        // Settings screen because the swipe panel owns it.
        SettingInfo::Value(StrId::STR_BRIGHTNESS, &CrossPointSettings::frontlightBrightness, {0, 100, 5},
                           "frontlightBrightness"),
#if FREEINK_CAP_WARMLIGHT
        SettingInfo::Value(StrId::STR_WARMTH, &CrossPointSettings::frontlightWarmth, {0, 100, 5}, "frontlightWarmth"),
#endif
        SettingInfo::Toggle(StrId::STR_FRONTLIGHT, &CrossPointSettings::frontlightOn, "frontlightOn"),

        // Clock entries: persisted, category-less (the device UI is
        // ClockSettingsActivity under System settings).
        // LEGACY: retired quarter-hour UTC offset (biased by 48). Persisted so
        // timezones::activeIndex() can migrate it into clockTimezone.
        SettingInfo::Value(StrId::STR_CLOCK, &CrossPointSettings::clockUtcOffsetQ, {0, 104, 1}, "clockUtcOffsetQ"),
        SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                          {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat"),
        // Index into the append-only table in src/util/Timezones.cpp; 255 = unset.
        SettingInfo::Value(StrId::STR_TIMEZONE, &CrossPointSettings::clockTimezone, {0, 255, 1}, "clockTimezone"),
        SettingInfo::Enum(StrId::STR_CLOCK_DST, &CrossPointSettings::clockDst,
                          {StrId::STR_CLOCK_DST_AUTO, StrId::STR_STATE_ON, StrId::STR_STATE_OFF}, "clockDst"),
        SettingInfo::Toggle(StrId::STR_CLOCK_IN_HEADER, &CrossPointSettings::clockShowInHeader, "clockShowHeader"),
        // Set once the RTC has been set (by an earlier firmware's NTP sync); the
        // clock settings screen shows "Not Set" until then.
        SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &CrossPointSettings::clockHasBeenSynced, "clockHasBeenSynced"),
    };
    // Erasing keeps the list at its initial allocation; inserting into a full
    // vector would reallocate it at double capacity for the process lifetime.
    const auto eraseEntry = [&v](const StrId nameId) {
      v.erase(std::find_if(v.begin(), v.end(), [nameId](const SettingInfo& s) { return s.nameId == nameId; }));
    };
    // Double-click power frontlight shortcut only exists on the X4 Pro.
    if (!BoardConfig::isX4Pro()) eraseEntry(StrId::STR_DBL_CLICK_PWR_LIGHT);
    return v;
  }();

  std::vector<SettingInfo> v = baseList;

  if (BoardConfig::hasHomeKey()) {
    v.reserve(v.size() + 3);
    for (unsigned i = 0; i < 3; ++i) {
      v.push_back(SettingInfo::StaticEnum(home_button::GESTURE_LABELS[i], home_button::FIELDS[i],
                                          home_button::ACTION_LABELS, home_button::KEYS[i], StrId::STR_CAT_CONTROLS));
    }
  }
  if (BoardConfig::hasTouch()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return s.nameId == StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION ||
                                    s.nameId == StrId::STR_SUNLIGHT_FADING_FIX;
                           }),
            v.end());
  }
  return v;
}
