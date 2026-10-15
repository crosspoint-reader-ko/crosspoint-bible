#pragma once

// Lays one Bible chapter out into lines and pages. Pure C++ (text measuring is
// injected) so it runs in host tests; BibleReaderActivity draws the result.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "BiblePack.h"

namespace bible {

// A token starting with this byte is a verse number, drawn bold.
constexpr char VERSE_NUMBER_MARK = '\x01';

enum class LineKind : uint8_t {
  ChapterTitle,  // "3장" at the top of the chapter
  Heading,       // section title
  PsalmTitle,    // psalm superscription / psalter division
  Text,          // verse text
};

struct LayoutLine {
  std::string text;  // space-separated tokens
  LineKind kind = LineKind::Text;
  uint16_t verseAtStart = 0;  // verse showing at the start of the line (0 before verse 1)
  uint16_t lastVerse = 0;     // last verse touched by the line
  bool justify = false;       // not the last line of its block: may be stretched to the full width
  int16_t gapBefore = 0;      // extra pixels above the line
};

struct ChapterVerse {
  uint16_t number = 0;
  uint8_t flags = 0;
  std::string text;
};

struct ChapterContent {
  std::string title;  // e.g. "3장"
  std::vector<ChapterVerse> verses;
  std::vector<Heading> headings;  // verseIndex is relative to the chapter (0 = first verse)
};

struct LayoutStyle {
  int width = 0;           // text area width in px
  int height = 0;          // text area height in px
  int lineHeight = 0;      // text line advance
  int titleLineHeight = 0; // chapter title line advance
  int blockGap = 0;        // gap above a heading or a new paragraph
  bool versePerLine = true;
  // Break lines between any two characters (not only at spaces), for justified text.
  bool charWrap = false;
  // Width of a token; bold for verse numbers, headings and the title.
  std::function<int(const std::string& token, bool bold)> measure;
  // Width of a verse number (digits only); defaults to measure(digits, true).
  std::function<int(const std::string& digits)> measureNumber;
  int spaceWidth = 0;
};

class ChapterLayout {
 public:
  void build(const ChapterContent& content, const LayoutStyle& style);
  void clear();

  const std::vector<LayoutLine>& lines() const { return lines_; }
  int pageCount() const { return static_cast<int>(pageStarts.size()); }
  // Lines [first, end) of a page.
  int pageFirstLine(int page) const { return pageStarts[page]; }
  int pageEndLine(int page) const;
  // Page holding the start of verse `verse` (1-based), 0 if none.
  int pageOfVerse(int verse) const;
  // First and last verse shown on a page (0 if the page holds no verse).
  uint16_t pageFirstVerse(int page) const;
  uint16_t pageLastVerse(int page) const;
  int lineAdvance(const LayoutLine& line) const;

 private:
  void addBlock(std::vector<std::string>& tokens, std::vector<uint16_t>& tokenVerse, LineKind kind, int gapBefore);
  void paginate();

  LayoutStyle style;
  std::vector<LayoutLine> lines_;
  std::vector<int> pageStarts;
};

// Split a UTF-8 string into whitespace-separated tokens.
std::vector<std::string> splitWords(const std::string& text);

}  // namespace bible
