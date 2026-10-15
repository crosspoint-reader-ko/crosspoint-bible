#include "HangulCompose.h"

namespace hangul {

namespace {

// Compatibility jamo, in Unicode order from U+3131.
// Index into the syllable formula: initial (choseong) and final (jongseong) tables.
constexpr uint32_t CHOSEONG[19] = {0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145,
                                   0x3146, 0x3147, 0x3148, 0x3149, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E};
// Index 0 = no final consonant.
constexpr uint32_t JONGSEONG[28] = {0,      0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137, 0x3139, 0x313A,
                                    0x313B, 0x313C, 0x313D, 0x313E, 0x313F, 0x3140, 0x3141, 0x3142, 0x3144, 0x3145,
                                    0x3146, 0x3147, 0x3148, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E};

struct Pair {
  uint32_t a, b, joined;
};
constexpr Pair VOWEL_PAIRS[] = {
    {0x3157, 0x314F, 0x3158},  // ㅗ+ㅏ=ㅘ
    {0x3157, 0x3150, 0x3159},  // ㅗ+ㅐ=ㅙ
    {0x3157, 0x3163, 0x315A},  // ㅗ+ㅣ=ㅚ
    {0x315C, 0x3153, 0x315D},  // ㅜ+ㅓ=ㅝ
    {0x315C, 0x3154, 0x315E},  // ㅜ+ㅔ=ㅞ
    {0x315C, 0x3163, 0x315F},  // ㅜ+ㅣ=ㅟ
    {0x3161, 0x3163, 0x3162},  // ㅡ+ㅣ=ㅢ
};
constexpr Pair FINAL_PAIRS[] = {
    {0x3131, 0x3145, 0x3133},  // ㄳ
    {0x3134, 0x3148, 0x3135},  // ㄵ
    {0x3134, 0x314E, 0x3136},  // ㄶ
    {0x3139, 0x3131, 0x313A},  // ㄺ
    {0x3139, 0x3141, 0x313B},  // ㄻ
    {0x3139, 0x3142, 0x313C},  // ㄼ
    {0x3139, 0x3145, 0x313D},  // ㄽ
    {0x3139, 0x314C, 0x313E},  // ㄾ
    {0x3139, 0x314D, 0x313F},  // ㄿ
    {0x3139, 0x314E, 0x3140},  // ㅀ
    {0x3142, 0x3145, 0x3144},  // ㅄ
};

int indexOf(const uint32_t* table, int n, uint32_t cp) {
  for (int i = 0; i < n; i++) {
    if (table[i] == cp) return i;
  }
  return -1;
}

uint32_t joinPair(const Pair* pairs, int n, uint32_t a, uint32_t b) {
  for (int i = 0; i < n; i++) {
    if (pairs[i].a == a && pairs[i].b == b) return pairs[i].joined;
  }
  return 0;
}

// The two consonants of a compound final, or {final, 0}.
Pair splitFinal(uint32_t t) {
  for (const Pair& p : FINAL_PAIRS) {
    if (p.joined == t) return p;
  }
  return {t, 0, t};
}

// One syllable being built.
struct Block {
  uint32_t l = 0, v = 0, t = 0;
  bool empty() const { return !l && !v && !t; }
};

void flush(std::string& out, Block& b) {
  if (b.empty()) return;
  if (b.l && b.v) {
    const int li = indexOf(CHOSEONG, 19, b.l);
    const int vi = static_cast<int>(b.v - 0x314F);
    const int ti = b.t ? indexOf(JONGSEONG, 28, b.t) : 0;
    appendUtf8(out, 0xAC00 + (li * 21 + vi) * 28 + (ti < 0 ? 0 : ti));
  } else {
    if (b.l) appendUtf8(out, b.l);
    if (b.v) appendUtf8(out, b.v);
  }
  b = Block();
}

}  // namespace

bool isConsonant(uint32_t cp) { return cp >= 0x3131 && cp <= 0x314E; }
bool isVowel(uint32_t cp) { return cp >= 0x314F && cp <= 0x3163; }

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::vector<uint32_t> decodeUtf8(const std::string& text) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    uint32_t cp = c;
    size_t n = 1;
    if (c >= 0xF0) {
      cp = c & 0x07;
      n = 4;
    } else if (c >= 0xE0) {
      cp = c & 0x0F;
      n = 3;
    } else if (c >= 0xC0) {
      cp = c & 0x1F;
      n = 2;
    }
    if (i + n > text.size()) break;
    for (size_t k = 1; k < n; k++) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
    out.push_back(cp);
    i += n;
  }
  return out;
}

std::string compose(const std::vector<uint32_t>& typed) {
  std::string out;
  Block b;
  for (const uint32_t cp : typed) {
    if (isConsonant(cp)) {
      if (b.l && b.v && !b.t) {
        // Final consonant, if this consonant can close a syllable (ㄸ ㅃ ㅉ cannot).
        if (indexOf(JONGSEONG, 28, cp) > 0) {
          b.t = cp;
          continue;
        }
      } else if (b.l && b.v && b.t) {
        if (const uint32_t joined = joinPair(FINAL_PAIRS, 11, b.t, cp)) {
          b.t = joined;
          continue;
        }
      }
      flush(out, b);
      b.l = cp;
    } else if (isVowel(cp)) {
      if (b.t) {
        // The final consonant (or the second half of a compound one) starts the next syllable.
        const Pair split = splitFinal(b.t);
        const uint32_t moved = split.b ? split.b : split.a;
        b.t = split.b ? split.a : 0;
        flush(out, b);
        b.l = moved;
        b.v = cp;
      } else if (b.l && !b.v) {
        b.v = cp;
      } else if (b.v) {
        if (const uint32_t joined = joinPair(VOWEL_PAIRS, 7, b.v, cp)) {
          b.v = joined;
          continue;
        }
        flush(out, b);
        b.v = cp;
      } else {
        b.v = cp;
      }
    } else {
      flush(out, b);
      appendUtf8(out, cp);
    }
  }
  flush(out, b);
  return out;
}

}  // namespace hangul
