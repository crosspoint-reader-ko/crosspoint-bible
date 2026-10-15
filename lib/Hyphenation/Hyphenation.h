#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Liang pattern hyphenation (patterns and algorithm from upstream CrossPoint, lib/Epub/Epub/hyphenation).
// Built-in languages: de en es fi fr it pl pt ru sv uk.
namespace hyphenation {

// Byte offsets inside the UTF-8 word where a hyphen may be inserted, ascending. Empty when the word has no
// break or the language has no patterns. `lang` is a BCP-47 tag or ISO 639-2 code ("en", "en-GB", "deu").
// Surrounding punctuation is ignored. A break at an existing '-' or soft hyphen is reported at the offset
// after it; apostrophes between letters can break too (see upstream Hyphenator).
std::vector<size_t> breakPoints(const char* lang, const std::string& word);

bool hasLanguage(const char* lang);

}  // namespace hyphenation
