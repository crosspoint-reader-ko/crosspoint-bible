#pragma once

#include "activities/Activity.h"

// Language list: English (built in) plus every language bundle on the SD card. Choosing one
// switches the UI language, the Bible and the fonts together (BibleSession::setLanguage,
// applyLanguage). Rows show the English name (the built-in font always has it) and the
// native name, which appears once the language's own UI font is loaded.
class BibleLanguageActivity final : public Activity {
 public:
  BibleLanguageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BibleLanguage", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  // English name for a language code ("ko" -> "Korean"); the code itself when unknown.
  static const char* englishName(const std::string& code);

 private:
  int selected = 0;
};
