#include "Hyphenation.h"

#include <Utf8.h>

#include <algorithm>
#include <cassert>
#include <cstring>

#include "HyphenationCommon.h"
#include "LanguageHyphenator.h"
#include "generated/hyph-de.trie.h"
#include "generated/hyph-en.trie.h"
#include "generated/hyph-es.trie.h"
#include "generated/hyph-fi.trie.h"
#include "generated/hyph-fr.trie.h"
#include "generated/hyph-it.trie.h"
#include "generated/hyph-pl.trie.h"
#include "generated/hyph-pt.trie.h"
#include "generated/hyph-ru.trie.h"
#include "generated/hyph-sv.trie.h"
#include "generated/hyph-uk.trie.h"

namespace {

struct BreakInfo {
  size_t byteOffset;
  bool requiresInsertedHyphen;
};

// Maps a codepoint index back to its byte offset inside the source word.
size_t byteOffsetForIndex(const std::vector<CodepointInfo>& cps, const size_t index) {
  return (index < cps.size()) ? cps[index].byteOffset : (cps.empty() ? 0 : cps.back().byteOffset);
}

// Builds a vector of break information from explicit hyphen markers in the given codepoints.
// Only hyphens that appear between two alphabetic characters are considered valid breaks.
//
// Example: "US-Satellitensystems" (cps: U, S, -, S, a, t, ...)
//   -> finds '-' at index 2 with alphabetic neighbors 'S' and 'S'
//   -> returns one BreakInfo at the byte offset of 'S' (the char after '-'),
//      with requiresInsertedHyphen=false because '-' is already visible.
//
// Example: "Satel\u00ADliten" (soft-hyphen between 'l' and 'l')
//   -> returns one BreakInfo with requiresInsertedHyphen=true (soft-hyphen
//      is invisible and needs a visible '-' when the break is used).
std::vector<BreakInfo> buildExplicitBreakInfos(const std::vector<CodepointInfo>& cps) {
  std::vector<BreakInfo> breaks;

  for (size_t i = 1; i + 1 < cps.size(); ++i) {
    const uint32_t cp = cps[i].value;
    if (!isExplicitHyphen(cp) || !isAlphabetic(cps[i - 1].value) || !isAlphabetic(cps[i + 1].value)) {
      continue;
    }
    // Offset points to the next codepoint so rendering starts after the hyphen marker.
    breaks.push_back({cps[i + 1].byteOffset, isSoftHyphen(cp)});
  }

  return breaks;
}

bool isSegmentSeparator(const uint32_t cp) { return isExplicitHyphen(cp) || isApostrophe(cp); }

void appendSegmentPatternBreaks(const std::vector<CodepointInfo>& cps, const LanguageHyphenator& hyphenator,
                                const bool includeFallback, std::vector<BreakInfo>& outBreaks) {
  size_t segStart = 0;

  for (size_t i = 0; i <= cps.size(); ++i) {
    const bool atEnd = i == cps.size();
    const bool atSeparator = !atEnd && isSegmentSeparator(cps[i].value);
    if (!atEnd && !atSeparator) {
      continue;
    }

    if (i > segStart) {
      std::vector<CodepointInfo> segment(cps.begin() + segStart, cps.begin() + i);
      auto segIndexes = hyphenator.breakIndexes(segment);

      if (includeFallback && segIndexes.empty()) {
        const size_t minPrefix = hyphenator.minPrefix();
        const size_t minSuffix = hyphenator.minSuffix();
        for (size_t idx = minPrefix; idx + minSuffix <= segment.size(); ++idx) {
          segIndexes.push_back(idx);
        }
      }

      for (const size_t idx : segIndexes) {
        assert(idx > 0 && idx < segment.size());
        if (idx == 0 || idx >= segment.size()) continue;
        const size_t cpIdx = segStart + idx;
        if (cpIdx < cps.size()) {
          outBreaks.push_back({cps[cpIdx].byteOffset, true});
        }
      }
    }

    segStart = i + 1;
  }
}

void appendApostropheContractionBreaks(const std::vector<CodepointInfo>& cps,
                                       std::vector<BreakInfo>& outBreaks) {
  constexpr size_t kMinLeftSegmentLen = 3;
  constexpr size_t kMinRightSegmentLen = 3;
  size_t segmentStart = 0;

  for (size_t i = 0; i < cps.size(); ++i) {
    if (isSegmentSeparator(cps[i].value)) {
      if (isApostrophe(cps[i].value) && i > 0 && i + 1 < cps.size() && isAlphabetic(cps[i - 1].value) &&
          isAlphabetic(cps[i + 1].value)) {
        size_t leftPrefixLen = 0;
        for (size_t j = segmentStart; j < i; ++j) {
          if (isAlphabetic(cps[j].value)) {
            ++leftPrefixLen;
          }
        }

        size_t rightSuffixLen = 0;
        for (size_t j = i + 1; j < cps.size() && !isSegmentSeparator(cps[j].value); ++j) {
          if (isAlphabetic(cps[j].value)) {
            ++rightSuffixLen;
          }
        }

        // Avoid stranding short clitics like "l'"/"d'" or contraction tails like "'ve"/"'re"/"'ll".
        if (leftPrefixLen >= kMinLeftSegmentLen && rightSuffixLen >= kMinRightSegmentLen) {
          outBreaks.push_back({cps[i + 1].byteOffset, false});
        }
      }
      segmentStart = i + 1;
    }
  }
}

void sortAndDedupeBreakInfos(std::vector<BreakInfo>& infos) {
  std::sort(infos.begin(), infos.end(), [](const BreakInfo& a, const BreakInfo& b) {
    if (a.byteOffset != b.byteOffset) {
      return a.byteOffset < b.byteOffset;
    }
    return a.requiresInsertedHyphen < b.requiresInsertedHyphen;
  });

  infos.erase(std::unique(infos.begin(), infos.end(),
                          [](const BreakInfo& a, const BreakInfo& b) {
                            return a.byteOffset == b.byteOffset;
                          }),
              infos.end());
}

// Same minimum prefix/suffix per language as upstream (English 3/3, others 2/2).
LanguageHyphenator englishHyphenator(en_patterns, isLatinLetter, toLowerLatin, 3, 3);
LanguageHyphenator frenchHyphenator(fr_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator germanHyphenator(de_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator russianHyphenator(ru_patterns, isCyrillicLetter, toLowerCyrillic);
LanguageHyphenator spanishHyphenator(es_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator italianHyphenator(it_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator swedishHyphenator(sv_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator ukrainianHyphenator(uk_patterns, isCyrillicLetter, toLowerCyrillic);
LanguageHyphenator polishHyphenator(pl_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator finnishHyphenator(fi_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator portugueseHyphenator(pt_patterns, isLatinLetter, toLowerLatin);

struct Entry {
  const char* tag;
  const LanguageHyphenator* hyphenator;
};
const Entry kEntries[] = {{"en", &englishHyphenator},    {"fr", &frenchHyphenator},    {"de", &germanHyphenator},
                          {"ru", &russianHyphenator},    {"es", &spanishHyphenator},   {"it", &italianHyphenator},
                          {"pl", &polishHyphenator},     {"sv", &swedishHyphenator},   {"uk", &ukrainianHyphenator},
                          {"fi", &finnishHyphenator},    {"pt", &portugueseHyphenator}};

struct Iso639Mapping {
  const char* iso639_2;
  const char* iso639_1;
};
constexpr Iso639Mapping kIso639Mappings[] = {{"eng", "en"}, {"fra", "fr"}, {"fre", "fr"}, {"deu", "de"},
                                             {"ger", "de"}, {"rus", "ru"}, {"spa", "es"}, {"ita", "it"},
                                             {"ukr", "uk"}, {"swe", "sv"}, {"fin", "fi"}, {"pol", "pl"},
                                             {"por", "pt"}};

const LanguageHyphenator* hyphenatorForLanguage(const char* langTag) {
  if (!langTag || !*langTag) return nullptr;
  std::string primary;
  for (const char* p = langTag; *p && *p != '-' && *p != '_'; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    primary.push_back(c);
  }
  for (const auto& m : kIso639Mappings) {
    if (primary == m.iso639_2) {
      primary = m.iso639_1;
      break;
    }
  }
  for (const auto& e : kEntries) {
    if (primary == e.tag) return e.hyphenator;
  }
  return nullptr;
}

}  // namespace

namespace hyphenation {

bool hasLanguage(const char* lang) { return hyphenatorForLanguage(lang) != nullptr; }

std::vector<size_t> breakPoints(const char* lang, const std::string& word) {
  std::vector<size_t> result;
  if (word.empty()) return result;
  const auto* hyphenator = hyphenatorForLanguage(lang);

  auto cps = collectCodepoints(word);
  trimSurroundingPunctuationAndFootnote(cps);

  bool hasApostropheLikeSeparator = false;
  for (const auto& cp : cps) {
    if (isApostrophe(cp.value)) {
      hasApostropheLikeSeparator = true;
      break;
    }
  }

  std::vector<BreakInfo> infos = buildExplicitBreakInfos(cps);
  if (!infos.empty()) {
    if (hyphenator) appendSegmentPatternBreaks(cps, *hyphenator, false, infos);
    if (hasApostropheLikeSeparator) appendApostropheContractionBreaks(cps, infos);
    sortAndDedupeBreakInfos(infos);
  } else if (hasApostropheLikeSeparator) {
    if (hyphenator) appendSegmentPatternBreaks(cps, *hyphenator, false, infos);
    appendApostropheContractionBreaks(cps, infos);
    sortAndDedupeBreakInfos(infos);
  } else if (hyphenator) {
    for (const size_t idx : hyphenator->breakIndexes(cps)) infos.push_back({byteOffsetForIndex(cps, idx), true});
  }

  result.reserve(infos.size());
  for (const auto& b : infos) result.push_back(b.byteOffset);
  return result;
}

}  // namespace hyphenation
