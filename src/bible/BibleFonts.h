#pragma once

// Reader fonts. Five size slots (BIBLE_*_FONT_ID) that every Bible screen draws with.
// They point at the built-in Noto Serif, or at the current language's SD font family
// (/bible/fonts/<code>/<family>/<pt>.epdfont) with Noto Serif as glyph fallback.
// The UI font is the built-in Noto Sans, redirected to /bible/fonts/<code>/ui/ when the
// language has one. See docs/languages.md.

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

namespace BibleFonts {

constexpr int COUNT = 5;
constexpr int IDS[COUNT] = {BIBLE_14_FONT_ID, BIBLE_16_FONT_ID, BIBLE_18_FONT_ID, BIBLE_22_FONT_ID,
                            BIBLE_28_FONT_ID};
constexpr int BUILTIN_IDS[COUNT] = {NOTO_SERIF_14_FONT_ID, NOTO_SERIF_16_FONT_ID, NOTO_SERIF_18_FONT_ID,
                                    NOTO_SERIF_22_FONT_ID, NOTO_SERIF_28_FONT_ID};
constexpr int POINTS[COUNT] = {14, 16, 18, 22, 28};

inline int fontId(int sizeIndex) {
  if (sizeIndex < 0) sizeIndex = 0;
  if (sizeIndex >= COUNT) sizeIndex = COUNT - 1;
  return IDS[sizeIndex];
}

// Chrome on Bible screens (headers, page numbers, labels).
constexpr int LABEL_FONT_ID = BIBLE_14_FONT_ID;

// Registers the built-in Noto Serif families (one per size) at boot.
void registerBuiltIns(GfxRenderer& renderer, const EpdFontFamily* const families[COUNT]);

// Points the five reader slots and the UI font at the language's fonts. `family` picks one
// of lang->fontFamilies ("" or unknown: the first). Null or font-less language: built-in.
// Returns the family actually used ("" for built-in).
std::string apply(GfxRenderer& renderer, const LanguageInfo* lang, const std::string& family);

}  // namespace BibleFonts
