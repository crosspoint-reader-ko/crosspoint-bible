#pragma once

// Bookmarks sorted into folders the reader names. Plain C++ so it runs in host
// tests; BibleSession reads and writes the file (/.crosspoint/bible-bookmarks.txt).
//
// File format, one record per line (UTF-8, tab-separated):
//   F<TAB>folder name
//   B<TAB>book<TAB>chapter<TAB>verse     (belongs to the folder above; book is 0-based)
// Unknown lines are skipped, so later versions can add record types.

#include <cstdint>
#include <string>
#include <vector>

namespace bible {

struct Bookmark {
  uint8_t book = 0;
  uint16_t chapter = 1;
  uint16_t verse = 1;
  bool operator==(const Bookmark& o) const { return book == o.book && chapter == o.chapter && verse == o.verse; }
};

struct BookmarkFolder {
  std::string name;
  std::vector<Bookmark> items;
};

class BookmarkStore {
 public:
  static constexpr size_t MAX_FOLDERS = 50;
  static constexpr size_t MAX_ITEMS = 500;  // per folder
  static constexpr size_t MAX_NAME_BYTES = 60;

  const std::vector<BookmarkFolder>& folders() const { return folders_; }
  size_t folderCount() const { return folders_.size(); }

  // Returns the new folder's index, or -1 when full or the name is empty after cleaning.
  int addFolder(const std::string& name);
  bool removeFolder(size_t folder);
  // Adds to the folder (newest first). A bookmark already in the folder moves to the top.
  bool add(size_t folder, const Bookmark& b);
  bool remove(size_t folder, size_t item);
  // Ensures at least one folder exists, named defaultName.
  void ensureDefaultFolder(const std::string& defaultName);

  std::string serialize() const;
  void parse(const std::string& text);

  // Folder names are one line of text without tabs, trimmed and capped at MAX_NAME_BYTES.
  static std::string cleanName(const std::string& name);

 private:
  std::vector<BookmarkFolder> folders_;
};

}  // namespace bible
