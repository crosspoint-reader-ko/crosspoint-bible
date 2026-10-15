#include "BibleLayout.h"

#include <algorithm>

namespace bible {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

size_t utf8CharLen(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c >> 5) == 0x6) return 2;
  if ((c >> 4) == 0xE) return 3;
  if ((c >> 3) == 0x1E) return 4;
  return 1;
}

bool isVerseNumber(const std::string& token) { return !token.empty() && token[0] == VERSE_NUMBER_MARK; }

// Punctuation that must not end a line (kinsoku): opening quotes and brackets.
bool isOpeningPunct(const std::string& s, size_t at) {
  if (at >= s.size()) return false;
  const char c = s[at];
  if (c == '(' || c == '[' || c == '{') return true;
  // U+2018 ‘, U+201C “, U+300C 「, U+300E 『, U+FF08 （, U+3008 〈, U+300A 《
  static const char* const wide[] = {"\xE2\x80\x98", "\xE2\x80\x9C", "\xE3\x80\x8C", "\xE3\x80\x8E",
                                     "\xEF\xBC\x88", "\xE3\x80\x88", "\xE3\x80\x8A"};
  for (const char* w : wide) {
    if (s.compare(at, 3, w) == 0) return true;
  }
  return false;
}

// Punctuation that must not start a line (kinsoku).
bool isClosingPunct(const std::string& s, size_t at) {
  if (at >= s.size()) return false;
  const char c = s[at];
  if (c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' || c == ')' || c == ']' || c == '}' ||
      c == '\'' || c == '"') {
    return true;
  }
  // U+2019 ’, U+201D ”, U+3001 、, U+3002 。, U+300D 」, U+300F 』
  static const char* const wide[] = {"\xE2\x80\x99", "\xE2\x80\x9D", "\xE3\x80\x81",
                                     "\xE3\x80\x82", "\xE3\x80\x8D", "\xE3\x80\x8F"};
  for (const char* w : wide) {
    if (s.compare(at, 3, w) == 0) return true;
  }
  return false;
}

// Valid hyphenation points of `word`: inside the word, on UTF-8 character boundaries, ascending, unique.
std::vector<size_t> hyphenPoints(const LayoutStyle& style, const std::string& word) {
  std::vector<size_t> pts = style.hyphenate(word);
  pts.erase(std::remove_if(pts.begin(), pts.end(),
                           [&](size_t p) {
                             return p == 0 || p >= word.size() ||
                                    (static_cast<unsigned char>(word[p]) & 0xC0) == 0x80;
                           }),
            pts.end());
  std::sort(pts.begin(), pts.end());
  pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
  return pts;
}

// The text that ends a hyphenated line: the prefix plus "-" (unless it already ends in one).
std::string hyphenPiece(const std::string& prefix) {
  return !prefix.empty() && prefix.back() == '-' ? prefix : prefix + "-";
}

size_t countSpaces(const std::string& s) { return static_cast<size_t>(std::count(s.begin(), s.end(), ' ')); }

}  // namespace

std::vector<std::string> splitWords(const std::string& text) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && isSpace(text[i])) i++;
    const size_t start = i;
    while (i < text.size() && !isSpace(text[i])) i++;
    if (i > start) out.emplace_back(text, start, i - start);
  }
  return out;
}

void ChapterLayout::clear() {
  lines_.clear();
  pageStarts.clear();
}

int ChapterLayout::lineAdvance(const LayoutLine& line) const {
  return line.kind == LineKind::ChapterTitle ? style.titleLineHeight : style.lineHeight;
}

void ChapterLayout::addBlock(std::vector<std::string>& tokens, std::vector<uint16_t>& tokenVerse, LineKind kind,
                             int gapBefore) {
  const bool boldBlock = kind != LineKind::Text;
  const auto width = [&](const std::string& t) {
    if (isVerseNumber(t)) return style.measureNumber ? style.measureNumber(t.substr(1)) : style.measure(t.substr(1), true);
    return style.measure(t, boldBlock);
  };

  LayoutLine line;
  line.kind = kind;
  line.gapBefore = static_cast<int16_t>(gapBefore);
  int lineWidth = 0;
  bool empty = true;

  const auto emit = [&](bool justify) {
    line.justify = justify && kind == LineKind::Text;
    line.rtl = style.rtl;
    line.spreadChars = style.charWrap && kind == LineKind::Text && countSpaces(line.text) < 2;
    lines_.push_back(std::move(line));
    line = LayoutLine();
    line.kind = kind;
    lineWidth = 0;
    empty = true;
  };
  const auto append = [&](const std::string& t, int w, uint16_t verse) {
    if (empty) {
      line.verseAtStart = verse;
    } else {
      line.text += ' ';
      lineWidth += style.spaceWidth;
    }
    line.text += t;
    lineWidth += w;
    line.lastVerse = verse;
    empty = false;
  };

  for (size_t i = 0; i < tokens.size(); i++) {
    const std::string& t = tokens[i];
    const uint16_t verse = tokenVerse[i];
    const int w = width(t);
    if (style.charWrap && kind == LineKind::Text && !isVerseNumber(t)) {
      // Character wrap: fill the line, breaking the word between characters if needed.
      std::string rest = t;
      while (!rest.empty()) {
        const int avail = empty ? style.width : style.width - lineWidth - style.spaceWidth;
        const int rw = style.measure(rest, false);
        if (rw <= avail) {
          append(rest, rw, verse);
          break;
        }
        size_t cut = 0;
        for (size_t p = 0; p < rest.size();) {
          const size_t n = std::min(utf8CharLen(static_cast<unsigned char>(rest[p])), rest.size() - p);
          if (style.measure(rest.substr(0, p + n), false) > avail) break;
          p += n;
          cut = p;
        }
        // Keep closing punctuation off the start of the next line, and opening punctuation
        // off the end of this one.
        const auto prevChar = [&rest](size_t at) {
          size_t prev = at - 1;
          while (prev > 0 && (static_cast<unsigned char>(rest[prev]) & 0xC0) == 0x80) prev--;
          return prev;
        };
        while (cut > 0 && (isClosingPunct(rest, cut) || isOpeningPunct(rest, prevChar(cut)))) cut = prevChar(cut);
        if (cut == 0) {
          if (!empty) {
            emit(true);
            continue;
          }
          cut = std::min(utf8CharLen(static_cast<unsigned char>(rest[0])), rest.size());
        }
        const std::string piece = rest.substr(0, cut);
        append(piece, style.measure(piece, false), verse);
        emit(true);
        rest.erase(0, cut);
      }
      continue;
    }
    const bool hyphenating = style.hyphenate && !style.charWrap && kind == LineKind::Text;
    // A verse number stays on the line of the word it introduces (with character wrap, its first character;
    // with hyphenation, its first hyphenated piece).
    int keep = w;
    if (isVerseNumber(t) && i + 1 < tokens.size() && tokenVerse[i + 1] == verse) {
      const std::string& next = tokens[i + 1];
      int leadWidth = 0;
      if (style.charWrap) {
        leadWidth = width(next.substr(0, std::min(utf8CharLen(static_cast<unsigned char>(next[0])), next.size())));
      } else {
        leadWidth = width(next);
        if (hyphenating && !isVerseNumber(next)) {
          const std::vector<size_t> pts = hyphenPoints(style, next);
          if (!pts.empty()) leadWidth = std::min(leadWidth, style.measure(hyphenPiece(next.substr(0, pts[0])), false));
        }
      }
      keep = std::min(w + style.spaceWidth + leadWidth, style.width);
    }

    std::string word = t;
    int wordWidth = w;
    if (hyphenating && !isVerseNumber(t)) {
      const std::vector<size_t> pts = hyphenPoints(style, t);
      if (!pts.empty()) {
        // Fill the line with the largest hyphenated prefix that fits, carry the rest to the next line.
        size_t consumed = 0;
        bool done = false;
        while (true) {
          const int avail = empty ? style.width : style.width - lineWidth - style.spaceWidth;
          if (wordWidth <= avail) {
            append(word, wordWidth, verse);
            done = true;
            break;
          }
          bool split = false;
          for (size_t k = pts.size(); k-- > 0 && pts[k] > consumed;) {
            const std::string piece = hyphenPiece(t.substr(consumed, pts[k] - consumed));
            const int pw = style.measure(piece, false);
            if (pw > avail) continue;
            append(piece, pw, verse);
            emit(true);
            consumed = pts[k];
            word = t.substr(consumed);
            wordWidth = style.measure(word, false);
            split = true;
            break;
          }
          if (split) continue;
          if (empty) break;  // wider than a whole line with no usable point: hard break below
          emit(true);
        }
        if (done) continue;
      }
    }

    if (!empty && lineWidth + style.spaceWidth + keep > style.width) emit(true);
    if (wordWidth <= style.width || isVerseNumber(t)) {
      append(word, wordWidth, verse);
      continue;
    }
    // A token wider than the line: break it between characters.
    std::string piece;
    for (size_t p = 0; p < word.size();) {
      const size_t n = std::min(utf8CharLen(static_cast<unsigned char>(word[p])), word.size() - p);
      std::string next = piece + word.substr(p, n);
      if (!piece.empty() && style.measure(next, boldBlock) > style.width - (empty ? 0 : lineWidth + style.spaceWidth)) {
        append(piece, style.measure(piece, boldBlock), verse);
        emit(false);
        piece = word.substr(p, n);
      } else {
        piece = std::move(next);
      }
      p += n;
    }
    if (!piece.empty()) append(piece, style.measure(piece, boldBlock), verse);
  }
  if (!empty) emit(false);
  tokens.clear();
  tokenVerse.clear();
}

void ChapterLayout::build(const ChapterContent& content, const LayoutStyle& layoutStyle) {
  clear();
  style = layoutStyle;
  if (style.width <= 0 || style.height <= 0 || style.lineHeight <= 0 || !style.measure) return;
  if (style.titleLineHeight <= 0) style.titleLineHeight = style.lineHeight;

  std::vector<std::string> tokens;
  std::vector<uint16_t> tokenVerse;

  if (!content.title.empty()) {
    tokens = splitWords(content.title);
    tokenVerse.assign(tokens.size(), 0);
    addBlock(tokens, tokenVerse, LineKind::ChapterTitle, 0);
  }

  size_t h = 0;
  int pendingGap = 0;
  for (size_t i = 0; i < content.verses.size(); i++) {
    const ChapterVerse& v = content.verses[i];
    bool headed = false;
    for (; h < content.headings.size() && content.headings[h].verseIndex <= i; h++) {
      const Heading& hd = content.headings[h];
      if (hd.verseIndex != i) continue;
      if (!tokens.empty()) addBlock(tokens, tokenVerse, LineKind::Text, pendingGap);
      std::vector<std::string> ht = splitWords(hd.text);
      // The heading belongs above verse v: it counts as the end of the previous verse.
      std::vector<uint16_t> hv(ht.size(), static_cast<uint16_t>(v.number - 1));
      addBlock(ht, hv, hd.kind == HeadingKind::Section ? LineKind::Heading : LineKind::PsalmTitle,
               lines_.empty() ? 0 : style.blockGap);
      headed = true;
    }

    const bool paragraph = (v.flags & VERSE_PARAGRAPH) != 0 && i > 0;
    if (style.versePerLine || paragraph || headed) {
      if (!tokens.empty()) addBlock(tokens, tokenVerse, LineKind::Text, pendingGap);
      pendingGap = paragraph && !headed ? style.blockGap / 2 : 0;
    }
    tokens.push_back(std::string(1, VERSE_NUMBER_MARK) + std::to_string(v.number));
    tokenVerse.push_back(v.number);
    for (auto& w : splitWords(v.text)) {
      tokens.push_back(std::move(w));
      tokenVerse.push_back(v.number);
    }
  }
  if (!tokens.empty()) addBlock(tokens, tokenVerse, LineKind::Text, pendingGap);
  paginate();
}

void ChapterLayout::paginate() {
  pageStarts.clear();
  if (lines_.empty()) {
    pageStarts.push_back(0);
    return;
  }
  pageStarts.push_back(0);
  int y = 0;
  for (size_t i = 0; i < lines_.size(); i++) {
    const bool first = static_cast<int>(i) == pageStarts.back();
    const int adv = lineAdvance(lines_[i]) + (first ? 0 : lines_[i].gapBefore);
    if (!first && y + adv > style.height) {
      // Keep headings with the text below them.
      int start = static_cast<int>(i);
      while (start - 1 > pageStarts.back() && lines_[start - 1].kind != LineKind::Text) start--;
      pageStarts.push_back(start);
      y = 0;
      for (int k = start; k <= static_cast<int>(i); k++) {
        y += lineAdvance(lines_[k]) + (k == start ? 0 : lines_[k].gapBefore);
      }
      continue;
    }
    y += adv;
  }
}

int ChapterLayout::pageEndLine(int page) const {
  return page + 1 < pageCount() ? pageStarts[page + 1] : static_cast<int>(lines_.size());
}

int ChapterLayout::pageOfVerse(int verse) const {
  for (size_t i = 0; i < lines_.size(); i++) {
    if (lines_[i].kind == LineKind::Text && lines_[i].lastVerse >= verse) {
      const auto it = std::upper_bound(pageStarts.begin(), pageStarts.end(), static_cast<int>(i));
      return static_cast<int>(it - pageStarts.begin()) - 1;
    }
  }
  return pageCount() > 0 ? pageCount() - 1 : 0;
}

uint16_t ChapterLayout::pageFirstVerse(int page) const {
  if (page < 0 || page >= pageCount()) return 0;
  for (int i = pageFirstLine(page); i < pageEndLine(page); i++) {
    if (lines_[i].kind == LineKind::Text) return lines_[i].verseAtStart;
  }
  return 0;
}

uint16_t ChapterLayout::pageLastVerse(int page) const {
  if (page < 0 || page >= pageCount()) return 0;
  for (int i = pageEndLine(page) - 1; i >= pageFirstLine(page); i--) {
    if (lines_[i].kind == LineKind::Text) return lines_[i].lastVerse;
  }
  return 0;
}

}  // namespace bible
