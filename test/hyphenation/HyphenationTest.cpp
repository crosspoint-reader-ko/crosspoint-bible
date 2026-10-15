#include <Hyphenation.h>
#include <gtest/gtest.h>

#include <string>

namespace {
// Renders the word with '|' at every break point.
std::string marked(const char* lang, const std::string& word) {
  std::string out;
  size_t prev = 0;
  for (const size_t b : hyphenation::breakPoints(lang, word)) {
    out += word.substr(prev, b - prev) + "|";
    prev = b;
  }
  return out + word.substr(prev);
}
}  // namespace

TEST(Hyphenation, Languages) {
  for (const char* l : {"en", "de", "es", "fi", "fr", "it", "pl", "pt", "ru", "sv", "uk", "en-GB", "EN_us", "eng"}) {
    EXPECT_TRUE(hyphenation::hasLanguage(l)) << l;
  }
  EXPECT_FALSE(hyphenation::hasLanguage("ko"));
  EXPECT_FALSE(hyphenation::hasLanguage(""));
  EXPECT_FALSE(hyphenation::hasLanguage(nullptr));
  EXPECT_TRUE(hyphenation::breakPoints("ko", "hyphenation").empty());
}

TEST(Hyphenation, English) {
  EXPECT_EQ(marked("en", "hyphenation"), "hyphen|ation");
  EXPECT_EQ(marked("en", "beginning"), "begin|ning");
  EXPECT_EQ(marked("en", "righteousness"), "right|eous|ness");
  EXPECT_EQ(marked("en", "Jerusalem"), "Jerusalem");
  EXPECT_EQ(marked("en", "righteousness,"), "right|eous|ness,");
  EXPECT_EQ(marked("en", "\"beginning,\""), "\"begin|ning,\"");
  EXPECT_EQ(marked("en", "God"), "God");
  EXPECT_EQ(marked("en", "well-known"), "well-|known");
}

TEST(Hyphenation, GermanAndRussian) {
  EXPECT_EQ(marked("de", "Gerechtigkeit"), "Ge|rech|tig|keit");
  EXPECT_EQ(marked("ru", "справедливость"), "спра|вед|ли|вость");
}
