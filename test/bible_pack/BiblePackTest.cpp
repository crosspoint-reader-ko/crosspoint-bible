// fixture.cpb comes from tools/bible/test_epub2cpb.py --write-fixture (synthetic text).
// Set BIBLE_CPB to a real pack (개역개정 or data/bible/krv1961.cpb) to also run the full-Bible checks.
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>

#include "BiblePack.h"

namespace {

class FileSource : public bible::ByteSource {
 public:
  explicit FileSource(const char* path) : in(path, std::ios::binary) {}
  bool readAt(uint32_t offset, void* dst, uint32_t length) override {
    in.clear();
    in.seekg(offset);
    in.read(static_cast<char*>(dst), length);
    return static_cast<uint32_t>(in.gcount()) == length;
  }

 private:
  std::ifstream in;
};

TEST(BiblePack, OpensFixture) {
  FileSource src(BIBLE_FIXTURE);
  bible::BiblePack pack;
  ASSERT_TRUE(pack.open(&src));
  EXPECT_EQ(pack.title(), "테스트");
  EXPECT_STREQ(pack.language(), "ko");
  EXPECT_EQ(pack.bookCount(), 66);
  EXPECT_EQ(pack.totalVerses(), 330u);
  EXPECT_STREQ(pack.book(0).name, "책1");
  EXPECT_STREQ(pack.book(0).abbr, "창");
  EXPECT_STREQ(pack.book(65).abbr, "계");
  EXPECT_EQ(pack.book(39).testament, 1);
  EXPECT_EQ(pack.book(0).chapterCount, 2);
  ASSERT_EQ(pack.metadata().size(), 2u);
  EXPECT_EQ(pack.metadata()[0], (std::pair<std::string, std::string>{"edition", "합성 본문"}));
  EXPECT_EQ(pack.metadata()[1].second, "Public Domain");
  EXPECT_EQ(pack.totalChapters(), 132u);
}

TEST(BiblePack, ReadsVersesAndHeadings) {
  FileSource src(BIBLE_FIXTURE);
  bible::BiblePack pack;
  ASSERT_TRUE(pack.open(&src));
  std::string text;
  const int32_t vi = pack.verseIndex({0, 1, 2});
  ASSERT_EQ(vi, 1);
  ASSERT_TRUE(pack.verseText(vi, text));
  EXPECT_EQ(text, "둘째 절 끝");
  EXPECT_EQ(pack.verseIndex({1, 2, 2}), 9);
  EXPECT_EQ(pack.verseIndex({0, 1, 4}), -1);
  EXPECT_EQ(pack.verseIndex({0, 3, 1}), -1);
  EXPECT_EQ(pack.verseFlags(0) & bible::VERSE_PARAGRAPH, bible::VERSE_PARAGRAPH);
  EXPECT_EQ(pack.verseFlags(2) & bible::VERSE_POETRY, bible::VERSE_POETRY);

  bible::Chapter ch;
  ASSERT_TRUE(pack.chapter(18, 1, ch));  // book 19 has psalm headings
  std::vector<bible::Heading> hs;
  ASSERT_TRUE(pack.chapterHeadings(ch, hs));
  ASSERT_EQ(hs.size(), 3u);
  EXPECT_EQ(hs[0].kind, bible::HeadingKind::PsalmBook);
  EXPECT_EQ(hs[1].text, "[다윗의 시]");
  EXPECT_EQ(hs[2].kind, bible::HeadingKind::Section);
  ASSERT_TRUE(pack.chapter(18, 2, ch));
  ASSERT_TRUE(pack.chapterHeadings(ch, hs));
  EXPECT_TRUE(hs.empty());
}

TEST(BiblePack, Clamps) {
  FileSource src(BIBLE_FIXTURE);
  bible::BiblePack pack;
  ASSERT_TRUE(pack.open(&src));
  const bible::Ref r = pack.clamp({200, 9, 9});
  EXPECT_EQ(r.book, 65);
  EXPECT_EQ(r.chapter, 2);
  EXPECT_EQ(r.verse, 2);
}

TEST(BiblePack, ReadsFromMemoryAcrossChapters) {
  std::ifstream in(BIBLE_FIXTURE, std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  bible::MemorySource src(bytes.data(), static_cast<uint32_t>(bytes.size()));
  bible::BiblePack pack;
  ASSERT_TRUE(pack.open(&src));
  std::string text;
  // Alternate between chapters so every read swaps the cached block.
  ASSERT_TRUE(pack.verseText(pack.verseIndex({65, 2, 2}), text));
  EXPECT_EQ(text, "이장 이절");
  ASSERT_TRUE(pack.verseText(pack.verseIndex({0, 1, 3}), text));
  EXPECT_EQ(text, "셋째 절");
  ASSERT_TRUE(pack.verseText(pack.verseIndex({0, 1, 1}), text));
  EXPECT_EQ(text, "첫째 절");
  EXPECT_FALSE(pack.verseText(330, text));
}

TEST(BiblePack, RejectsGarbage) {
  FileSource src(__FILE__);
  bible::BiblePack pack;
  EXPECT_FALSE(pack.open(&src));
  EXPECT_FALSE(pack.isOpen());
}

TEST(BiblePack, RealBible) {
  const char* path = std::getenv("BIBLE_CPB");
  if (!path) GTEST_SKIP() << "BIBLE_CPB not set";
  FileSource src(path);
  bible::BiblePack pack;
  ASSERT_TRUE(pack.open(&src));
  // 31,102 in the standard versification; the 개역한글 source lacks Psalm 72:20.
  EXPECT_GE(pack.totalVerses(), 31101u);
  EXPECT_EQ(pack.book(18).chapterCount, 150);
  EXPECT_EQ(pack.verseCountOf(18, 119), 176);
  std::string text;
  ASSERT_TRUE(pack.verseText(pack.verseIndex({42, 3, 16}), text));
  EXPECT_EQ(text.rfind("하나님이 세상을 이처럼 사랑하사", 0), 0u);
  ASSERT_TRUE(pack.verseText(pack.verseIndex({18, 119, 176}), text));
  EXPECT_EQ(text.rfind("잃은 양 같이", 0), 0u);
  ASSERT_TRUE(pack.verseText(pack.verseIndex({65, 22, 21}), text));
  EXPECT_EQ(text.substr(text.size() - 6), "아멘");
}

}  // namespace
