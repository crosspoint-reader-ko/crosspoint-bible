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
