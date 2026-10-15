#pragma once

// The open Bible pack plus the reader's saved position and display preferences.
// One global instance (BIBLE); activities share it instead of reopening the SD file.

#include <BibleBookmarks.h>
#include <BibleLayout.h>
#include <BiblePack.h>
#include <HalStorage.h>

#include <memory>
#include <string>

class SdByteSource final : public bible::ByteSource {
 public:
  bool open(const std::string& path);
  void close();
  bool readAt(uint32_t offset, void* dst, uint32_t length) override;

 private:
  HalFile file;
};

struct BiblePrefs {
  uint8_t fontSize = 2;      // index into BibleFonts (0 = smallest)
  uint8_t lineSpacing = 1;   // 0 tight, 1 normal, 2 wide
  uint8_t versePerLine = 1;  // 1 = each verse starts a line, 0 = flowing paragraphs
  uint8_t showHeadings = 1;  // section titles and psalm superscriptions
  uint8_t justify = 0;       // 0 = left aligned, 1 = justified with character wrap
};

class BibleSession {
 public:
  static constexpr const char* PACK_DIR = "/bible";
  static constexpr const char* STATE_PATH = "/.crosspoint/bible.bin";
  static constexpr const char* BOOKMARKS_PATH = "/.crosspoint/bible-bookmarks.txt";
  static constexpr int LINE_SPACING_COUNT = 3;

  static constexpr const char* BUILT_IN_PATH = "(built-in)";

  // Opens the first .cpb in /bible (sorted by name), else the pack built into the
  // firmware. Cheap when already open.
  bool ensureOpen();
  void close();
  bool isOpen() const { return pack.isOpen(); }
  bible::BiblePack& getPack() { return pack; }
  const std::string& packPath() const { return path; }

  bool loadChapter(int book, int chapter, bible::ChapterContent& out);

  // "요한복음 3장" / "시편 23편"
  std::string chapterLabel(int book, int chapter) const;
  std::string chapterTitle(int book, int chapter) const;
  // "50장" / "50 chapters"
  std::string chapterCountLabel(int book) const;
  // Replace the first %d in a translated string.
  static std::string format(const char* fmt, int n);
  static bool isPsalms(const bible::Book& b) { return b.number == 19; }

  bible::Ref position;
  BiblePrefs prefs;
  void loadState();
  void saveState();

  // Bookmark folders; loaded on first use, saved after every change.
  bible::BookmarkStore& bookmarks();
  void saveBookmarks();
  // "요한복음 3:16"
  std::string referenceLabel(const bible::Bookmark& b) const;

 private:
  SdByteSource source;
  std::unique_ptr<bible::MemorySource> builtIn;
  bible::BookmarkStore bookmarkStore;
  bool bookmarksLoaded = false;
  bible::BiblePack pack;
  std::string path;
};

extern BibleSession BIBLE;
