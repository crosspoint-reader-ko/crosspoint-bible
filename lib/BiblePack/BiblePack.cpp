#include "BiblePack.h"

#include <cstring>

namespace bible {

namespace {

constexpr char MAGIC[8] = {'C', 'P', 'B', 'I', 'B', 'L', 'E', '\0'};
constexpr uint32_t HEADER_SIZE = 64;
constexpr uint32_t BOOK_ENTRY_SIZE = 64;
constexpr uint32_t CHAPTER_ENTRY_SIZE = 8;
constexpr uint32_t HEADING_ENTRY_SIZE = 12;
// Longest verse in 개역개정 (에 8:9) is under 1 KB; anything far beyond is a corrupt pack.
constexpr uint32_t MAX_TEXT_LEN = 16 * 1024;

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

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
  offText = u32(h + 48);
  textSize = u32(h + 52);
  memcpy(lang, h + 56, 7);
  lang[7] = '\0';
  if (nBooks == 0 || nBooks > 255 || chapterCount == 0 || verseCount == 0) return false;

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
}

bool BiblePack::chapter(int bookIndex, int chapterNumber, Chapter& out) {
  if (bookIndex < 0 || bookIndex >= bookCount()) return false;
  const Book& b = books[bookIndex];
  if (chapterNumber < 1 || chapterNumber > b.chapterCount) return false;
  uint8_t e[CHAPTER_ENTRY_SIZE];
  if (!read(offChapters + (b.firstChapter + chapterNumber - 1) * CHAPTER_ENTRY_SIZE, e, sizeof(e))) return false;
  out.firstVerse = u32(e);
  out.verseCount = u16(e + 4);
  out.firstHeading = u16(e + 6);
  return out.verseCount > 0 && out.firstVerse + out.verseCount <= verseCount;
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

bool BiblePack::verseText(uint32_t verseIndex, std::string& out) {
  out.clear();
  if (verseIndex >= verseCount) return false;
  uint8_t e[8];
  if (!read(offVerses + verseIndex * 4, e, sizeof(e))) return false;
  const uint32_t start = u32(e);
  const uint32_t end = u32(e + 4);
  if (end < start || end > textSize || end - start > MAX_TEXT_LEN) return false;
  out.resize(end - start);
  return end == start || read(offText + start, &out[0], end - start);
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
    const uint32_t off = u32(e + 4);
    const uint16_t len = u16(e + 8);
    if (off + len > textSize) return false;
    Heading h;
    h.verseIndex = verse;
    h.kind = static_cast<HeadingKind>(e[10]);
    h.text.resize(len);
    if (len && !read(offText + off, &h.text[0], len)) return false;
    out.push_back(std::move(h));
  }
  return true;
}

}  // namespace bible
