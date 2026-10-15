#pragma once

#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"

// Facts about the open Bible pack: name, edition, copyright or license, source,
// version, notes, size (books, chapters, verses, section titles) and where it was
// loaded from. Side buttons page when it does not fit; Back closes.
class BibleInfoActivity final : public Activity {
 public:
  BibleInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BibleInfo", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  std::vector<std::pair<std::string, std::string>> rows;
  int page = 0;
  int pageCount = 1;
};
