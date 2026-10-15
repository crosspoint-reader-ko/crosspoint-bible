#include "BiblePack.h"

#include <InflateReader.h>

#include <cstring>

namespace bible {

namespace {

constexpr char MAGIC[8] = {'C', 'P', 'B', 'I', 'B', 'L', 'E', '\0'};
constexpr uint32_t HEADER_SIZE = 64;
constexpr uint32_t BOOK_ENTRY_SIZE = 64;
constexpr uint32_t CHAPTER_ENTRY_SIZE = 24;
constexpr uint32_t HEADING_ENTRY_SIZE = 12;
// Verse offsets are u16, so no chapter block exceeds 64 KB (개역개정's largest, Psalm 119, is 17 KB).
constexpr uint32_t MAX_BLOCK_SIZE = 0xFFFF;

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool MemorySource::readAt(uint32_t offset, void* dst, uint32_t length) {
  if (offset > size || length > size - offset) return false;
  memcpy(dst, bytes + offset, length);
  return true;
}

bool BiblePack::open(ByteSource* source) {
  close();
  if (!source) return false;
  uint8_t h[HEADER_SIZE];
  if (!source->readAt(0, h, HEADER_SIZE) || memcmp(h, MAGIC, sizeof(MAGIC)) != 0) return false;
  if (u16(h + 8) != FORMAT_VERSION) return false;
  const uint16_t nBooks = u16(h + 10);
  chapterCount = u32(h + 12);
  verseCount = u32(h + 16);
  const uint32_t offTitle = u32(h + 20);
  const uint32_t offBooks = u32(h + 24);
  offChapters = u32(h + 28);
  offVerses = u32(h + 32);
  offFlags = u32(h + 36);
  offHeadings = u32(h + 40);
  headingCount = u32(h + 44);
  offBlocks = u32(h + 48);
  maxRawSize = u32(h + 52);
  memcpy(lang, h + 56, 7);
  lang[7] = '\0';
  if (nBooks == 0 || nBooks > 255 || chapterCount == 0 || verseCount == 0 || maxRawSize > MAX_BLOCK_SIZE) return false;

  char titleBuf[64] = {};
  if (!source->readAt(offTitle, titleBuf, sizeof(titleBuf) - 1)) return false;
  packTitle = titleBuf;

  books.resize(nBooks);
  uint8_t e[BOOK_ENTRY_SIZE];
  for (uint16_t i = 0; i < nBooks; i++) {
    if (!source->readAt(offBooks + i * BOOK_ENTRY_SIZE, e, BOOK_ENTRY_SIZE)) {
      books.clear();
      return false;
    }
    Book& b = books[i];
    b.testament = e[0];
    b.number = e[1];
    b.chapterCount = u16(e + 2);
    b.firstChapter = u32(e + 4);
    memcpy(b.name, e + 8, 40);
    b.name[39] = '\0';
    memcpy(b.abbr, e + 48, 16);
    b.abbr[15] = '\0';
    if (b.chapterCount == 0 || b.firstChapter + b.chapterCount > chapterCount) {
      books.clear();
      return false;
    }
  }
  src = source;
  return true;
}

void BiblePack::close() {
  src = nullptr;
  books.clear();
  packTitle.clear();
  block.clear();
  block.shrink_to_fit();
  blockChapter = -1;
}

bool BiblePack::chapterAt(uint32_t index, Chapter& out) {
  if (index >= chapterCount) return false;
  uint8_t e[CHAPTER_ENTRY_SIZE];
  if (!read(offChapters + index * CHAPTER_ENTRY_SIZE, e, sizeof(e))) return false;
  out.index = index;
  out.firstVerse = u32(e);
  out.verseCount = u16(e + 4);
  out.firstHeading = u16(e + 6);
  out.blockOffset = u32(e + 8);
  out.blockSize = u32(e + 12);
  out.rawSize = u32(e + 16);
  out.versesEnd = u32(e + 20);
  return out.verseCount > 0 && out.firstVerse + out.verseCount <= verseCount && out.rawSize <= maxRawSize &&
         out.versesEnd <= out.rawSize;
}

bool BiblePack::chapter(int bookIndex, int chapterNumber, Chapter& out) {
  if (bookIndex < 0 || bookIndex >= bookCount()) return false;
  const Book& b = books[bookIndex];
  if (chapterNumber < 1 || chapterNumber > b.chapterCount) return false;
  return chapterAt(b.firstChapter + chapterNumber - 1, out);
}

int BiblePack::verseCountOf(int bookIndex, int chapterNumber) {
  Chapter ch;
  return chapter(bookIndex, chapterNumber, ch) ? ch.verseCount : 0;
}

int32_t BiblePack::verseIndex(const Ref& ref) {
  Chapter ch;
  if (!chapter(ref.book, ref.chapter, ch) || ref.verse < 1 || ref.verse > ch.verseCount) return -1;
  return static_cast<int32_t>(ch.firstVerse + ref.verse - 1);
}

Ref BiblePack::clamp(Ref ref) {
  if (books.empty()) return ref;
  if (ref.book >= books.size()) ref.book = static_cast<uint8_t>(books.size() - 1);
  const uint16_t chapters = books[ref.book].chapterCount;
  if (ref.chapter < 1) ref.chapter = 1;
  if (ref.chapter > chapters) ref.chapter = chapters;
  const int verses = verseCountOf(ref.book, ref.chapter);
  if (ref.verse < 1) ref.verse = 1;
  if (verses > 0 && ref.verse > verses) ref.verse = static_cast<uint16_t>(verses);
  return ref;
}

bool BiblePack::loadBlock(const Chapter& ch) {
  if (blockChapter == static_cast<int32_t>(ch.index)) return true;
  blockChapter = -1;
  block.resize(ch.rawSize);
  if (ch.rawSize == 0) {
    blockInfo = ch;
    blockChapter = static_cast<int32_t>(ch.index);
    return true;
  }
  // Memory-mapped packs inflate straight from flash; SD packs read the compressed block first.
  std::vector<uint8_t> compressed;
  const uint8_t* in = nullptr;
  if (const uint8_t* mapped = src->data()) {
    in = mapped + offBlocks + ch.blockOffset;
  } else {
    compressed.resize(ch.blockSize);
    if (!read(offBlocks + ch.blockOffset, compressed.data(), ch.blockSize)) return false;
    in = compressed.data();
  }
  InflateReader inflater;
  inflater.init(false);
  inflater.setSource(in, ch.blockSize);
  if (!inflater.read(block.data(), ch.rawSize)) return false;
  blockInfo = ch;
  blockChapter = static_cast<int32_t>(ch.index);
  return true;
}

bool BiblePack::chapterOfVerse(uint32_t verseIndex, Chapter& out) {
  if (blockChapter >= 0 && verseIndex >= blockInfo.firstVerse &&
      verseIndex < blockInfo.firstVerse + blockInfo.verseCount) {
    out = blockInfo;
    return true;
  }
  // Binary search the chapter table for the last chapter starting at or before the verse.
  uint32_t lo = 0, hi = chapterCount;
  while (hi - lo > 1) {
    const uint32_t mid = (lo + hi) / 2;
    uint8_t e[4];
    if (!read(offChapters + mid * CHAPTER_ENTRY_SIZE, e, sizeof(e))) return false;
    if (u32(e) <= verseIndex) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return chapterAt(lo, out) && verseIndex < out.firstVerse + out.verseCount;
}

uint32_t BiblePack::verseStart(uint32_t verseIndex) {
  uint8_t e[2] = {};
  read(offVerses + verseIndex * 2, e, sizeof(e));
  return u16(e);
}

bool BiblePack::verseText(uint32_t verseIndex, std::string& out) {
  out.clear();
  if (verseIndex >= verseCount) return false;
  Chapter ch;
  if (!chapterOfVerse(verseIndex, ch) || !loadBlock(ch)) return false;
  const uint32_t start = verseStart(verseIndex);
  const uint32_t end = verseIndex + 1 < ch.firstVerse + ch.verseCount ? verseStart(verseIndex + 1) : ch.versesEnd;
  if (end < start || end > ch.versesEnd) return false;
  out.assign(reinterpret_cast<const char*>(block.data()) + start, end - start);
  return true;
}

uint8_t BiblePack::verseFlags(uint32_t verseIndex) {
  uint8_t f = 0;
  if (verseIndex < verseCount) read(offFlags + verseIndex, &f, 1);
  return f;
}

bool BiblePack::chapterHeadings(const Chapter& ch, std::vector<Heading>& out) {
  out.clear();
  const uint32_t endVerse = ch.firstVerse + ch.verseCount;
  uint8_t e[HEADING_ENTRY_SIZE];
  for (uint32_t i = ch.firstHeading; i < headingCount; i++) {
    if (!read(offHeadings + i * HEADING_ENTRY_SIZE, e, sizeof(e))) return false;
    const uint32_t verse = u32(e);
    if (verse >= endVerse) break;
    if (verse < ch.firstVerse) continue;
    if (!loadBlock(ch)) return false;
    const uint32_t off = u32(e + 4);
    const uint16_t len = u16(e + 8);
    if (off < ch.versesEnd || off + len > ch.rawSize) return false;
    Heading h;
    h.verseIndex = verse;
    h.kind = static_cast<HeadingKind>(e[10]);
    h.text.assign(reinterpret_cast<const char*>(block.data()) + off, len);
    out.push_back(std::move(h));
  }
  return true;
}

}  // namespace bible
