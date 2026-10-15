#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

// On-screen keyboard with two-set (두벌식) Hangul composition and an English/number
// layout. Arrow buttons move over the keys, Confirm presses one. Back deletes one
// jamo, or cancels when the text is empty; holding Back always cancels.
// Finishes with TextResult{text}.
class BibleTextInputActivity final : public Activity {
 public:
  BibleTextInputActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                         size_t maxBytes = 60)
      : Activity("BibleTextInput", renderer, mappedInput), title(std::move(title)), maxBytes(maxBytes) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  enum class Action : uint8_t { Char, Shift, Lang, Space, Delete, Done };
  struct Key {
    const char* label;  // nullptr: the label is the character itself
    uint32_t cp;
    Action action;
    uint8_t units;  // relative width in its row
  };
  using Row = std::vector<Key>;

  const std::vector<Row>& layout() const;
  const Key& selectedKey() const;
  void moveColumn(int delta);
  void moveRow(int delta);
  void press(const Key& key);
  void finishWithText();
  void cancel();
  std::string text() const;

  std::string title;
  size_t maxBytes;
  std::vector<uint32_t> typed;
  bool hangulMode = true;
  bool shift = false;
  int row = 1;
  int col = 0;
};
