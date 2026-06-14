/* screen_main_menu.cpp port */

function enterMainMenu() { releasePanelSprites(); }
function exitMainMenu() { releasePanelSprites(); }

function drawMainMenu() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("MAIN MENU");
  display.fillRoundRect(5, 40, 230, 65, 5, COLOR_DARKER_BG);
  updateMainMenuDisplay();

  const btnY = 115, btnH = 47, btnGap = 52;
  drawButton(5, btnY, 112, btnH, "Jog", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, btnY, 112, btnH, "Work Area", COLOR_BLUE, COLOR_WHITE, 2);
  drawMultiLineButton(5, btnY + btnGap, 112, btnH, "Feeds &", "Speeds", COLOR_BLUE, COLOR_WHITE, 2);
  drawMultiLineButton(123, btnY + btnGap, 112, btnH, "Spindle", "Control", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(5, btnY + btnGap * 2, 112, btnH, "Macros", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, btnY + btnGap * 2, 112, btnH, "SD Card", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(5, btnY + btnGap * 3, 112, btnH, "Probe", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, btnY + btnGap * 3, 112, btnH, "Status", COLOR_BLUE, COLOR_WHITE, 2);
}

function updateMainMenuDisplay() {
  if (currentPendantScreen !== PSCREEN_MAIN_MENU) return;
  const statusStr = pendantMachine.status;
  const P = panel(230, 65, 5, 40);
  const g = P.g, ox = P.ox, oy = P.oy;
  g.fillRect(ox, oy, 230, 65, COLOR_DARKER_BG);

  if (!pendantSynced || statusStr === "N/C" || statusStr.length === 0) {
    const phase = pendantConnected ? "Syncing" : "Connecting";
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    let lw = g.textWidth("STATUS");
    g.setCursor(ox + 115 - (lw / 2 | 0), oy + 8); g.print("STATUS");
    g.setTextColor(COLOR_ORANGE); g.setTextSize(3);
    let cw = g.textWidth(phase);
    g.setCursor(ox + 115 - (cw / 2 | 0), oy + 30); g.print(phase);
  } else if (statusStr.startsWith("Alarm")) {
    const desc = alarmDescription(statusStr);
    g.setTextColor(TFT_RED); g.setTextSize(1);
    let dw = g.textWidth(desc);
    g.setCursor(ox + 115 - (dw / 2 | 0), oy + 8); g.print(desc);
    g.setTextSize(4);
    let sw = g.textWidth("ALARM");
    g.setCursor(ox + 115 - (sw / 2 | 0), oy + 26); g.print("ALARM");
  } else {
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    let lw = g.textWidth("STATUS");
    g.setCursor(ox + 115 - (lw / 2 | 0), oy + 8); g.print("STATUS");
    g.setTextColor(COLOR_CYAN); g.setTextSize(4);
    let sw = g.textWidth(statusStr);
    g.setCursor(ox + 115 - (sw / 2 | 0), oy + 26); g.print(statusStr);
  }
}

function handleMainMenuTouch(x, y) {
  const btnY = 115, btnH = 47, btnGap = 52;
  if (isTouchInBounds(x, y, 5, btnY, 112, btnH)) currentPendantScreen = PSCREEN_JOG_HOMING;
  else if (isTouchInBounds(x, y, 123, btnY, 112, btnH)) currentPendantScreen = PSCREEN_PROBING_WORK;
  else if (isTouchInBounds(x, y, 5, btnY + btnGap, 112, btnH)) currentPendantScreen = PSCREEN_FEEDS_SPEEDS;
  else if (isTouchInBounds(x, y, 123, btnY + btnGap, 112, btnH)) currentPendantScreen = PSCREEN_SPINDLE_CONTROL;
  else if (isTouchInBounds(x, y, 5, btnY + btnGap * 2, 112, btnH)) currentPendantScreen = PSCREEN_MACROS;
  else if (isTouchInBounds(x, y, 123, btnY + btnGap * 2, 112, btnH)) currentPendantScreen = PSCREEN_SD_CARD;
  else if (isTouchInBounds(x, y, 5, btnY + btnGap * 3, 112, btnH)) currentPendantScreen = PSCREEN_PROBE;
  else if (isTouchInBounds(x, y, 123, btnY + btnGap * 3, 112, btnH)) currentPendantScreen = PSCREEN_STATUS;
}
