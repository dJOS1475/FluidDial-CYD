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
    pendantSdCard.scrollOffset = 0;
    pendantSdCard.selectedFile = 0;
    request_file_list("/sd");
  }
}
function exitSDCard() {}

function updateSDCardFileList() {
  if (currentPendantScreen !== PSCREEN_SD_CARD) return;
  if (pendantSdCard.loading && pendantSdCard.loadStartMs !== 0 && millis() - pendantSdCard.loadStartMs > 10000) {
    pendantSdCard.loading = false; pendantSdCard.loadFailed = true;
  }
  const ox = 5, oy = 40;
  display.fillRect(5, 40, 230, 200, COLOR_BACKGROUND);

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
    for (let i = 0; i < 5 && i < pendantSdCard.fileCount; i++) {
      const displayIndex = i + pendantSdCard.scrollOffset;
      if (displayIndex >= pendantSdCard.fileCount) break;
      let bg;
      if (displayIndex === pendantSdCard.selectedFile && pendantSdCard.pendingRun) bg = COLOR_DARK_GREEN;
      else if (displayIndex === pendantSdCard.selectedFile) bg = COLOR_BUTTON_ACTIVE;
      else bg = COLOR_BUTTON_GRAY;
      display.fillRoundRect(ox, oy + i * 40, 230, 36, 8, bg);
      display.setTextColor(COLOR_WHITE); display.setTextSize(1);
      display.setCursor(ox + 5, oy + 12 + i * 40); display.print(pendantSdCard.files[displayIndex]);
    }
  }
}

function drawSDCardScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("SD CARD");
  updateSDCardFileList();
  drawButton(5, 242, 72, 36, "<<", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  drawButton(83, 242, 72, 36, "Refresh", COLOR_DARK_GREEN, COLOR_WHITE, 1);
  drawButton(161, 242, 72, 36, ">>", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  if (pendantSdCard.pendingRun) {
    drawButton(5, 282, 110, 36, "Load", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(121, 282, 114, 36, "Run", COLOR_DARK_GREEN, COLOR_WHITE, 2);
  } else {
    drawButton(5, 282, 230, 36, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  }
}

function handleSDCardTouch(x, y) {
  for (let i = 0; i < 5; i++) {
    if (isTouchInBounds(x, y, 5, 40 + i * 40, 230, 36)) {
      const displayIndex = i + pendantSdCard.scrollOffset;
      if (displayIndex < pendantSdCard.fileCount) {
        pendantSdCard.selectedFile = displayIndex;
        pendantSdCard.pendingRun = true;
        drawSDCardScreen();
      }
      return;
    }
  }
  if (isTouchInBounds(x, y, 5, 242, 72, 36)) {
    if (pendantSdCard.scrollOffset > 0) { pendantSdCard.scrollOffset--; updateSDCardFileList(); }
    return;
  }
  if (isTouchInBounds(x, y, 83, 242, 72, 36)) {
    if (pendantConnected) {
      pendantSdCard.loading = true; pendantSdCard.loadFailed = false;
      pendantSdCard.loadStartMs = millis();
      pendantSdCard.fileCount = 0; pendantSdCard.scrollOffset = 0;
      pendantSdCard.selectedFile = 0; pendantSdCard.pendingRun = false;
      updateSDCardFileList(); request_file_list("/sd");
    }
    return;
  }
  if (isTouchInBounds(x, y, 161, 242, 72, 36)) {
    if (pendantSdCard.scrollOffset + 5 < pendantSdCard.fileCount) { pendantSdCard.scrollOffset++; updateSDCardFileList(); }
    return;
  }
  if (pendantSdCard.pendingRun) {
    if (isTouchInBounds(x, y, 5, 282, 110, 36)) {
      pendantSdCard.loadedFile = pendantSdCard.files[pendantSdCard.selectedFile];
      pendantSdCard.pendingRun = false;
      currentPendantScreen = PSCREEN_STATUS;
      return;
    }
    if (isTouchInBounds(x, y, 121, 282, 114, 36)) {
      if (pendantConnected) {
        send_line("$SD/Run=" + pendantSdCard.files[pendantSdCard.selectedFile]);
        pendantSdCard.loadedFile = ""; pendantSdCard.pendingRun = false;
        currentPendantScreen = PSCREEN_STATUS;
      }
      return;
    }
  } else {
    if (isTouchInBounds(x, y, 5, 282, 230, 36)) currentPendantScreen = PSCREEN_MAIN_MENU;
  }
}
