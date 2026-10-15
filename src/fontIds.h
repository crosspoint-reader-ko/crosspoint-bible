// Font IDs for CrossPoint Bible
#pragma once

// UI font (built-in Noto Sans 10pt) - single font for all UI sizes
#define UI_FONT_ID (1983644244)
#define SMALL_FONT_ID UI_FONT_ID
#define UI_10_FONT_ID UI_FONT_ID
#define UI_12_FONT_ID UI_FONT_ID

// The current language's system (UI) font from its SD bundle (/bible/fonts/<code>/ui/).
// When registered, UI_FONT_ID is redirected to it and Noto Sans backs it per glyph.
#define SYSTEM_FONT_ID (-888888)

// Former KoPub Batang 14pt reader font id; the Bible 14pt font keeps it (src/bible/BibleFonts.h).
#define KOPUB_14_FONT_ID (-1446433084)

// Reserved id, kept non-zero (asserted below).
#define UI_FALLBACK_FONT_ID (-777777)

// --- Upstream built-in reader font IDs (kept so upstream render/settings code compiles).
// The Korean build does not load these font objects; text in them falls back to Pretendard.
// UI_10/UI_12/SMALL are intentionally NOT redefined here — the Korean Pretendard IDs above win.
#define NOTOSERIF_12_FONT_ID (85340443)
#define NOTOSERIF_14_FONT_ID (-1367885987)
#define NOTOSERIF_16_FONT_ID (1428909134)
#define NOTOSERIF_18_FONT_ID (-501438527)
#define NOTOSANS_12_FONT_ID (2057568286)
#define NOTOSANS_14_FONT_ID (-1589315735)
#define NOTOSANS_16_FONT_ID (1669013660)
#define NOTOSANS_18_FONT_ID (37077304)

// Font ID 0 is reserved as the "not found" sentinel. Guard against any hash producing 0.
static_assert(NOTOSERIF_12_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSERIF_14_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSERIF_16_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSERIF_18_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSANS_12_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSANS_14_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSANS_16_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSANS_18_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(UI_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(UI_FALLBACK_FONT_ID != 0, "Font ID collision with sentinel");
