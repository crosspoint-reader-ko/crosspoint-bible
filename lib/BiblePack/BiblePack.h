#pragma once

// Reader for CrossPoint Bible packs (.cpb), written by tools/bible/epub2cpb.py.
// Format: docs/bible-pack-format.md. All indices here are 0-based unless named
// "chapter"/"verse" (those are the 1-based numbers people read).

#include <cstdint>
#include <string>
#include <vector>

namespace bible {

// Random-access byte source, so the pack can be read from an SD file on the
// device and from a std::ifstream in host tests.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual bool readAt(uint32_t offset, void* dst, uint32_t length) = 0;
};

enum VerseFlag : uint8_t {
  VERSE_PARAGRAPH = 0x01,  // verse starts a paragraph
  VERSE_POETRY = 0x02,     // verse is set as poetry lines
};

enum class HeadingKind : uint8_t {
  Section = 0,     // section title above a verse
  PsalmTitle = 1,  // psalm superscription, e.g. [다윗의 시]
  PsalmBook = 2,   // psalter division, e.g. 제 일 권
};

struct Ref {
  uint8_t book = 0;      // 0-based book index (0 = Genesis)
  uint16_t chapter = 1;  // 1-based
  uint16_t verse = 1;    // 1-based
};

struct Book {
  uint8_t testament = 0;  // 0 = Old, 1 = New
  uint8_t number = 0;     // 1..66 canonical number
  uint16_t chapterCount = 0;
  uint32_t firstChapter = 0;  // index into the chapter table
  char name[40] = {};         // UTF-8, NUL-terminated
  char abbr[16] = {};
};

struct Chapter {
  uint32_t firstVerse = 0;  // global verse index
  uint16_t verseCount = 0;
  uint16_t firstHeading = 0;  // first heading whose verse index >= firstVerse
};

struct Heading {
  uint32_t verseIndex = 0;  // the heading sits directly above this verse
  HeadingKind kind = HeadingKind::Section;
  std::string text;
};

class BiblePack {
 public:
  static constexpr uint16_t FORMAT_VERSION = 1;

  // Reads and validates the header and book table. The source must outlive the pack.
  bool open(ByteSource* source);
  void close();
  bool isOpen() const { return src != nullptr; }

  const std::string& title() const { return packTitle; }
  const char* language() const { return lang; }
  int bookCount() const { return static_cast<int>(books.size()); }
  const Book& book(int index) const { return books[index]; }
  uint32_t totalVerses() const { return verseCount; }

  bool chapter(int bookIndex, int chapterNumber, Chapter& out);
  int verseCountOf(int bookIndex, int chapterNumber);

  // Global verse index for a reference, or -1 if it does not exist.
  int32_t verseIndex(const Ref& ref);
  // Clamp a reference into range (book, then chapter, then verse).
  Ref clamp(Ref ref);

  bool verseText(uint32_t verseIndex, std::string& out);
  uint8_t verseFlags(uint32_t verseIndex);
  // Headings above verses [firstVerse, firstVerse + count) of the given chapter.
  bool chapterHeadings(const Chapter& ch, std::vector<Heading>& out);

 private:
  bool read(uint32_t offset, void* dst, uint32_t length) { return src && src->readAt(offset, dst, length); }

  ByteSource* src = nullptr;
  std::vector<Book> books;
  std::string packTitle;
  char lang[8] = {};
  uint32_t chapterCount = 0;
  uint32_t verseCount = 0;
  uint32_t headingCount = 0;
  uint32_t offChapters = 0;
  uint32_t offVerses = 0;
  uint32_t offFlags = 0;
  uint32_t offHeadings = 0;
  uint32_t offText = 0;
  uint32_t textSize = 0;
};

}  // namespace bible
