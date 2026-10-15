#pragma once

// Reader fonts: five size slots (BIBLE_*_FONT_ID) for the Bible text. They point at the
// built-in Noto Serif, or at the current language's SD font family
// (/bible/fonts/<code>/<family>/<pt>.epdfont) with Noto Serif as glyph fallback.
// Sans-serif UI: three size slots (BIBLE_UI_*_FONT_ID) for menus, lists and chrome, on the
// built-in Noto Sans or the language's system font (/bible/fonts/<code>/ui/<pt>.epdfont).
// Settings uses UI_FONT_ID (10 pt), redirected the same way. See docs/languages.md.

#include <string>

class GfxRenderer;
class EpdFontFamily;
struct LanguageInfo;

#define BIBLE_14_FONT_ID (-1446433084)
#define BIBLE_16_FONT_ID (-1446433016)
#define BIBLE_18_FONT_ID (-1446433018)
#define BIBLE_22_FONT_ID (-1446433022)
#define BIBLE_28_FONT_ID (-1446433028)

// Built-in Noto Serif, registered once at boot; the slots above fall back to these.
#define NOTO_SERIF_14_FONT_ID (-1446433114)
#define NOTO_SERIF_16_FONT_ID (-1446433116)
#define NOTO_SERIF_18_FONT_ID (-1446433118)
#define NOTO_SERIF_22_FONT_ID (-1446433122)
#define NOTO_SERIF_28_FONT_ID (-1446433128)

#define BIBLE_UI_14_FONT_ID (-1446433214)
#define BIBLE_UI_16_FONT_ID (-1446433216)
#define BIBLE_UI_22_FONT_ID (-1446433222)

// Built-in Noto Sans; the UI slots fall back to these.
#define NOTO_SANS_14_FONT_ID (-1446433314)
#define NOTO_SANS_16_FONT_ID (-1446433316)
#define NOTO_SANS_22_FONT_ID (-1446433322)

namespace BibleFonts {

constexpr int COUNT = 5;
constexpr int IDS[COUNT] = {BIBLE_14_FONT_ID, BIBLE_16_FONT_ID, BIBLE_18_FONT_ID, BIBLE_22_FONT_ID, BIBLE_28_FONT_ID};
constexpr int BUILTIN_IDS[COUNT] = {NOTO_SERIF_14_FONT_ID, NOTO_SERIF_16_FONT_ID, NOTO_SERIF_18_FONT_ID,
                                    NOTO_SERIF_22_FONT_ID, NOTO_SERIF_28_FONT_ID};
constexpr int POINTS[COUNT] = {14, 16, 18, 22, 28};

inline int fontId(int sizeIndex) {
  if (sizeIndex < 0) sizeIndex = 0;
  if (sizeIndex >= COUNT) sizeIndex = COUNT - 1;
  return IDS[sizeIndex];
}

constexpr int UI_COUNT = 3;
constexpr int UI_IDS[UI_COUNT] = {BIBLE_UI_14_FONT_ID, BIBLE_UI_16_FONT_ID, BIBLE_UI_22_FONT_ID};
constexpr int UI_BUILTIN_IDS[UI_COUNT] = {NOTO_SANS_14_FONT_ID, NOTO_SANS_16_FONT_ID, NOTO_SANS_22_FONT_ID};
constexpr int UI_POINTS[UI_COUNT] = {14, 16, 22};

// Sans-serif fonts of Bible screens: chrome and small labels, list rows, titles.
constexpr int LABEL_FONT_ID = BIBLE_UI_14_FONT_ID;
constexpr int ROW_FONT_ID = BIBLE_UI_16_FONT_ID;
constexpr int TITLE_FONT_ID = BIBLE_UI_22_FONT_ID;

// Registers the built-in Noto Serif (reader) and Noto Sans (UI) families at boot.
void registerBuiltIns(GfxRenderer& renderer, const EpdFontFamily* const serif[COUNT],
                      const EpdFontFamily* const sans[UI_COUNT]);

// Points the reader slots, the UI slots and the Settings font at the language's fonts. `family` picks one
// of lang->fontFamilies ("" or unknown: the first). Null or font-less language: built-in.
// Returns the family actually used ("" for built-in).
std::string apply(GfxRenderer& renderer, const LanguageInfo* lang, const std::string& family);

}  // namespace BibleFonts
