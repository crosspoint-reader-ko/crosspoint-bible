#include "FirmwarePickerActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr size_t NAME_BUFFER_SIZE = 500;
}  // namespace

static void formatFileName(const std::string& filename, char* buffer, const size_t bufferSize) {
  if (filename.empty()) {
    buffer[0] = '\0';
    return;
  }
  const bool isDirectory = filename.back() == '/';
  const size_t dot = isDirectory ? filename.size() - 1 : filename.rfind('.');
  const int length = static_cast<int>(dot == std::string::npos ? filename.size() : dot);
  const char* format = isDirectory && !UITheme::getInstance().getTheme().showsFileIcons() ? "[%.*s]" : "%.*s";
  snprintf(buffer, bufferSize, format, length, filename.c_str());
  // Compose only the display copy; filesystem lookup needs the raw entry bytes.
  utf8ComposeNfcInPlace(buffer);
}

static void formatFileExtension(const std::string& filename, char* buffer, const size_t bufferSize) {
  buffer[0] = '\0';
  if (filename.empty() || filename.back() == '/') return;
  if (const char* extension = strrchr(filename.c_str(), '.')) snprintf(buffer, bufferSize, "%s", extension);
}

FirmwarePickerActivity::FirmwarePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string initialPath)
    : UiListActivity("FirmwarePicker", renderer, mappedInput),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

void FirmwarePickerActivity::loadFiles() {
  files.clear();
  prewarmedStart = -1;  // new folder contents: re-prewarm the visible window

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FWPICK", "fileNameBuffer not allocated");
    root.close();
    return;
  }

  // Size `files` once before the fill loop: doubling growth on a large folder
  // holds the old and new string blocks live across the final realloc. The
  // count-only pass reads no names, so filtered-out entries just over-reserve
  // by a few slots.
  size_t entryCount = 0;
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) ++entryCount;
  root.rewindDirectory();
  files.reserve(entryCount);

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    const bool isDirectory = file.isDirectory();
    if (fileNameBuffer[0] == '.' || strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (isDirectory) {
      files.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      std::string_view filename{fileNameBuffer.get()};
      // Only firmware images are offered.
      if (FsHelpers::checkFileExtension(filename, ".bin")) {
        files.emplace_back(filename);
      }
    }
  }
  root.close();
  FsHelpers::sortFileList(files);
}

// fui::ListProps::rowProvider — formats row `index` from files[index] into the
// activity's scratch buffers on demand. Runs on the render task only for the
// rows the list actually lays out, so nothing per-file is materialized beyond
// `files` itself. The label/value pointers stay valid until the next call,
// which is all the provider contract requires. The "[folder]" format is
// theme-dependent, but it's re-derived here on every
// repaint, so a theme change picked up while this activity was paused
// underneath another screen needs no cache invalidation.
void FirmwarePickerActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<FirmwarePickerActivity*>(ctx);
  if (index >= self->files.size()) return;
  const std::string& entry = self->files[index];
  formatFileName(entry, self->rowNameBuf, sizeof(self->rowNameBuf));
  item.label = self->rowNameBuf;
  formatFileExtension(entry, self->rowExtBuf, sizeof(self->rowExtBuf));
  if (self->rowExtBuf[0] != '\0') {
    item.value = self->rowExtBuf;
  }
  item.icon = listIconFor(UITheme::getFileIcon(entry));
  item.actionValue = static_cast<int16_t>(index);
}

// Batch-prewarm the CJK fallback glyphs for a bounded window of display names
// starting at the viewport top — one SD pass per list page (the reader TOC's
// refreshTocWindow pattern) instead of one unbounded pass over the whole
// folder. Getter form: no concatenated copy (a bare-new string append aborts
// under heap pressure). The last index covers the bottom path band: basepath
// (possibly a CJK folder name) draws in the same small font, so it must live
// in the same batch or it would evict the rows' glyphs when the heap gate
// disables union merging. (prewarmFallbackText appends the truncation
// ellipsis.)
void FirmwarePickerActivity::prewarmRowGlyphs(const int start) {
  const int total = static_cast<int>(files.size());
  int clamped = start;
  if (clamped > total - PREWARM_WINDOW) clamped = total - PREWARM_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == prewarmedStart) return;
  prewarmedStart = clamped;
  const int count = total - clamped < PREWARM_WINDOW ? total - clamped : PREWARM_WINDOW;

  struct PrewarmCtx {
    FirmwarePickerActivity* self;
    int first;
    int count;
  } prewarmCtx{this, clamped, count};
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        auto* c = const_cast<PrewarmCtx*>(static_cast<const PrewarmCtx*>(ctx));
        if (i < static_cast<uint32_t>(c->count)) {
          formatFileName(c->self->files[c->first + i], c->self->rowNameBuf, sizeof(c->self->rowNameBuf));
          return c->self->rowNameBuf;
        }
        return c->self->basepath.c_str();
      },
      &prewarmCtx, static_cast<uint32_t>(count) + 1);
}

void FirmwarePickerActivity::onEnter() {
  UiListActivity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FWPICK", "malloc failed for name buffer");
    return;
  }

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    // The first screen build pulls the viewport to it (ListNav follow-on-build).
    nav.selected = static_cast<int>(findEntry(fileName));
  } else {
    loadFiles();
  }
}

void FirmwarePickerActivity::onExit() {
  Activity::onExit();
  files.clear();
  fileNameBuffer.reset();
}

void FirmwarePickerActivity::activateIndex(const int index) {
  (void)index;  // base already synced nav.selected to the tapped row
  // Activation navigates or opens; a lingering flash would gray an unrelated
  // row on the next list.
  app.clearTapFlash();
  activateSelected();
}

void FirmwarePickerActivity::activateSelected() {
  if (files.empty()) return;
  // A touch activation can carry a row index captured before a delete/reload
  // shrank the list; the next render re-registers the rows.
  if (nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string& entry = files[nav.selected];
  bool isDirectory = (entry.back() == '/');

  // A .bin returns its path to the caller; directories are entered.
  if (!isDirectory) {
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    ActivityResult res{FilePathResult{cleanBasePath + entry}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  // buildScreen() runs on the render task and reads basepath plus `files`
  // through the row provider; mutate only under the render lock.
  {
    RenderLock lock(*this);
    if (basepath.back() != '/') basepath += "/";
    basepath += entry.substr(0, entry.length() - 1);
    loadFiles();
    nav.selected = 0;
    nav.top = 0;
  }
  requestUpdate();
}

bool FirmwarePickerActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Go up one directory, or cancel back to the caller at the root.
    if (basepath != "/") {
      const std::string oldPath = basepath;

      {
        // buildScreen() runs on the render task and reads basepath plus `files`
        // through the row provider; mutate only under the render lock.
        RenderLock lock(*this);
        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        nav.selected = static_cast<int>(findEntry(dirName));
        nav.top = 0;
        nav.follow(listCount());
      }

      requestUpdate();
    } else {
      ActivityResult res;
      res.isCancelled = true;
      setResult(std::move(res));
      finish();
    }
    return true;
  }

  return false;
}

void FirmwarePickerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Full path band at the bottom: separator on top, left-truncated so the
  // deepest directory stays visible.
  {
    const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(pathLineHeight + metrics.verticalSpacing));
    screen.target().fill(fui::Rect{band.x, band.y, band.width, 3}, fui::Paint::solid(fui::Color::Black));
    const int pathY =
        band.y + metrics.verticalSpacing / 2 + (band.height - metrics.verticalSpacing / 2 - pathLineHeight) / 2;
    const int pathMaxWidth = band.width - metrics.contentSidePadding * 2;
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, band.x + metrics.contentSidePadding, pathY, pathDisplay);
  }

  if (files.empty()) {
    screen.centeredText(tr(STR_NO_BIN_FILES), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.rowProvider = &FirmwarePickerActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(files.size());
  props.action = ACTION_ROW;
  // Tap selects/navigates (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;  // air between the extension and the row edge
  // Names use up to two small-font lines; shared list layout sizes each row.
  fui::TextStyle label = screen.theme().smallText;
  label.maxLines = 2;
  props.labelText = label;

  // The trailing value here is just the short extension: skip the balanced
  // 60%-band wrap cap and let both name lines run the full width before it.
  props.balanceWrappedLabelWithValue = false;
  props = screen.resolveListProps(props);
  const int16_t twoLineRowHeight = static_cast<int16_t>(
      screen.target().lineHeight(props.labelText.font) * props.labelText.maxLines + 2 * props.rowPaddingY);
  props.rowHeight = std::max(props.rowHeight, twoLineRowHeight);
  syncListViewport(screen, props);
  // Prewarm the window at the final viewport (syncListViewport just applied
  // follow/clamping to nav.top) before the list resolves rows through the
  // provider.
  prewarmRowGlyphs(nav.top);
  screen.list(props);
}

void FirmwarePickerActivity::drawChrome() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SELECT_FIRMWARE_FILE));
}

void FirmwarePickerActivity::drawFooter() {
  // Confirm on a .bin returns the path to the caller; directories still descend.
  const bool selectingFile =
      !files.empty() && nav.selected >= 0 && nav.selected < listCount() && files[nav.selected].back() != '/';
  const char* confirmLabel = files.empty() ? "" : (selectingFile ? tr(STR_SELECT) : tr(STR_OPEN));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

size_t FirmwarePickerActivity::findEntry(const std::string& name) const {
  const auto entry = std::find(files.begin(), files.end(), name);
  return entry != files.end() ? static_cast<size_t>(entry - files.begin()) : 0;
}
