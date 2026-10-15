#include "BibleBookmarks.h"

#include <algorithm>
#include <cstdlib>

namespace bible {

std::string BookmarkStore::cleanName(const std::string& name) {
  std::string out;
  for (const char c : name) out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
  const size_t first = out.find_first_not_of(' ');
  if (first == std::string::npos) return "";
  out = out.substr(first, out.find_last_not_of(' ') - first + 1);
  if (out.size() > MAX_NAME_BYTES) {
    // Cut at a character boundary.
    size_t cut = MAX_NAME_BYTES;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;
    out.resize(cut);
  }
  return out;
}

int BookmarkStore::addFolder(const std::string& name) {
  const std::string clean = cleanName(name);
  if (clean.empty() || folders_.size() >= MAX_FOLDERS) return -1;
  folders_.push_back({clean, {}});
  return static_cast<int>(folders_.size()) - 1;
}

bool BookmarkStore::removeFolder(size_t folder) {
  if (folder >= folders_.size()) return false;
  folders_.erase(folders_.begin() + static_cast<long>(folder));
  return true;
}

bool BookmarkStore::add(size_t folder, const Bookmark& b) {
  if (folder >= folders_.size()) return false;
  auto& items = folders_[folder].items;
  items.erase(std::remove(items.begin(), items.end(), b), items.end());
  if (items.size() >= MAX_ITEMS) items.pop_back();
  items.insert(items.begin(), b);
  return true;
}

bool BookmarkStore::remove(size_t folder, size_t item) {
  if (folder >= folders_.size() || item >= folders_[folder].items.size()) return false;
  auto& items = folders_[folder].items;
  items.erase(items.begin() + static_cast<long>(item));
  return true;
}

void BookmarkStore::ensureDefaultFolder(const std::string& defaultName) {
  if (folders_.empty()) addFolder(defaultName);
}

std::string BookmarkStore::serialize() const {
  std::string out;
  for (const auto& f : folders_) {
    out += "F\t" + f.name + "\n";
    for (const auto& b : f.items) {
      out += "B\t" + std::to_string(b.book) + "\t" + std::to_string(b.chapter) + "\t" + std::to_string(b.verse) + "\n";
    }
  }
  return out;
}

void BookmarkStore::parse(const std::string& text) {
  folders_.clear();
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 2 || line[1] != '\t') continue;
    if (line[0] == 'F') {
      if (folders_.size() < MAX_FOLDERS) folders_.push_back({cleanName(line.substr(2)), {}});
    } else if (line[0] == 'B' && !folders_.empty() && folders_.back().items.size() < MAX_ITEMS) {
      const char* p = line.c_str() + 2;
      char* next = nullptr;
      const long book = std::strtol(p, &next, 10);
      if (next == p || *next != '\t') continue;
      p = next + 1;
      const long chapter = std::strtol(p, &next, 10);
      if (next == p || *next != '\t') continue;
      p = next + 1;
      const long verse = std::strtol(p, &next, 10);
      if (next == p) continue;
      if (book < 0 || book > 255 || chapter < 1 || chapter > 0xFFFF || verse < 1 || verse > 0xFFFF) continue;
      folders_.back().items.push_back(
          {static_cast<uint8_t>(book), static_cast<uint16_t>(chapter), static_cast<uint16_t>(verse)});
    }
  }
}

}  // namespace bible
