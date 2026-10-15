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

void registerBuiltIns(GfxRenderer& renderer, const EpdFontFamily* const families[COUNT]) {
  for (int i = 0; i < COUNT; i++) {
    builtIns[i] = families[i];
    renderer.insertFont(BUILTIN_IDS[i], families[i]);
    renderer.insertFont(IDS[i], families[i]);
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

  // UI: the language's system font with the built-in Noto Sans behind it.
  renderer.clearFontRedirect();
  renderer.removeFont(SYSTEM_FONT_ID);
  if (lang && lang->hasUiFont) {
    const std::string path = firstFontFile(std::string(BibleSession::FONT_DIR) + "/" + lang->code + "/ui");
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
