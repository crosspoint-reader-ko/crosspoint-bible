#pragma once

// The open Bible pack plus the reader's saved position and display preferences.
// One global instance (BIBLE); activities share it instead of reopening the SD file.

#include <BibleBookmarks.h>
#include <BibleLayout.h>
#include <BiblePack.h>
#include <HalStorage.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

class GfxRenderer;

class SdByteSource final : public bible::ByteSource {
 public:
  bool open(const std::string& path);
  void close();
  bool readAt(uint32_t offset, void* dst, uint32_t length) override;

 private:
  HalFile file;
};

// A Bible the reader can open: the pack built into the firmware or a .cpb on the SD card.
struct BibleChoice {
  std::string path;  // BibleSession::BUILT_IN_PATH for the built-in pack
  std::string title;
};

// A language available on this device: the built-in pack's language plus every
// /bible/lang/<code>/ folder holding at least one .cpb (docs/languages.md).
struct LanguageInfo {
  std::string code;                       // "en", "ko", ...
  std::string name;                       // native name (pack metadata language_name)
  std::vector<BibleChoice> bibles;        // sorted by file name, built-in first
  std::vector<std::string> fontFamilies;  // /bible/fonts/<code>/<family>/; empty: built-in fonts
  bool hasUiFont = false;                 // /bible/fonts/<code>/ui/<pt>.epdfont
};

struct BiblePrefs {
  // First-run defaults: 18 pt, normal spacing, verse per line, justified.
  uint8_t fontSize = 2;      // index into BibleFonts (0 = smallest; 2 = 18 pt)
  uint8_t lineSpacing = 1;   // 0 tight, 1 normal, 2 wide
  uint8_t versePerLine = 1;  // 1 = each verse starts a line, 0 = flowing paragraphs
  uint8_t showHeadings = 1;  // section titles and psalm superscriptions
  uint8_t justify = 1;       // 0 = left aligned, 1 = justified with character wrap
};

class BibleSession {
 public:
  static constexpr const char* PACK_DIR = "/bible";
  static constexpr const char* STATE_PATH = "/.crosspoint/bible.bin";
  static constexpr const char* BOOKMARKS_PATH = "/.crosspoint/bible-bookmarks.txt";
  static constexpr int LINE_SPACING_COUNT = 3;

  static constexpr const char* BUILT_IN_PATH = "(built-in)";
  static constexpr const char* LANG_DIR = "/bible/lang";
  static constexpr const char* FONT_DIR = "/bible/fonts";
  static constexpr const char* LANG_STATE_PATH = "/.crosspoint/bible-lang.txt";

  // Opens the current language's chosen Bible (falls back to the language's first Bible,
  // then to the built-in pack). Cheap when already open.
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

  // Languages: scanned from the built-in pack and the SD card on first use.
  const std::vector<LanguageInfo>& languages();
  void rescanLanguages();
  const LanguageInfo* currentLanguage();
  const std::string& languageCode() const { return langCode; }
  // Switch language / Bible / reader font family. Closes the open pack; the caller
  // re-applies fonts (BibleFonts::apply) and re-opens.
  bool setLanguage(const std::string& code);
  void setBible(const std::string& path);
  void setFontFamily(const std::string& family);
  const std::string& chosenBible() { return bibleChoice[langCode]; }
  const std::string& chosenFontFamily() { return fontChoice[langCode]; }
  // Applies the current language: UI language (I18n, by the language code) and fonts.
  void applyLanguage(GfxRenderer& renderer);
  // Pack metadata value, "" when absent.
  std::string meta(const char* key) const;

  bible::Ref position;
  BiblePrefs prefs;
  void loadState();
  void saveState();
  void saveLanguageState();

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

  bool openPath(const std::string& p);
  std::vector<LanguageInfo> langs;
  bool langsScanned = false;
  std::string langCode = "en";
  std::map<std::string, std::string> bibleChoice;  // language code -> Bible path
  std::map<std::string, std::string> fontChoice;   // language code -> font family
};

extern BibleSession BIBLE;
