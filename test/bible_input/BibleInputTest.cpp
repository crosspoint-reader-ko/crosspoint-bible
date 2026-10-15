#include <gtest/gtest.h>

#include "BibleBookmarks.h"
#include "HangulCompose.h"

namespace {

std::string type(const std::string& jamo) { return hangul::compose(hangul::decodeUtf8(jamo)); }

}  // namespace

TEST(Hangul, ComposesSyllables) {
  EXPECT_EQ(type("ㄱㅏ"), "가");
  EXPECT_EQ(type("ㅎㅏㄴㅏㄴㅣㅁ"), "하나님");
  EXPECT_EQ(type("ㄱㅏㅁㅅㅏ"), "감사");      // final ㅁ stays, ㅅ starts the next syllable
  EXPECT_EQ(type("ㅇㅗㅏㅇ"), "왕");          // ㅗ+ㅏ joins to ㅘ
  EXPECT_EQ(type("ㄷㅏㄹㄱ"), "닭");          // compound final ㄺ
  EXPECT_EQ(type("ㄷㅏㄹㄱㅇㅣ"), "닭이");
  EXPECT_EQ(type("ㄷㅏㄹㄱㅏ"), "달가");      // ㄺ splits: ㄱ moves to the next syllable
  EXPECT_EQ(type("ㅇㅢ"), "의");
  EXPECT_EQ(type("ㄸㅏㄸ"), "따ㄸ");          // ㄸ cannot be a final consonant
}

TEST(Hangul, LoneJamoAndMixedText) {
  EXPECT_EQ(type("ㄱ"), "ㄱ");
  EXPECT_EQ(type("ㅏ"), "ㅏ");
  EXPECT_EQ(type("ㄱㄴ"), "ㄱㄴ");
  EXPECT_EQ(type("ㄱㅏ 1ㄴㅏ"), "가 1나");
  EXPECT_EQ(type("abcㄱㅏ"), "abc가");
}

TEST(Hangul, BackspaceRemovesOneJamo) {
  auto typed = hangul::decodeUtf8("ㄷㅏㄹㄱ");
  typed.pop_back();
  EXPECT_EQ(hangul::compose(typed), "달");
  typed.pop_back();
  EXPECT_EQ(hangul::compose(typed), "다");
}

TEST(Bookmarks, FoldersAndItems) {
  bible::BookmarkStore s;
  s.ensureDefaultFolder("기본");
  ASSERT_EQ(s.folderCount(), 1u);
  const int f = s.addFolder("  감사\t기도 ");
  ASSERT_EQ(f, 1);
  EXPECT_EQ(s.folders()[1].name, "감사 기도");
  EXPECT_EQ(s.addFolder("   "), -1);
  EXPECT_TRUE(s.add(1, {42, 3, 16}));
  EXPECT_TRUE(s.add(1, {18, 23, 1}));
  EXPECT_TRUE(s.add(1, {42, 3, 16}));  // again: moves to the top, no duplicate
  ASSERT_EQ(s.folders()[1].items.size(), 2u);
  EXPECT_EQ(s.folders()[1].items[0], (bible::Bookmark{42, 3, 16}));
  EXPECT_TRUE(s.remove(1, 1));
  EXPECT_FALSE(s.remove(1, 5));
  EXPECT_TRUE(s.removeFolder(0));
  EXPECT_EQ(s.folders()[0].name, "감사 기도");
}

TEST(Bookmarks, RoundTripsAndSkipsJunk) {
  bible::BookmarkStore s;
  s.addFolder("기본");
  s.addFolder("시편");
  s.add(0, {0, 1, 1});
  s.add(1, {18, 23, 1});
  s.add(1, {18, 119, 176});
  const std::string text = s.serialize();

  bible::BookmarkStore r;
  r.parse(text + "X\tfuture record\nB\tbad\n\nB\t1\t0\t1\n");
  ASSERT_EQ(r.folderCount(), 2u);
  EXPECT_EQ(r.folders()[1].name, "시편");
  ASSERT_EQ(r.folders()[1].items.size(), 2u);
  EXPECT_EQ(r.folders()[1].items[0], (bible::Bookmark{18, 119, 176}));
  EXPECT_EQ(r.serialize(), text);
}

TEST(Bookmarks, NameIsCutAtCharacterBoundary) {
  std::string longName;
  for (int i = 0; i < 30; i++) longName += "가";  // 90 bytes
  const std::string clean = bible::BookmarkStore::cleanName(longName);
  EXPECT_LE(clean.size(), bible::BookmarkStore::MAX_NAME_BYTES);
  EXPECT_EQ(clean.size() % 3, 0u);
}
