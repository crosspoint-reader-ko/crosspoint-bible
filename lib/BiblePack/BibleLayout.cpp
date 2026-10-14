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
    // A verse number stays on the line of the word it introduces.
    const int keep = isVerseNumber(t) && i + 1 < tokens.size() && tokenVerse[i + 1] == verse
                         ? std::min(w + style.spaceWidth + width(tokens[i + 1]), style.width)
                         : w;
    if (!empty && lineWidth + style.spaceWidth + keep > style.width) emit(true);
    if (w <= style.width || isVerseNumber(t)) {
      append(t, w, verse);
      continue;
    }
    // A token wider than the line: break it between characters.
    std::string piece;
    for (size_t p = 0; p < t.size();) {
      const size_t n = std::min(utf8CharLen(static_cast<unsigned char>(t[p])), t.size() - p);
      std::string next = piece + t.substr(p, n);
      if (!piece.empty() && style.measure(next, boldBlock) > style.width - (empty ? 0 : lineWidth + style.spaceWidth)) {
        append(piece, style.measure(piece, boldBlock), verse);
        emit(false);
        piece = t.substr(p, n);
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
