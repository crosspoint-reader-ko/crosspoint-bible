#include "BibleSession.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "BibleFonts.h"
#include "CrossPointSettings.h"

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

namespace {
// Reads a pack's title and metadata without keeping it open.
bool peekPack(bible::ByteSource& src, std::string& title, std::string& lang, std::string& langName) {
  bible::BiblePack p;
  if (!p.open(&src)) return false;
  title = p.title();
  lang = p.language();
  for (const auto& [k, v] : p.metadata()) {
    if (k == "language_name") langName = v;
  }
  return true;
}

// Entry names of a directory, sorted: folders when `dirs`, else files. (HalStorage::listFiles
// skips folders, and the language and font-family layout is folders.)
std::vector<std::string> listNames(const char* dir, bool dirs = false) {
  std::vector<std::string> out;
  HalFile root = Storage.open(dir);
  if (!root || !root.isDirectory()) return out;
  char name[128];
  for (HalFile f = root.openNextFile(); f && out.size() < 100; f = root.openNextFile()) {
    const bool isDir = f.isDirectory();
    f.getName(name, sizeof(name));
    f.close();
    if (isDir == dirs && name[0] && name[0] != '.') out.emplace_back(name);
  }
  root.close();
  std::sort(out.begin(), out.end());
  return out;
}
}  // namespace

void BibleSession::rescanLanguages() {
  langs.clear();
  langsScanned = true;
  const auto languageFor = [this](const std::string& code) -> LanguageInfo& {
    for (auto& l : langs) {
      if (l.code == code) return l;
    }
    langs.push_back(LanguageInfo{});
    langs.back().code = code;
    return langs.back();
  };
  const auto addBible = [&](const std::string& code, const std::string& name, BibleChoice choice) {
    LanguageInfo& l = languageFor(code.empty() ? "en" : code);
    if (l.name.empty()) l.name = name;
    l.bibles.push_back(std::move(choice));
  };

  // Built-in pack.
  const auto size = static_cast<uint32_t>(bible_pack_end - bible_pack_start);
  std::string builtInTitle;
  if (size > 0) {
    bible::MemorySource mem(bible_pack_start, size);
    std::string lang, langName;
    if (peekPack(mem, builtInTitle, lang, langName)) addBible(lang, langName, {BUILT_IN_PATH, builtInTitle});
  }
  // /bible/lang/<code>/*.cpb
  for (const auto& code : listNames(LANG_DIR, true)) {
    const std::string dir = std::string(LANG_DIR) + "/" + code;
    for (const auto& f : listNames(dir.c_str())) {
      if (!endsWithCpb(f)) continue;
      SdByteSource src;
      std::string title, lang, langName;
      const std::string p = dir + "/" + f;
      // The English bundle carries the built-in pack too, so a card can hold every language;
      // a copy of the built-in pack (same title and size) is not listed twice.
      if (src.open(p) && peekPack(src, title, lang, langName) &&
          !(src.size() == size && size > 0 && title == builtInTitle)) {
        addBible(code, langName, {p, title});
      }
      src.close();
    }
  }
  // Legacy: /bible/*.cpb joins its own language.
  for (const auto& f : listNames(PACK_DIR)) {
    if (!endsWithCpb(f)) continue;
    SdByteSource src;
    std::string title, lang, langName;
    const std::string p = std::string(PACK_DIR) + "/" + f;
    if (src.open(p) && peekPack(src, title, lang, langName)) addBible(lang, langName, {p, title});
    src.close();
  }
  // Fonts: /bible/fonts/<code>/<family>/ and /bible/fonts/<code>/ui/
  for (auto& l : langs) {
    const std::string dir = std::string(FONT_DIR) + "/" + l.code;
    for (const auto& family : listNames(dir.c_str(), true)) {
      if (family == "ui") {
        l.hasUiFont = true;
      } else if (family != "LICENSES") {
        l.fontFamilies.push_back(family);
      }
    }
    if (l.name.empty()) l.name = l.code;
  }
  // English (built-in fonts and UI) first, then by code.
  std::sort(langs.begin(), langs.end(), [](const LanguageInfo& a, const LanguageInfo& b) {
    if ((a.code == "en") != (b.code == "en")) return a.code == "en";
    return a.code < b.code;
  });
  LOG_INF("BIBLE", "%u languages", static_cast<unsigned>(langs.size()));
}

const std::vector<LanguageInfo>& BibleSession::languages() {
  if (!langsScanned) rescanLanguages();
  return langs;
}

const LanguageInfo* BibleSession::currentLanguage() {
  for (const auto& l : languages()) {
    if (l.code == langCode) return &l;
  }
  return languages().empty() ? nullptr : &languages().front();
}

bool BibleSession::setLanguage(const std::string& code) {
  for (const auto& l : languages()) {
    if (l.code != code) continue;
    langCode = code;
    close();
    saveLanguageState();
    return true;
  }
  return false;
}

void BibleSession::setBible(const std::string& p) {
  bibleChoice[langCode] = p;
  close();
  saveLanguageState();
}

void BibleSession::setFontFamily(const std::string& family) {
  fontChoice[langCode] = family;
  saveLanguageState();
}

void BibleSession::applyLanguage(GfxRenderer& renderer) {
  const LanguageInfo* lang = currentLanguage();
  if (lang) langCode = lang->code;
  BibleFonts::apply(renderer, lang, fontChoice[langCode]);
  Language ui = I18n::languageFromBcp47(langCode.c_str());
  if (ui == Language::_COUNT) ui = Language::EN;  // no translation: English UI
  I18N.setLanguage(ui);
  SETTINGS.language = static_cast<uint8_t>(ui);
}

std::string BibleSession::meta(const char* key) const {
  for (const auto& [k, v] : pack.metadata()) {
    if (k == key) return v;
  }
  return "";
}

bool BibleSession::openPath(const std::string& p) {
  if (p == BUILT_IN_PATH) {
    const auto size = static_cast<uint32_t>(bible_pack_end - bible_pack_start);
    if (size == 0) return false;
    builtIn = std::make_unique<bible::MemorySource>(bible_pack_start, size);
    if (pack.open(builtIn.get())) return true;
    builtIn.reset();
    return false;
  }
  if (source.open(p) && pack.open(&source)) return true;
  source.close();
  return false;
}

bool BibleSession::ensureOpen() {
  if (pack.isOpen()) return true;
  const LanguageInfo* lang = currentLanguage();
  if (!lang) return false;
  if (lang->code != langCode) langCode = lang->code;  // saved language no longer installed
  std::vector<std::string> order;
  const std::string& chosen = bibleChoice[langCode];
  if (!chosen.empty()) order.push_back(chosen);
  for (const auto& b : lang->bibles) order.push_back(b.path);
  for (const auto& p : order) {
    if (!openPath(p)) {
      LOG_ERR("BIBLE", "Not a Bible pack: %s", p.c_str());
      continue;
    }
    path = p;
    position = pack.clamp(position);
    LOG_INF("BIBLE", "Opened %s (%s, %s, %u verses)", p.c_str(), pack.title().c_str(), langCode.c_str(),
            static_cast<unsigned>(pack.totalVerses()));
    return true;
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
  // A pack may carry its own formats (pre-shaped scripts whose UI stays English).
  const std::string own = meta(psalm ? "psalm_fmt" : "chapter_fmt");
  if (own.find("%d") != std::string::npos) return format(own.c_str(), chapter);
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

static void loadLanguageLines(const String& text, std::string& langCode, std::map<std::string, std::string>& bibles,
                              std::map<std::string, std::string>& fonts) {
  std::string all(text.c_str(), text.length());
  size_t pos = 0;
  while (pos < all.size()) {
    size_t end = all.find('\n', pos);
    if (end == std::string::npos) end = all.size();
    const std::string line = all.substr(pos, end - pos);
    pos = end + 1;
    const size_t t1 = line.find('\t');
    if (t1 == std::string::npos) continue;
    const std::string key = line.substr(0, t1);
    if (key == "lang") {
      langCode = line.substr(t1 + 1);
      continue;
    }
    const size_t t2 = line.find('\t', t1 + 1);
    if (t2 == std::string::npos) continue;
    const std::string code = line.substr(t1 + 1, t2 - t1 - 1);
    if (key == "bible") bibles[code] = line.substr(t2 + 1);
    if (key == "font") fonts[code] = line.substr(t2 + 1);
  }
}

void BibleSession::loadState() {
  loadLanguageLines(Storage.readFile(LANG_STATE_PATH), langCode, bibleChoice, fontChoice);
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

void BibleSession::saveLanguageState() {
  Storage.mkdir("/.crosspoint");
  std::string text = "lang\t" + langCode + "\n";
  for (const auto& [code, p] : bibleChoice) {
    if (!p.empty()) text += "bible\t" + code + "\t" + p + "\n";
  }
  for (const auto& [code, f] : fontChoice) {
    if (!f.empty()) text += "font\t" + code + "\t" + f + "\n";
  }
  Storage.writeFile(LANG_STATE_PATH, String(text.c_str()));
}
