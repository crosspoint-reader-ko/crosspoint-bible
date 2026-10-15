#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// SD card browser that lists folders and .bin files and returns the chosen firmware
// path through FilePathResult (cancelled when Back leaves the root). Used by the SD
// firmware update, which is the only update path in CrossPoint Bible.
class FirmwarePickerActivity final : public UiListActivity {
 private:
  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  // Pull-based rows: the SDK list resolves each drawn row on demand through
  // provideRow() (fui::ListProps::rowProvider), so the only per-file
  // residency is `files` itself — no full-length rowNames/rowExtensions/
  // rowItems arrays (a 1000-file folder used to pin ~100KB of vectors plus a
  // heap copy of every display name, which aborted under -fno-exceptions
  // when the contiguous blocks no longer fit). The label/value strings for
  // the row being laid out live in these scratch buffers; the provider
  // contract only needs them valid until the next provideRow() call.
  static constexpr size_t ROW_NAME_BUF_SIZE = 512;  // NAME_BUFFER_SIZE + "[]" + terminator slack
  char rowNameBuf[ROW_NAME_BUF_SIZE]{};
  char rowExtBuf[16]{};
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  // CJK fallback glyphs are prewarmed for a bounded window of rows around the
  // viewport (one SD pass per list page, like the reader TOC) instead of the
  // whole folder. -1 = nothing prewarmed; reset by loadFiles().
  static constexpr int PREWARM_WINDOW = 24;
  int prewarmedStart = -1;
  void prewarmRowGlyphs(int start);

  int listCount() const override { return static_cast<int>(files.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Short Back goes up a directory (cancel at root); Confirm activates on release.
  bool handleButtons() override;
  // Header shows the picker title (battery indicator via GUI.drawHeader);
  // footer labels depend on path depth and the selected entry.
  void drawChrome() override;
  void drawFooter() override;
  void activateSelected();

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FirmwarePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  std::string initialPath = "/");
  void onEnter() override;
  void onExit() override;
};
