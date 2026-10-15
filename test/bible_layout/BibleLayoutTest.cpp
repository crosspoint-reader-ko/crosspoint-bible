#include <gtest/gtest.h>

#include "BibleLayout.h"

using namespace bible;

namespace {

// Every UTF-8 character is 10 px wide (bold 12 px), a space 10 px.
int fakeWidth(const std::string& s, bool bold) {
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n * (bold ? 12 : 10);
}

LayoutStyle style(int width, int height, bool versePerLine = true) {
  LayoutStyle s;
  s.width = width;
  s.height = height;
  s.lineHeight = 20;
  s.titleLineHeight = 40;
  s.blockGap = 10;
  s.versePerLine = versePerLine;
  s.measure = fakeWidth;
  s.spaceWidth = 10;
  return s;
}

ChapterContent chapter(int verses, const char* text) {
  ChapterContent c;
  c.title = "3장";
  for (int i = 1; i <= verses; i++) c.verses.push_back({static_cast<uint16_t>(i), 0, text});
  return c;
}

const char NUM = VERSE_NUMBER_MARK;

}  // namespace

TEST(BibleLayout, SplitWords) {
  EXPECT_EQ(splitWords("  가 나다\t라 "), (std::vector<std::string>{"가", "나다", "라"}));
  EXPECT_TRUE(splitWords("   ").empty());
}

TEST(BibleLayout, VersePerLineWrapsAndJustifies) {
  ChapterLayout l;
  // width 100: "1" (12) + " " + "가나다" (30) + " " + "라마" (20) = 82; next word would overflow.
  l.build(chapter(2, "가나다 라마 바사아"), style(100, 1000));
  const auto& lines = l.lines();
  ASSERT_EQ(lines.size(), 5u);  // title + 2 lines per verse
  EXPECT_EQ(lines[0].kind, LineKind::ChapterTitle);
  EXPECT_EQ(lines[1].text, std::string(1, NUM) + "1 가나다 라마");
  EXPECT_TRUE(lines[1].justify);
  EXPECT_EQ(lines[2].text, "바사아");
  EXPECT_FALSE(lines[2].justify);
  EXPECT_EQ(lines[3].verseAtStart, 2);
  EXPECT_EQ(lines[2].lastVerse, 1);
}

TEST(BibleLayout, ParagraphModeFlowsVerses) {
  ChapterLayout l;
  ChapterContent c = chapter(3, "가");
  c.verses[2].flags = VERSE_PARAGRAPH;
  l.build(c, style(1000, 1000, false));
  const auto& lines = l.lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[1].text, std::string(1, NUM) + "1 가 " + NUM + "2 가");
  EXPECT_EQ(lines[2].gapBefore, 5);
  EXPECT_EQ(lines[2].verseAtStart, 3);
}

TEST(BibleLayout, VerseNumberStaysWithItsText) {
  ChapterLayout l;
  ChapterContent c = chapter(2, "가나다라");
  l.build(c, style(1000, 1000, false));
  // Narrow: "1 가나다라" = 12+10+40 = 62 fits 70; "2" + " 가나다라" must move together.
  l.build(c, style(70, 1000, false));
  const auto& lines = l.lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[2].text, std::string(1, NUM) + "2 가나다라");
}

TEST(BibleLayout, HardBreaksLongWords) {
  ChapterLayout l;
  ChapterContent c = chapter(1, "가나다라마바사아자차");  // 100 px in a 45 px line
  c.title.clear();
  l.build(c, style(45, 1000));
  const auto& lines = l.lines();
  ASSERT_GE(lines.size(), 3u);
  for (const auto& line : lines) EXPECT_LE(fakeWidth(line.text, false), 60) << line.text;
}

TEST(BibleLayout, HeadingsAndPagination) {
  ChapterLayout l;
  ChapterContent c = chapter(6, "가");
  c.headings.push_back({3, HeadingKind::Section, "소제목"});
  // Height 100: title 40 + verses 1..3 (60) fill page 0. The heading (gap 10 + 20) and verse 4 go to page 1.
  l.build(c, style(1000, 100));
  ASSERT_EQ(l.pageCount(), 2);
  EXPECT_EQ(l.pageFirstVerse(0), 1);
  EXPECT_EQ(l.pageLastVerse(0), 3);
  EXPECT_EQ(l.lines()[l.pageFirstLine(1)].kind, LineKind::Heading);
  EXPECT_EQ(l.pageFirstVerse(1), 4);
  EXPECT_EQ(l.pageOfVerse(4), 1);
  EXPECT_EQ(l.pageOfVerse(1), 0);
  EXPECT_EQ(l.pageOfVerse(99), 1);
}

TEST(BibleLayout, HeadingIsNotOrphaned) {
  ChapterLayout l;
  ChapterContent c = chapter(4, "가");
  c.headings.push_back({2, HeadingKind::Section, "소제목"});
  // title 40 + v1 20 + v2 20 = 80; heading (10+20) would end at 110 > 100 anyway; use height 115:
  // heading fits (ends at 110) but verse 3 does not, so the heading moves to page 1.
  l.build(c, style(1000, 115));
  ASSERT_EQ(l.pageCount(), 2);
  EXPECT_EQ(l.lines()[l.pageFirstLine(1)].kind, LineKind::Heading);
  EXPECT_EQ(l.pageLastVerse(0), 2);
}

TEST(BibleLayout, CharWrapFillsLinesAndKeepsPunctuation) {
  ChapterLayout l;
  ChapterContent c = chapter(1, "가나 다라마바사 아자.");
  c.title.clear();
  LayoutStyle st = style(70, 1000);
  st.charWrap = true;
  l.build(c, st);
  const auto& lines = l.lines();
  // "1"(12) + " " + "가나"(20) = 42; 70-42-10 = 18 px left: one more character "다".
  ASSERT_GE(lines.size(), 3u);
  EXPECT_EQ(lines[0].text, std::string(1, NUM) + "1 가나 다");
  EXPECT_TRUE(lines[0].justify);
  // "라마바사" (40) + " " leaves 20 px: "아자" fits, but "." may not start a line, so "자" moves down.
  EXPECT_EQ(lines[1].text, "라마바사 아");
  EXPECT_EQ(lines.back().text, "자.");
  EXPECT_FALSE(lines.back().justify);
  for (const auto& line : lines) EXPECT_LE(fakeWidth(line.text, false), 70 + 2) << line.text;
}

namespace {

// Fake hyphenation: "righteousness" -> right|eous|ness, "everlasting" -> ever|last|ing.
std::vector<size_t> fakeHyphenate(const std::string& word) {
  if (word == "righteousness") return {5, 9};
  if (word == "everlasting") return {4, 8};
  if (word == "well-known") return {5};
  return {};
}

std::string num(int n) { return std::string(1, NUM) + std::to_string(n); }

}  // namespace

TEST(BibleLayout, HyphenatesWordThatDoesNotFit) {
  ChapterLayout l;
  ChapterContent c = chapter(1, "in righteousness");
  c.title.clear();
  // "1"(12) + " in"(30) = 42; at width 120, 120-42-10 = 68 left: "righteous-" (100) no, "right-" (60) yes.
  LayoutStyle st = style(120, 1000);
  st.hyphenate = fakeHyphenate;
  l.build(c, st);
  const auto& lines = l.lines();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].text, num(1) + " in right-");
  EXPECT_TRUE(lines[0].justify);
  EXPECT_EQ(lines[1].text, "eousness");
  EXPECT_FALSE(lines[1].justify);
  EXPECT_EQ(lines[1].verseAtStart, 1);
  for (const auto& line : lines) EXPECT_LE(fakeWidth(line.text, false), 120) << line.text;
}

TEST(BibleLayout, HyphenationPicksLargestFittingPoint) {
  ChapterLayout l;
  ChapterContent c = chapter(1, "in righteousness");
  c.title.clear();
  // 160-52 = 108 left: "righteous-" (100) fits, "righteousness" (130) does not.
  LayoutStyle st = style(160, 1000);
  st.hyphenate = fakeHyphenate;
  l.build(c, st);
  ASSERT_EQ(l.lines().size(), 2u);
  EXPECT_EQ(l.lines()[0].text, num(1) + " in righteous-");
  EXPECT_EQ(l.lines()[1].text, "ness");
}

TEST(BibleLayout, NoHyphenationWithoutCallbackOrWhenNothingFits) {
  ChapterContent c = chapter(1, "in righteousness");
  c.title.clear();
  ChapterLayout l;
  l.build(c, style(140, 1000));  // no hyphenate: plain word wrap (88 px left, word is 130)
  ASSERT_EQ(l.lines().size(), 2u);
  EXPECT_EQ(l.lines()[0].text, num(1) + " in");
  EXPECT_EQ(l.lines()[1].text, "righteousness");

  // 48 px left at width 100 ("right-" is 60), so the word wraps whole. It is still wider than the line
  // (130 > 100), so the new line hyphenates it at the largest point that fits: "righteous-" (100).
  LayoutStyle st = style(100, 1000);
  st.hyphenate = fakeHyphenate;
  l.build(c, st);
  ASSERT_EQ(l.lines().size(), 3u);
  EXPECT_EQ(l.lines()[0].text, num(1) + " in");
  EXPECT_EQ(l.lines()[1].text, "righteous-");
  EXPECT_EQ(l.lines()[2].text, "ness");

  ChapterContent fits = chapter(1, "into everlasting");  // 110 px: fits a whole line, wraps unbroken
  fits.title.clear();
  st = style(110, 1000);
  st.hyphenate = fakeHyphenate;
  // "1 into" = 62; 110-62-10 = 38 px left: "ever-" (50) does not fit.
  l.build(fits, st);
  ASSERT_EQ(l.lines().size(), 2u);
  EXPECT_EQ(l.lines()[0].text, num(1) + " into");
  EXPECT_EQ(l.lines()[1].text, "everlasting");
}

TEST(BibleLayout, HyphenatesWordWiderThanLine) {
  ChapterLayout l;
  ChapterContent c = chapter(1, "everlasting");
  c.title.clear();
  LayoutStyle st = style(60, 1000, false);
  st.hyphenate = fakeHyphenate;
  // Line 1: "1" then 60-12-10 = 38 px left: no prefix fits, so the word starts line 2 and splits at its
  // hyphenation points: "ever-" (50), "last-" (50), "ing".
  l.build(c, st);
  const auto& lines = l.lines();
  ASSERT_EQ(lines.size(), 4u);
  EXPECT_EQ(lines[0].text, num(1));
  EXPECT_EQ(lines[1].text, "ever-");
  EXPECT_EQ(lines[2].text, "last-");
  EXPECT_EQ(lines[3].text, "ing");
  for (const auto& line : lines) EXPECT_LE(fakeWidth(line.text, false), 60) << line.text;
}

TEST(BibleLayout, HyphenationFallsBackToHardBreak) {
  ChapterLayout l;
  ChapterContent c = chapter(1, "righteousness");
  c.title.clear();
  LayoutStyle st = style(40, 1000);  // even "right-" (60) is wider than the line
  st.hyphenate = fakeHyphenate;
  l.build(c, st);
  for (const auto& line : l.lines()) EXPECT_LE(fakeWidth(line.text, false), 40) << line.text;
  std::string joined;
  for (size_t i = 1; i < l.lines().size(); i++) joined += l.lines()[i].text;
  EXPECT_EQ(joined, "righteousness");
}

TEST(BibleLayout, NoDoubleHyphenAndNumbersNeverHyphenated) {
  ChapterLayout l;
  ChapterContent c = chapter(1, "a well-known");
  c.title.clear();
  // "1 a" = 32; at width 100, 100-32-10 = 58 left: "well-" (50) fits and gets no second hyphen.
  LayoutStyle st = style(100, 1000);
  st.hyphenate = [](const std::string& w) -> std::vector<size_t> {
    if (!w.empty() && w[0] == VERSE_NUMBER_MARK) ADD_FAILURE() << "verse number passed to hyphenate";
    return fakeHyphenate(w);
  };
  l.build(c, st);
  ASSERT_EQ(l.lines().size(), 2u);
  EXPECT_EQ(l.lines()[0].text, num(1) + " a well-");
  EXPECT_EQ(l.lines()[1].text, "known");
}

TEST(BibleLayout, CharWrapIgnoresHyphenation) {
  ChapterLayout l;
  ChapterContent c = chapter(1, "가나 다라마바사 아자.");
  c.title.clear();
  LayoutStyle st = style(70, 1000);
  st.charWrap = true;
  st.hyphenate = [](const std::string&) -> std::vector<size_t> {
    ADD_FAILURE() << "hyphenate called with charWrap";
    return {};
  };
  l.build(c, st);
  EXPECT_EQ(l.lines()[0].text, num(1) + " 가나 다");
}

TEST(BibleLayout, RtlFlagPropagatesWithoutReordering) {
  ChapterLayout l;
  ChapterContent c = chapter(2, "שלום עולם");
  LayoutStyle st = style(1000, 1000);
  st.rtl = true;
  l.build(c, st);
  const auto& lines = l.lines();
  ASSERT_EQ(lines.size(), 3u);
  for (const auto& line : lines) EXPECT_TRUE(line.rtl);
  // Logical order: the verse number comes first (drawn rightmost by the renderer).
  EXPECT_EQ(lines[1].text, num(1) + " שלום עולם");

  st.rtl = false;
  l.build(c, st);
  for (const auto& line : l.lines()) EXPECT_FALSE(line.rtl);
}

TEST(BibleLayout, CjkCharWrapSetsSpreadHint) {
  ChapterLayout l;
  // Chinese: no spaces in the verse text.
  ChapterContent c = chapter(1, "起初神创造天地地是空虚混沌渊面黑暗");
  c.title.clear();
  LayoutStyle st = style(100, 1000);
  st.charWrap = true;
  l.build(c, st);
  const auto& lines = l.lines();
  ASSERT_GE(lines.size(), 2u);
  for (const auto& line : lines) {
    EXPECT_TRUE(line.spreadChars) << line.text;
    EXPECT_LE(fakeWidth(line.text, false), 100 + 2) << line.text;
  }
  EXPECT_TRUE(lines[0].justify);
  EXPECT_FALSE(lines.back().justify);
}

TEST(BibleLayout, SpreadHintOffForSpacedOrWordWrappedText) {
  ChapterLayout l;
  ChapterContent c = chapter(1, "가 나 다 라 마 바 사 아");  // Korean: spaces between words
  c.title.clear();
  LayoutStyle st = style(1000, 1000);
  st.charWrap = true;
  l.build(c, st);
  ASSERT_EQ(l.lines().size(), 1u);
  EXPECT_FALSE(l.lines()[0].spreadChars);

  ChapterContent zh = chapter(1, "起初神创造天地");
  zh.title = "第一章";
  st.charWrap = false;  // word wrap never sets the hint
  l.build(zh, st);
  for (const auto& line : l.lines()) EXPECT_FALSE(line.spreadChars);

  st.charWrap = true;  // headings and the title never spread
  l.build(zh, st);
  EXPECT_FALSE(l.lines()[0].spreadChars);
  EXPECT_TRUE(l.lines()[1].spreadChars);
}

TEST(BibleLayout, CharWrapKeepsOpeningQuoteOffLineEnd) {
  ChapterLayout l;
  // "가나다“라마" in a 50 px line after the number: "1"(12)+" "(10) leaves 28 px: "가나" fits,
  // so no quote at the end. Then "다“라마" (one char = 10 px, quote = 10 px) ...
  ChapterContent c = chapter(1, "가나다라“마바");
  c.title.clear();
  LayoutStyle st = style(60, 1000);
  st.charWrap = true;
  l.build(c, st);
  for (const auto& line : l.lines()) {
    ASSERT_FALSE(line.text.empty());
    EXPECT_NE(line.text.substr(line.text.size() - 3), "\xE2\x80\x9C") << line.text;
  }
}

TEST(BibleLayout, BalancesLinesOverTheBlock) {
  ChapterLayout l;
  ChapterContent c = chapter(1, "aa bb cc ddddddd eeeee");
  c.title.clear();
  // Greedy filling would set "1 aa bb cc" (102, full) and then "ddddddd" alone, 32 px short with no
  // space to stretch. Moving "cc" down evens the block out.
  l.build(c, style(102, 1000));
  const auto& lines = l.lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0].text, num(1) + " aa bb");
  EXPECT_EQ(lines[1].text, "cc ddddddd");
  EXPECT_EQ(lines[2].text, "eeeee");
  EXPECT_TRUE(lines[0].justify);
  EXPECT_TRUE(lines[1].justify);
  EXPECT_FALSE(lines[2].justify);
}

TEST(BibleLayout, BalancedLinesFitAndKeepVerseNumbersWithText) {
  ChapterLayout l;
  ChapterContent c = chapter(4, "a bbb cc dddd e ffffff gg hhh iiiii j kk llllllll m nnnn");
  LayoutStyle st = style(130, 1000, false);
  st.hyphenate = fakeHyphenate;
  l.build(c, st);
  for (const auto& line : l.lines()) {
    if (line.kind != LineKind::Text) continue;
    const auto tokens = splitWords(line.text);
    ASSERT_FALSE(tokens.empty());
    EXPECT_NE(tokens.back()[0], NUM) << line.text;  // a number never ends a line before its text
    int w = 0;
    for (const auto& t : tokens) w += t[0] == NUM ? fakeWidth(t.substr(1), true) : fakeWidth(t, false);
    w += static_cast<int>(tokens.size() - 1) * st.spaceWidth;
    EXPECT_LE(w, st.width) << line.text;
  }
}
