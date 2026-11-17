/* screen_main_menu.cpp port */

function enterMainMenu() { releasePanelSprites(); }
function exitMainMenu() { releasePanelSprites(); }

function drawMainMenu() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("MAIN MENU");
  drawMainMenuNew();
}

// Returns the link-fault to show in the STATUS panel, or null when the radio is
// fine (or not in use) and the panel should report the machine as usual.
// UART has no link to report, so it always returns null.
function espNowLinkFault() {
  if (comms_active_mode() !== "ESPNOW") return null;
  if (!espnow_radio_ready()) {
    return { big: "Radio down", col: PROBE_C_RED, sub: "ESP-NOW failed to start" };
  }
  if (!espnow_has_saved_pairing()) {
    return { big: "Not paired", col: PROBE_C_RED, sub: "Connection > Pair New" };
  }
  if (!espnow_is_connected()) {
    const info = {};
    const idx = espnow_active_profile_index();
    const name = (idx >= 0 && espnow_get_profile(idx, info)) ? info.hostname : "machine";
    return { big: "No link", col: PROBE_C_YELLOW, sub: "Reconnecting to " + name };
  }
  return null;
}

// Same layout and touch rectangles as before; icons on every button, framed
// status panel.
function drawMainMenuNew() {
  uiDrawPanel(5, 40, 230, 65);
  updateMainMenuDisplay();
  const btnY = 115, btnH = 47, btnGap = 52, W = COLOR_WHITE, B = COLOR_BLUE;
  drawButton(5, btnY, 112, btnH, "Jog", B, W, 2, "jog");
  drawMultiLineButton(123, btnY, 112, btnH, "Work", "Area", B, W, 2, "work");
  drawMultiLineButton(5, btnY + btnGap, 112, btnH, "Feeds &", "Speeds", B, W, 2, "gauge");
  drawMultiLineButton(123, btnY + btnGap, 112, btnH, "Spindle", "Control", B, W, 2, "spindle");
  drawButton(5, btnY + btnGap * 2, 112, btnH, "Macros", B, W, 2, "macro");
  drawButton(123, btnY + btnGap * 2, 112, btnH, "SD Card", B, W, 2, "sd");
  drawButton(5, btnY + btnGap * 3, 112, btnH, "Probe", B, W, 2, "probe");
  drawButton(123, btnY + btnGap * 3, 112, btnH, "Status", B, W, 2, "status");
}

// New-style status panel: small light-blue caption, big FreeSansBold value,
// optional small sub-line.
function mainMenuPanelNew(caption, big, bigCol, sub) {
  const g = display, ox = 5, oy = 40, cx = 120, GAP = 7;
  g.fillRect(ox + 2, oy + 2, 226, 61, UI_PANEL_BG);
  // caption (GLCD section-label style, as on every screen) + big value (+ sub
  // line) centred as one block, caps to caps
  g.setFont(null); g.setTextSize(1);
  const [ca, cb] = uiCapBox(g);
  g.setFont(GFX_FONTS.FreeSansBold18pt7b);
  if (g.textWidth(big) > 220) g.setFont(GFX_FONTS.FreeSansBold12pt7b);
  const bigFont = g._font;
  const [ba, bb] = uiCapBox(g);
  const subH = sub ? 7 + 5 : 0;
  const blockH = (cb - ca) + GAP + (bb - ba) + subH;
  let top = Math.round(oy + 65 / 2 - blockH / 2);
  g.setFont(null); g.setTextColor(COLOR_GRAY_TEXT);
  g.setCursor(cx - (g.textWidth(caption) / 2 | 0), top - ca); g.print(caption);
  top += (cb - ca) + GAP;
  g.setFont(bigFont); g.setTextColor(bigCol);
  g.setCursor(cx - (g.textWidth(big) / 2 | 0), top - ba); g.print(big);
  top += (bb - ba) + 5;
  g.setFont(null);
  if (sub) {
    g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(cx - (g.textWidth(sub) / 2 | 0), top); g.print(sub);
  }
}

function updateMainMenuDisplayNew() {
  const statusStr = pendantMachine.status;
  const linkFault = espNowLinkFault();
  if (linkFault) mainMenuPanelNew("ESP-NOW", linkFault.big, linkFault.col, linkFault.sub);
  else if (!pendantSynced || statusStr === "N/C" || statusStr.length === 0)
    mainMenuPanelNew("STATUS", pendantConnected ? "Syncing" : "Connecting", COLOR_ORANGE, null);
  else if (statusStr.startsWith("Alarm"))
    mainMenuPanelNew(alarmDescription(statusStr), "ALARM", TFT_RED, null);
  else mainMenuPanelNew("STATUS", statusStr, COLOR_CYAN, null);
}

function updateMainMenuDisplay() {
  if (currentPendantScreen !== PSCREEN_MAIN_MENU) return;
  updateMainMenuDisplayNew();
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
