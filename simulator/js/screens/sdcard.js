/* screen_sd_card.cpp port */

function enterSDCard() {
  releasePanelSprites();
  pendantSdCard.pendingRun = false;
  g_expecting_json = false;
  if (pendantConnected) {
    pendantSdCard.loading = true;
    pendantSdCard.loadFailed = false;
    pendantSdCard.loadStartMs = millis();
    pendantSdCard.fileCount = 0;
    pendantSdCard.totalFiles = 0;
    sdRequestDir("/sd");          // always re-enter at the card root
  }
}
function exitSDCard() {}

function updateSDCardFileList() {
  if (currentPendantScreen !== PSCREEN_SD_CARD) return;
  if (pendantSdCard.loading && pendantSdCard.loadStartMs !== 0 && millis() - pendantSdCard.loadStartMs > 10000) {
    pendantSdCard.loading = false; pendantSdCard.loadFailed = true;
  }
  // Footer first: it sits outside the list panel, so it must track fileCount
  // changes (a listing arriving) independently of the list itself.
  drawSDFooter();

  const ox = 5, oy = 40;
  display.fillRect(5, 40, 230, 200, COLOR_BACKGROUND);
  drawListCrumb(ox, oy, pendantSdCard.curPath || "/sd");

  if (pendantSdCard.loading) {
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(2);
    display.setCursor(ox + 50, oy + 100); display.print("Loading...");
  } else if (pendantSdCard.loadFailed) {
    display.setTextColor(COLOR_ORANGE); display.setTextSize(1);
    display.setCursor(ox + 15, oy + 90); display.print("Couldn't load file list.");
    display.setTextColor(COLOR_GRAY_TEXT);
    display.setCursor(ox + 15, oy + 108); display.print("Tap Refresh to try again.");
  } else if (pendantSdCard.fileCount === 0) {
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(ox + 15, oy + 90); display.print(pendantConnected ? "No GCode files found." : "Not connected.");
    display.setCursor(ox + 15, oy + 108); display.print("Press Refresh to retry.");
  } else {
    for (let i = 0; i < LIST_ROWS && i < pendantSdCard.fileCount; i++) {
      const displayIndex = i + pendantSdCard.scrollOffset;
      if (displayIndex >= pendantSdCard.fileCount) break;
      const dir = !!pendantSdCard.isDir[displayIndex];
      let bg;
      if (displayIndex === pendantSdCard.selectedFile && pendantSdCard.pendingRun) bg = COLOR_DARK_GREEN;
      else if (displayIndex === pendantSdCard.selectedFile) bg = COLOR_BUTTON_ACTIVE;
      else bg = COLOR_BUTTON_GRAY;
      const ry = oy + LIST_ROW_Y0 + i * LIST_ROW_PITCH;
      display.fillRoundRect(ox, ry, 230, LIST_ROW_H, 8, bg);
      // Folders read as navigation, not jobs: blue text and a trailing slash.
      display.setTextColor(dir ? COLOR_CYAN : COLOR_WHITE);
      display.setTextSize(1);
      display.setCursor(ox + 5, ry + 11);
      display.print(sdRowLabel(displayIndex));
    }
  }
}

// Page readout — replaces the old << / >> pair.  Tappable (advances a page,
// wrapping) so the list is still fully reachable by touch alone; dimmed and
// inert when there is only one page.  Geometry matches the Load button directly
// below it (5,282,110) so the two rows line up.
// Own dirty check, mirroring the firmware: the 100 ms tick calls this for free and
// it repaints the moment a listing lands.  force=true after a full redraw, where
// fillScreen() has already wiped it.
let _sdFooterLast = null;
function drawSDFooter(force) {
  const pages = listPageCount(pendantSdCard.fileCount);
  const page  = listPageIndex(pendantSdCard.scrollOffset) + 1;
  const shown = pendantSdCard.fileCount === 0 ? 0 : pages;
  const trunc = pendantSdCard.totalFiles > pendantSdCard.fileCount;
  const live  = pendantSdCard.fileCount > 0 && pages > 1;
  const sig = `${page}/${shown}/${trunc}/${live}`;
  if (!force && _sdFooterLast === sig) return;
  _sdFooterLast = sig;
  drawListFooter(242, page, shown, trunc, live);
}

// Row label — folders get a trailing slash, ".." is spelled out, and long names
// are truncated so they cannot overflow the row (230 px at 6 px/char).
const SD_LABEL_MAX = 35;
function sdRowLabel(i) {
  const name = pendantSdCard.files[i];
  if (pendantSdCard.isDir[i] && name === "..") return ".. (up a folder)";
  const label = name + (pendantSdCard.isDir[i] ? "/" : "");
  return label.length > SD_LABEL_MAX ? label.substring(0, SD_LABEL_MAX - 3) + "..." : label;
}

// Absolute path of a listed entry — "$SD/Run=" needs the full path once files
// can live in subfolders, which is what the old UI's FilePreviewScene sends.
function sdFullPath(i) {
  const base = pendantSdCard.curPath === "/" ? "" : pendantSdCard.curPath;
  return base + "/" + pendantSdCard.files[i];
}

// Ask for a directory listing and park the screen in its loading state.
// A generation counter tags the request so a reply that arrives after we have
// navigated somewhere else can be dropped rather than filling the wrong folder.
function sdRequestDir(path) {
  pendantSdCard.curPath     = path;
  pendantSdCard.dirLevel    = path.split("/").filter(Boolean).length - 1;
  pendantSdCard.listGen     = (pendantSdCard.listGen || 0) + 1;
  pendantSdCard.scrollOffset = 0;
  pendantSdCard.selectedFile = 0;
  pendantSdCard.pendingRun   = false;
  pendantSdCard.loading      = true;
  pendantSdCard.loadFailed   = false;
  pendantSdCard.loadStartMs  = millis();
  drawSDCardScreen();          // bottom row changes with pendingRun
  request_file_list(path);
}

function sdDescend(i) { sdRequestDir(sdFullPath(i)); }

function sdAscend() {
  if (pendantSdCard.dirLevel <= 0) return;
  const p = pendantSdCard.curPath;
  sdRequestDir(p.substring(0, p.lastIndexOf("/")) || "/sd");
}

// Repaint after a page change.  The list area and the page readout are the only
// things that move, and the readout sits BELOW the list sprite (y=242), so
// painting both directly avoids the full-screen fillScreen() that
// drawSDCardScreen() would do — no flicker on a fast spin.
// updateSDCardFileList() refreshes the footer too, mirroring the firmware.
function sdRepaintPaged() { updateSDCardFileList(); }

// Dial — one detent pages the list by one screen, clamped at both ends.
// Does NOT move the selection: Load/Run stay armed on the file you picked, so
// you can page away and still load it.
function handleSDCardDial(delta) {
  const next = listPageStep(delta, pendantSdCard.fileCount, pendantSdCard.scrollOffset);
  if (next < 0) return;              // already at the first/last page
  pendantSdCard.scrollOffset = next;
  sdRepaintPaged();
}

function drawSDCardScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("SD CARD");
  updateSDCardFileList();
  drawSDFooter(true);   // fillScreen() wiped it — force past the dirty check
  if (pendantSdCard.pendingRun) {
    drawButton(5, 282, 110, 36, "Load", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(121, 282, 114, 36, "Run", COLOR_DARK_GREEN, COLOR_WHITE, 2);
  } else {
    drawButton(5, 282, 230, 36, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  }
}

function handleSDCardTouch(x, y) {
  for (let i = 0; i < LIST_ROWS; i++) {
    if (isTouchInBounds(x, y, 5, listRowHitY(i), 230, listRowHitH())) {
      const displayIndex = i + pendantSdCard.scrollOffset;
      if (displayIndex < pendantSdCard.fileCount) {
        if (pendantSdCard.isDir[displayIndex]) {
          // ".." walks back up; any other folder is descended into.
          if (pendantSdCard.files[displayIndex] === "..") sdAscend();
          else                                           sdDescend(displayIndex);
        } else {
          pendantSdCard.selectedFile = displayIndex;
          pendantSdCard.pendingRun = true;
          drawSDCardScreen();
        }
      }
      return;
    }
  }
  // JOG hint and PAGE readout both advance one page, wrapping back to the first.
  if (listFooterPageTap(x, y, 242)) {
    const next = listPageAdvanceWrap(pendantSdCard.fileCount, pendantSdCard.scrollOffset);
    if (next >= 0) { pendantSdCard.scrollOffset = next; sdRepaintPaged(); }
    return;
  }
  if (isTouchInBounds(x, y, rowBtnX(3, 2), 242, rowBtnWAt(3, 2), 36)) {
    if (pendantConnected) {
      pendantSdCard.loading = true; pendantSdCard.loadFailed = false;
      pendantSdCard.loadStartMs = millis();
      sdRequestDir(pendantSdCard.curPath || "/sd");
    }
    return;
  }
  if (pendantSdCard.pendingRun) {
    if (isTouchInBounds(x, y, 5, 282, 110, 36)) {
      pendantSdCard.loadedFile = sdFullPath(pendantSdCard.selectedFile);
      pendantSdCard.pendingRun = false;
      currentPendantScreen = PSCREEN_STATUS;
      return;
    }
    if (isTouchInBounds(x, y, 121, 282, 114, 36)) {
      if (pendantConnected) {
        send_line("$SD/Run=" + sdFullPath(pendantSdCard.selectedFile));
        pendantSdCard.loadedFile = ""; pendantSdCard.pendingRun = false;
        currentPendantScreen = PSCREEN_STATUS;
      }
      return;
    }
  } else {
    if (isTouchInBounds(x, y, 5, 282, 230, 36)) currentPendantScreen = PSCREEN_MAIN_MENU;
  }
}
