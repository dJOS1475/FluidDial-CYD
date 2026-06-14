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
  const ox = 5, oy = 40;
  display.fillRect(5, 40, 230, 200, COLOR_BACKGROUND);

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
      display.fillRoundRect(ox, oy + i * 40, 230, 36, 8, bg);
      display.setTextColor(COLOR_WHITE); display.setTextSize(1);
      display.setCursor(ox + 5, oy + 12 + i * 40); display.print(macroLabel(displayIndex));
    }
  }
}

function drawMacrosScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("MACROS");
  updateMacrosFileList();
  drawButton(5, 242, 72, 36, "<<", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  drawButton(83, 242, 72, 36, "Refresh", COLOR_DARK_GREEN, COLOR_WHITE, 1);
  drawButton(161, 242, 72, 36, ">>", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  if (pendantMacros.pendingRun) {
    drawButton(5, 282, 110, 36, "Cancel", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(121, 282, 114, 36, "Run", COLOR_DARK_GREEN, COLOR_WHITE, 2);
  } else {
    drawButton(5, 282, 230, 36, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  }
}

function handleMacrosTouch(x, y) {
  for (let i = 0; i < 5; i++) {
    if (isTouchInBounds(x, y, 5, 40 + i * 40, 230, 36)) {
      const displayIndex = i + pendantMacros.scrollOffset;
      if (displayIndex < pendantMacros.count) {
        pendantMacros.selected = displayIndex;
        pendantMacros.pendingRun = true;
        drawMacrosScreen();
      }
      return;
    }
  }
  if (isTouchInBounds(x, y, 5, 242, 72, 36)) {
    if (pendantMacros.scrollOffset > 0) { pendantMacros.scrollOffset--; updateMacrosFileList(); }
    return;
  }
  if (isTouchInBounds(x, y, 83, 242, 72, 36)) {
    if (pendantConnected) { _refreshMacros(); drawMacrosScreen(); }
    return;
  }
  if (isTouchInBounds(x, y, 161, 242, 72, 36)) {
    if (pendantMacros.scrollOffset + 5 < pendantMacros.count) { pendantMacros.scrollOffset++; updateMacrosFileList(); }
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
