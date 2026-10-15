#include "BibleSession.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <memory>
#include <cstring>
#include <vector>

#include "BibleFonts.h"

BibleSession BIBLE;

// Pack linked into the firmware by scripts/embed_bible_pack.py (empty when built without one).
extern "C" const uint8_t bible_pack_start[];
extern "C" const uint8_t bible_pack_end[];

namespace {
constexpr char STATE_MAGIC[4] = {'C', 'P', 'B', 'S'};
constexpr uint8_t STATE_VERSION = 1;

bool endsWithCpb(const std::string& name) {
  if (name.size() < 4) return false;
  std::string ext = name.substr(name.size() - 4);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".cpb";
}
}  // namespace

bool SdByteSource::open(const std::string& p) {
  close();
  return Storage.openFileForRead("BIBLE", p, file);
}

void SdByteSource::close() {
  if (file) file.close();
}

bool SdByteSource::readAt(uint32_t offset, void* dst, uint32_t length) {
  if (!file || !file.seek(offset)) return false;
  return file.read(dst, length) == static_cast<int>(length);
}

bool BibleSession::ensureOpen() {
  if (pack.isOpen()) return true;
  std::vector<std::string> candidates;
  for (const auto& name : Storage.listFiles(PACK_DIR, 50)) {
    const std::string n = name.c_str();
    if (!n.empty() && n[0] != '.' && endsWithCpb(n)) candidates.push_back(n);
  }
  std::sort(candidates.begin(), candidates.end());
  for (const auto& n : candidates) {
    const std::string p = std::string(PACK_DIR) + "/" + n;
    if (source.open(p) && pack.open(&source)) {
      path = p;
      position = pack.clamp(position);
      LOG_INF("BIBLE", "Opened %s (%s, %u verses)", p.c_str(), pack.title().c_str(),
              static_cast<unsigned>(pack.totalVerses()));
      return true;
    }
    LOG_ERR("BIBLE", "Not a Bible pack: %s", p.c_str());
    source.close();
  }
  const auto size = static_cast<uint32_t>(bible_pack_end - bible_pack_start);
  if (size > 0) {
    builtIn = std::make_unique<bible::MemorySource>(bible_pack_start, size);
    if (pack.open(builtIn.get())) {
      path = BUILT_IN_PATH;
      position = pack.clamp(position);
      LOG_INF("BIBLE", "Opened built-in pack (%s, %u verses)", pack.title().c_str(),
              static_cast<unsigned>(pack.totalVerses()));
      return true;
    }
    LOG_ERR("BIBLE", "Built-in pack is not readable");
    builtIn.reset();
  }
  return false;
}

void BibleSession::close() {
  pack.close();
  source.close();
  builtIn.reset();
  path.clear();
}

bool BibleSession::loadChapter(int book, int chapter, bible::ChapterContent& out) {
  out = bible::ChapterContent();
  bible::Chapter ch;
  if (!pack.chapter(book, chapter, ch)) return false;
  out.title = chapterTitle(book, chapter);
  out.verses.reserve(ch.verseCount);
  for (uint16_t i = 0; i < ch.verseCount; i++) {
    bible::ChapterVerse v;
    v.number = static_cast<uint16_t>(i + 1);
    v.flags = pack.verseFlags(ch.firstVerse + i);
    if (!pack.verseText(ch.firstVerse + i, v.text)) return false;
    out.verses.push_back(std::move(v));
  }
  if (prefs.showHeadings && pack.chapterHeadings(ch, out.headings)) {
    for (auto& h : out.headings) h.verseIndex -= ch.firstVerse;
  }
  return true;
}

std::string BibleSession::format(const char* fmt, int n) {
  std::string out = fmt;
  const size_t at = out.find("%d");
  if (at != std::string::npos) out.replace(at, 2, std::to_string(n));
  return out;
}

std::string BibleSession::chapterTitle(int book, int chapter) const {
  const bool psalm = book >= 0 && book < pack.bookCount() && isPsalms(pack.book(book));
  return format(psalm ? tr(STR_BIBLE_PSALM_FMT) : tr(STR_BIBLE_CHAPTER_FMT), chapter);
}

std::string BibleSession::chapterCountLabel(int book) const {
  if (book < 0 || book >= pack.bookCount()) return "";
  const bible::Book& b = pack.book(book);
  return format(isPsalms(b) ? tr(STR_BIBLE_PSALM_COUNT_FMT) : tr(STR_BIBLE_CHAPTER_COUNT_FMT), b.chapterCount);
}

std::string BibleSession::chapterLabel(int book, int chapter) const {
  if (book < 0 || book >= pack.bookCount()) return "";
  return std::string(pack.book(book).name) + " " + chapterTitle(book, chapter);
}

void BibleSession::loadState() {
  HalFile f;
  if (!Storage.openFileForRead("BIBLE", STATE_PATH, f)) return;
  uint8_t b[16] = {};
  const int n = f.read(b, sizeof(b));
  f.close();
  if (n < 12 || memcmp(b, STATE_MAGIC, 4) != 0 || b[4] != STATE_VERSION) return;
  position.book = b[5];
  position.chapter = static_cast<uint16_t>(b[6] | (b[7] << 8));
  position.verse = static_cast<uint16_t>(b[8] | (b[9] << 8));
  prefs.fontSize = std::min<uint8_t>(b[10], BibleFonts::COUNT - 1);
  prefs.lineSpacing = std::min<uint8_t>(b[11], LINE_SPACING_COUNT - 1);
  if (n >= 14) {
    prefs.versePerLine = b[12] ? 1 : 0;
    prefs.showHeadings = b[13] ? 1 : 0;
  }
  if (n >= 15) prefs.justify = b[14] ? 1 : 0;
}

void BibleSession::saveState() {
  Storage.mkdir("/.crosspoint");
  HalFile f;
  if (!Storage.openFileForWrite("BIBLE", STATE_PATH, f)) return;
  const uint8_t b[15] = {
      static_cast<uint8_t>(STATE_MAGIC[0]),
      static_cast<uint8_t>(STATE_MAGIC[1]),
      static_cast<uint8_t>(STATE_MAGIC[2]),
      static_cast<uint8_t>(STATE_MAGIC[3]),
      STATE_VERSION,
      position.book,
      static_cast<uint8_t>(position.chapter & 0xFF),
      static_cast<uint8_t>(position.chapter >> 8),
      static_cast<uint8_t>(position.verse & 0xFF),
      static_cast<uint8_t>(position.verse >> 8),
      prefs.fontSize,
      prefs.lineSpacing,
      prefs.versePerLine,
      prefs.showHeadings,
      prefs.justify,
  };
  f.write(b, sizeof(b));
  f.close();
}

bible::BookmarkStore& BibleSession::bookmarks() {
  if (!bookmarksLoaded) {
    bookmarksLoaded = true;
    const String text = Storage.readFile(BOOKMARKS_PATH);
    bookmarkStore.parse(std::string(text.c_str(), text.length()));
    bookmarkStore.ensureDefaultFolder(tr(STR_BIBLE_DEFAULT_FOLDER));
  }
  return bookmarkStore;
}

void BibleSession::saveBookmarks() {
  Storage.mkdir("/.crosspoint");
  const std::string text = bookmarkStore.serialize();
  if (!Storage.writeFile(BOOKMARKS_PATH, String(text.c_str()))) LOG_ERR("BIBLE", "Cannot save bookmarks");
}

std::string BibleSession::referenceLabel(const bible::Bookmark& b) const {
  if (b.book >= pack.bookCount()) return "?";
  return std::string(pack.book(b.book).name) + " " + std::to_string(b.chapter) + ":" + std::to_string(b.verse);
}
