#pragma once

// Two-set (두벌식) Hangul composition for the on-screen keyboard. The keyboard
// keeps what was typed as a list of code points (compatibility jamo U+3131..U+3163
// and anything else as-is); compose() turns that list into UTF-8 text, joining jamo
// into syllables. Deleting the last typed code point and composing again gives the
// usual "backspace removes one jamo" behaviour.

#include <cstdint>
#include <string>
#include <vector>

namespace hangul {

bool isConsonant(uint32_t cp);  // ㄱ..ㅎ (compatibility jamo, including double consonants)
bool isVowel(uint32_t cp);      // ㅏ..ㅣ

std::string compose(const std::vector<uint32_t>& typed);

// UTF-8 helpers shared with the keyboard.
void appendUtf8(std::string& out, uint32_t cp);
std::vector<uint32_t> decodeUtf8(const std::string& text);

}  // namespace hangul
