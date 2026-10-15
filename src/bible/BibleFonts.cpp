#include "BibleFonts.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SdFontFamily.h>

#include "BibleSession.h"
#include "fontIds.h"

namespace BibleFonts {

namespace {
const EpdFontFamily* builtIns[COUNT] = {};
const EpdFontFamily* builtInSans[UI_COUNT] = {};
// Reader slot of the same or nearest size, for UI glyphs only the reader font has.
constexpr int READER_FOR_UI[UI_COUNT] = {0, 1, 3};

// Registers an SD font in `slot`; false (and nothing registered) when the file is missing or bad.
bool insertSd(GfxRenderer& renderer, int slot, const std::string& path) {
  if (!Storage.exists(path.c_str())) return false;
  auto* family = new SdFontFamily(path.c_str());
  if (!family->load()) {
    LOG_ERR("FONT", "Cannot load %s", path.c_str());
    delete family;
    return false;
  }
  renderer.insertSdFont(slot, family);  // takes ownership
  return true;
}

std::string firstFontFile(const std::string& dir) {
  std::string best;
  for (const auto& n : Storage.listFiles(dir.c_str(), 20)) {
    std::string name = n.c_str();
    const size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (name.size() > 8 && name.compare(name.size() - 8, 8, ".epdfont") == 0 && (best.empty() || name < best)) {
      best = name;
    }
  }
  return best.empty() ? "" : dir + "/" + best;
}
}  // namespace

void registerBuiltIns(GfxRenderer& renderer, const EpdFontFamily* const serif[COUNT],
                      const EpdFontFamily* const sans[UI_COUNT]) {
  for (int i = 0; i < COUNT; i++) {
    builtIns[i] = serif[i];
    renderer.insertFont(BUILTIN_IDS[i], serif[i]);
    renderer.insertFont(IDS[i], serif[i]);
  }
  for (int i = 0; i < UI_COUNT; i++) {
    builtInSans[i] = sans[i];
    renderer.insertFont(UI_BUILTIN_IDS[i], sans[i]);
    renderer.insertFont(UI_IDS[i], sans[i]);
  }
}

std::string apply(GfxRenderer& renderer, const LanguageInfo* lang, const std::string& family) {
  std::string used;
  if (lang && !lang->fontFamilies.empty()) {
    used = lang->fontFamilies.front();
    for (const auto& f : lang->fontFamilies) {
      if (f == family) used = f;
    }
  }

  for (int i = 0; i < COUNT; i++) {
    renderer.clearGlyphFallback(IDS[i]);
    bool sd = false;
    if (!used.empty()) {
      const std::string path = std::string(BibleSession::FONT_DIR) + "/" + lang->code + "/" + used + "/" +
                               std::to_string(POINTS[i]) + ".epdfont";
      sd = insertSd(renderer, IDS[i], path);
      // Digits, Latin and Hebrew letters the language font may lack come from Noto Serif.
      if (sd) renderer.setGlyphFallback(IDS[i], BUILTIN_IDS[i]);
    }
    if (!sd) renderer.insertFont(IDS[i], builtIns[i]);
  }

  // Bible screens: the language's system font at each UI size with Noto Sans behind it, or
  // Noto Sans alone. Without a system font, glyphs Noto Sans lacks (Hangul, pre-shaped
  // Hindi/Malayalam clusters in book names) come from the reader font.
  const std::string uiDir = lang ? std::string(BibleSession::FONT_DIR) + "/" + lang->code + "/ui/" : "";
  for (int i = 0; i < UI_COUNT; i++) {
    renderer.clearGlyphFallback(UI_IDS[i]);
    const bool sd =
        lang && lang->hasUiFont && insertSd(renderer, UI_IDS[i], uiDir + std::to_string(UI_POINTS[i]) + ".epdfont");
    if (sd) {
      renderer.setGlyphFallback(UI_IDS[i], UI_BUILTIN_IDS[i]);
    } else {
      renderer.insertFont(UI_IDS[i], builtInSans[i]);
      if (!used.empty()) renderer.setGlyphFallback(UI_IDS[i], IDS[READER_FOR_UI[i]]);
    }
  }

  // Settings: UI_FONT_ID redirected to the system font's 10 pt.
  renderer.clearFontRedirect();
  renderer.removeFont(SYSTEM_FONT_ID);
  if (lang && lang->hasUiFont) {
    std::string path = uiDir + "10.epdfont";
    if (!Storage.exists(path.c_str())) path = firstFontFile(uiDir.substr(0, uiDir.size() - 1));
    if (!path.empty() && insertSd(renderer, SYSTEM_FONT_ID, path)) {
      renderer.setGlyphFallback(SYSTEM_FONT_ID, UI_FONT_ID);
      renderer.setFontRedirect(UI_FONT_ID, SYSTEM_FONT_ID);
    }
  }

  if (auto* fcm = renderer.getFontCacheManager()) fcm->clearCache();
  LOG_INF("FONT", "Fonts for %s: %s%s", lang ? lang->code.c_str() : "-", used.empty() ? "built-in" : used.c_str(),
          lang && lang->hasUiFont ? " + ui" : "");
  return used;
}

}  // namespace BibleFonts
