#pragma once

#include <BibleBookmarks.h>

#include <string>

#include "activities/Activity.h"

// Bookmark folders and their bookmarks.
//   Browse: folders -> bookmarks; Confirm on a bookmark finishes with BibleRefResult.
//   AddTo:  folders; Confirm adds `toAdd` to that folder and finishes with
//           TextResult{folder name} (the reader shows it as a confirmation).
// In both modes the last folder row creates a folder (keyboard), and holding
// Confirm on a folder or bookmark asks before deleting it.
class BibleBookmarksActivity final : public Activity {
 public:
  enum class Mode { Browse, AddTo };

  BibleBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode,
                         bible::Bookmark toAdd = {})
      : Activity("BibleBookmarks", renderer, mappedInput), mode(mode), toAdd(toAdd) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  enum class Level { Folders, Items };

  int rowCount() const;
  void move(int delta);
  void activate();
  void askDelete();
  void deleteConfirmed();
  void createFolder();
  std::string snippet(const bible::Bookmark& b);

  void drawList(int top, int left, int width, int height);
  void drawConfirm(int left, int width, int screenH);

  Mode mode;
  bible::Bookmark toAdd;
  Level level = Level::Folders;
  int folder = 0;     // open folder (Items level)
  int selected = 0;   // row in the current list
  int folderRow = 0;  // selection to restore when leaving a folder
  bool confirmingDelete = false;
  bool skipNextInput = false;
};
