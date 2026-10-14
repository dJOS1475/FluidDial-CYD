/* screen_macros.cpp port */

function enterMacros() {
  releasePanelSprites();
  pendantMacros.scrollOffset = 0;
  pendantMacros.selected = -1;
  pendantMacros.pendingRun = false;
  if (pendantMacros.cacheValid) {
    pendantMacros.loading = false; pendantMacros.loadFailed = false;
  } else {
    pendantMacros.loading = true; pendantMacros.loadFailed = false; pendantMacros.count = 0;
    if (pendantConnected) requestMacros();
  }
}
function exitMacros() {}

function _refreshMacros() {
  pendantMacros.cacheValid = false;
  pendantMacros.scrollOffset = 0;
  pendantMacros.selected = -1;
  pendantMacros.pendingRun = false;
  pendantMacros.loading = true;
  pendantMacros.count = 0;
  if (pendantConnected) requestMacros();
}

function macroLabel(displayIndex) {
  let label = pendantMacros.content[displayIndex];
  if (label.length > 36) label = label.substring(0, 33) + "...";
  return label;
}

function updateMacrosFileList() {
  if (currentPendantScreen !== PSCREEN_MACROS) return;
  if (pendantMacros.loading && pendantMacros.loadStartMs !== 0 && millis() - pendantMacros.loadStartMs > 35000) {
    pendantMacros.loading = false; pendantMacros.loadFailed = true;
  }
  // Footer first: it sits outside the list panel, so it must track count changes
  // (a macro list arriving) independently of the list itself.
  drawMacrosFooter();

  const ox = 5, oy = 40;
  display.fillRect(5, 40, 230, 200, COLOR_BACKGROUND);
  drawListCrumb(ox, oy, pendantMacros.sourceFile || "macrocfg.json");

  if (pendantMacros.loading) {
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(2);
    display.setCursor(ox + 45, oy + 100); display.print(pendantConnected ? "Loading..." : "Not connected.");
  } else if (pendantMacros.loadFailed) {
    display.setTextColor(COLOR_ORANGE); display.setTextSize(1);
    display.setCursor(ox + 15, oy + 90); display.print("Couldn't load macros.");
    display.setTextColor(COLOR_GRAY_TEXT);
    display.setCursor(ox + 15, oy + 108); display.print("Tap Refresh to try again.");
  } else if (pendantMacros.count === 0) {
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(ox + 15, oy + 90); display.print("No macros found.");
    display.setCursor(ox + 15, oy + 108); display.print("Add macros in FluidNC preferences.");
  } else {
    for (let i = 0; i < 5 && i < pendantMacros.count; i++) {
      const displayIndex = i + pendantMacros.scrollOffset;
      if (displayIndex >= pendantMacros.count) break;
      let bg;
      if (displayIndex === pendantMacros.selected && pendantMacros.pendingRun) bg = COLOR_DARK_GREEN;
      else if (displayIndex === pendantMacros.selected) bg = COLOR_BUTTON_ACTIVE;
      else bg = COLOR_BUTTON_GRAY;
      const ry = oy + LIST_ROW_Y0 + i * LIST_ROW_PITCH;
      display.fillRoundRect(ox, ry, 230, LIST_ROW_H, 8, bg);
      display.setTextColor(COLOR_WHITE); display.setTextSize(1);
      display.setCursor(ox + 5, ry + 11); display.print(macroLabel(displayIndex));
    }
  }
}

// Dial — pages the macro list one screen at a time, clamped.  The << / >> buttons
// still step a single row; the dial is the fast way through a long list.
function handleMacrosDial(delta) {
  const next = listPageStep(delta, pendantMacros.count, pendantMacros.scrollOffset);
  if (next < 0) return;
  pendantMacros.scrollOffset = next;
  updateMacrosFileList();
  drawMacrosFooter();
}

// Own dirty check, mirroring the firmware — see drawSDFooter().
let _macrosFooterLast = null;
function drawMacrosFooter(force) {
  const pages = listPageCount(pendantMacros.count);
  const page  = listPageIndex(pendantMacros.scrollOffset) + 1;
  const shown = pendantMacros.count === 0 ? 0 : pages;
  const live  = pendantMacros.count > 0 && pages > 1;
  const sig = `${page}/${shown}/${live}`;
  if (!force && _macrosFooterLast === sig) return;
  _macrosFooterLast = sig;
  drawListFooter(242, page, shown, false, live);
}

function drawMacrosScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("MACROS");
  updateMacrosFileList();
  drawMacrosFooter(true);   // fillScreen() wiped it — force past the dirty check
  if (pendantMacros.pendingRun) {
    drawButton(5, 282, 110, 36, "Cancel", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(121, 282, 114, 36, "Run", COLOR_DARK_GREEN, COLOR_WHITE, 2);
  } else {
    drawButton(5, 282, 230, 36, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  }
}

function handleMacrosTouch(x, y) {
  for (let i = 0; i < 5; i++) {
    if (isTouchInBounds(x, y, 5, listRowHitY(i), 230, listRowHitH())) {
      const displayIndex = i + pendantMacros.scrollOffset;
      if (displayIndex < pendantMacros.count) {
        pendantMacros.selected = displayIndex;
        pendantMacros.pendingRun = true;
        drawMacrosScreen();
      }
      return;
    }
  }
  // JOG hint and PAGE readout both advance one page, wrapping back to the first.
  if (listFooterPageTap(x, y, 242)) {
    const next = listPageAdvanceWrap(pendantMacros.count, pendantMacros.scrollOffset);
    if (next >= 0) { pendantMacros.scrollOffset = next; updateMacrosFileList(); }
    return;
  }
  if (isTouchInBounds(x, y, rowBtnX(3, 2), 242, rowBtnWAt(3, 2), 36)) {
    if (pendantConnected) { _refreshMacros(); drawMacrosScreen(); }
    return;
  }
  if (pendantMacros.pendingRun) {
    if (isTouchInBounds(x, y, 5, 282, 110, 36)) { pendantMacros.pendingRun = false; drawMacrosScreen(); return; }
    if (isTouchInBounds(x, y, 121, 282, 114, 36)) {
      if (pendantConnected && pendantMacros.selected >= 0) {
        const fn = pendantMacros.filename[pendantMacros.selected];
        let cmd;
        if (fn.startsWith("/sd/")) cmd = "$SD/Run=" + fn.substring(4);
        else if (fn.startsWith("/localfs/")) cmd = "$Localfs/Run=" + fn.substring(9);
        else if (fn.startsWith("cmd:")) cmd = fn.substring(4);
        else cmd = fn;
        send_line(cmd);
        pendantMacros.pendingRun = false;
        currentPendantScreen = PSCREEN_STATUS;
      }
      return;
    }
  } else {
    if (isTouchInBounds(x, y, 5, 282, 230, 36)) currentPendantScreen = PSCREEN_MAIN_MENU;
  }
}
