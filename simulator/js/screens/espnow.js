/* screen_espnow.cpp port — pairing wizard + machine list (ESPNOW_SPEC §6b/§6c) */

const STEP_Y = 40, STEP_H = 92;
const STAT_Y = 138, STAT_H = 86;
const PACT_Y = 236, PACT_H = 42;
const PNAV_Y = 280, PNAV_H = 38;

// A pairing window that never ends is worse than one that fails — on hardware
// it sat in "Waiting..." from boot with no automatic way out.
const PAIR_TIMEOUT_MS = 90000;
let _pairStartedMs = 0;

function enterEspNowPair() {
  releasePanelSprites();
  _pairStartedMs = millis();
  espnow_start_pairing();
}
function exitEspNowPair() {
  if (!espnow_pairing_complete()) espnow_cancel_pairing();
  releasePanelSprites();
}

function pairView() {
  // Nothing to listen with — say so rather than counting down a wait that
  // cannot succeed.
  if (!espnow_radio_ready()) return "noradio";
  if (espnow_pairing_complete()) return "ok";
  if (simEspNow.pairing === "fail") return "fail";
  if (espnow_is_reconnecting()) return "working";
  // Only the idle wait times out; a handshake in progress is left to finish.
  if (_pairStartedMs && millis() - _pairStartedMs >= PAIR_TIMEOUT_MS) return "fail";
  return "wait";
}

function drawSteps(v) {
  display.fillRoundRect(5, STEP_Y, 230, STEP_H, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, STEP_Y + 3); display.print("ON THE CONTROLLER");
  const steps = [
    ["Open FluidNC console", v !== "wait"],
    ["Run:  $espnow/pair",   v === "ok" || v === "working"],
    ["Keep pendant nearby",  v === "ok"],
  ];
  steps.forEach(([txt, done], i) => {
    const y = STEP_Y + 18 + i * 18;
    display.fillCircle(18, y + 4, 7, done ? PROBE_C_GREEN : COLOR_BUTTON_GRAY);
    display.setTextSize(1); display.setTextColor(COLOR_WHITE);
    display.setCursor(15, y + 1); display.print(done ? "*" : String(i + 1));
    display.setTextColor(done ? COLOR_GRAY_TEXT : COLOR_WHITE);
    display.setCursor(32, y + 1); display.print(txt);
  });
  display.setTextColor(PROBE_C_DIMBLUE);
  display.setCursor(32, STEP_Y + 72); display.print("or WebUI > Settings > ESP-NOW");
}

function drawPairStatus(v) {
  display.fillRoundRect(5, STAT_Y, 230, STAT_H, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, STAT_Y + 3); display.print("STATUS");
  const M = {
    ok:      ["Paired",     PROBE_C_GREEN,  null],
    working: ["Pairing",    PROBE_C_BLUE,   "Exchanging keys (P-256)"],
    fail:    ["Timed out",  PROBE_C_RED,    "No controller responded"],
    noradio: ["Radio down", PROBE_C_RED,    "ESP-NOW failed to start"],
    wait:    ["Waiting...", PROBE_C_YELLOW, "Listening for controller"],
  }[v];
  display.setTextSize(3); display.setTextColor(M[1]);
  display.setCursor(14, STAT_Y + 20); display.print(M[0]);
  display.setTextSize(1); display.setTextColor(COLOR_GRAY_TEXT);
  if (v === "ok") {
    const info = {}; const idx = espnow_active_profile_index();
    if (idx >= 0 && espnow_get_profile(idx, info)) {
      display.setCursor(14, STAT_Y + 54); display.print(info.hostname);
      display.setCursor(14, STAT_Y + 68); display.print(macStr(info.mac, info.channel));
    }
  } else if (M[2]) {
    display.setCursor(14, STAT_Y + 54); display.print(M[2]);
    if (v === "fail") { display.setCursor(14, STAT_Y + 68); display.print("Check FluidNC is v4.0.4+"); }
  }
}

function drawPairActions(v) {
  display.fillRect(5, PACT_Y, 230, PACT_H, COLOR_BACKGROUND);
  if (v === "ok") {
    drawButton(5, PACT_Y, 230, PACT_H, "Use This Machine", PROBE_BTN_GREEN, COLOR_WHITE, 2);
    drawButton(5, PNAV_Y, 230, PNAV_H, "Machines", COLOR_INDIGO, COLOR_WHITE, 2);
  } else if (v === "fail") {
    drawButton(rowBtnX(2,0), PACT_Y, rowBtnWAt(2,0), PACT_H, "Retry", PROBE_BTN_GREEN, COLOR_WHITE, 2);
    drawButton(rowBtnX(2,1), PACT_Y, rowBtnWAt(2,1), PACT_H, "Cancel", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(5, PNAV_Y, 230, PNAV_H, "Back", COLOR_BLUE, COLOR_WHITE, 2);
  } else {
    drawButton(5, PACT_Y, 230, PACT_H, "Cancel", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(5, PNAV_Y, 230, PNAV_H, "Back", COLOR_BLUE, COLOR_WHITE, 2);
  }
}

function drawEspNowPairScreen() {
  const v = pairView();
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("ESP-NOW PAIRING");
  drawSteps(v); drawPairStatus(v); drawPairActions(v);
}

let _lastPairView = "wait";
function updateEspNowPairDisplay() {
  if (currentPendantScreen !== PSCREEN_ESPNOW_PAIR) return;
  const v = pairView();
  drawPairStatus(v);
  if (v !== _lastPairView) { drawSteps(v); drawPairActions(v); _lastPairView = v; }
}

function handleEspNowPairTouch(x, y) {
  const v = pairView();
  if (v === "ok") {
    if (isTouchInBounds(x, y, 5, PACT_Y, 230, PACT_H)) { currentPendantScreen = PSCREEN_CONNECTION; return; }
    if (isTouchInBounds(x, y, 5, PNAV_Y, 230, PNAV_H)) { currentPendantScreen = PSCREEN_ESPNOW_MACHINES; return; }
    return;
  }
  if (v === "fail") {
    if (isTouchInBounds(x, y, rowBtnX(2,0), PACT_Y, rowBtnWAt(2,0), PACT_H)) { _pairStartedMs = millis(); espnow_start_pairing(); drawEspNowPairScreen(); return; }
    if (isTouchInBounds(x, y, rowBtnX(2,1), PACT_Y, rowBtnWAt(2,1), PACT_H)) { espnow_cancel_pairing(); currentPendantScreen = PSCREEN_CONNECTION; return; }
  } else if (isTouchInBounds(x, y, 5, PACT_Y, 230, PACT_H)) {
    espnow_cancel_pairing(); currentPendantScreen = PSCREEN_CONNECTION; return;
  }
  if (isTouchInBounds(x, y, 5, PNAV_Y, 230, PNAV_H)) currentPendantScreen = PSCREEN_CONNECTION;
}

// ===== Machines list =====
const ROW0_Y = 44, MROW_H = 52, MROW_GAP = 4, MACT_Y = 232, MACT_H = 42;
let _selectedRow = -1, _confirmForget = false;

function enterEspNowMachines() {
  releasePanelSprites();
  _selectedRow = espnow_active_profile_index();
  _confirmForget = false;
}
function exitEspNowMachines() { releasePanelSprites(); }
function machineRowY(i) { return ROW0_Y + i * (MROW_H + MROW_GAP); }

function drawMachineRow(i) {
  const info = {};
  if (!espnow_get_profile(i, info)) return;
  const y = machineRowY(i);
  const active = espnow_active_profile_index() === i;
  const picked = _selectedRow === i;
  display.fillRoundRect(5, y, 230, MROW_H, 4, active ? PROBE_SEL_BG : PROBE_BG_PANEL);
  display.drawRoundRect(5, y, 230, MROW_H, 4, active ? PROBE_C_YELLOW : (picked ? COLOR_ORANGE : PROBE_C_TAPBDR));
  display.setTextSize(2); display.setTextColor(active ? PROBE_C_YELLOW : COLOR_WHITE);
  display.setCursor(12, y + 7); display.print(info.hostname);
  display.setTextSize(1); display.setTextColor(COLOR_GRAY_TEXT);
  display.setCursor(12, y + 28); display.print(macStr(info.mac));
  display.setCursor(12, y + 39); display.print("ch" + info.channel);
  if (active) {
    display.setTextColor(PROBE_C_GREEN);
    display.setCursor(44, y + 39); display.print("ACTIVE");
  }
  const live = active && espnow_is_connected();
  const bars = live ? espnow_signal_bars() : 0;
  for (let b = 0; b < 4; b++) {
    const h = 4 + b * 4, bx = 196 + b * 9, by = y + 26 - h;
    display.fillRoundRect(bx, by, 6, h, 1, b < bars ? PROBE_C_GREEN : PROBE_BG_SCREEN);
  }
  display.setTextSize(1);
  display.setTextColor(live ? COLOR_GRAY_TEXT : PROBE_C_DIMBLUE);
  display.setCursor(196, y + 30); display.print(live ? "ok" : "--");
}

function drawEspNowMachinesScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("MACHINES");
  const n = espnow_profile_count();
  for (let i = 0; i < n; i++) drawMachineRow(i);
  if (n === 0) {
    display.fillRoundRect(5, ROW0_Y, 230, MROW_H, 4, PROBE_BG_PANEL);
    display.setTextSize(1); display.setTextColor(COLOR_GRAY_TEXT);
    display.setCursor(14, ROW0_Y + 22); display.print("No machines paired yet");
  }
  display.setTextSize(1); display.setTextColor(PROBE_C_DIMBLUE);
  display.setCursor(8, 216); display.print(`Tap a machine to use it  -  ${n}/5 paired`);
  const full = n >= 5;
  drawButton(rowBtnX(2,0), MACT_Y, rowBtnWAt(2,0), MACT_H, full ? "Full" : "Pair New",
             full ? COLOR_BUTTON_GRAY : PROBE_BTN_TEAL, COLOR_WHITE, 2);
  drawButton(rowBtnX(2,1), MACT_Y, rowBtnWAt(2,1), MACT_H, "Forget", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  drawButton(5, PNAV_Y, 230, PNAV_H, "Back", COLOR_BLUE, COLOR_WHITE, 2);
  if (_confirmForget) probeDrawConfirmOverlay("FORGET MACHINE");
}

function updateEspNowMachinesDisplay() {
  if (currentPendantScreen !== PSCREEN_ESPNOW_MACHINES) return;
  if (_confirmForget) return;
  const n = espnow_profile_count();
  for (let i = 0; i < n; i++) drawMachineRow(i);
}

function handleEspNowMachinesTouch(x, y) {
  if (_confirmForget) {
    if (isTouchInBounds(x, y, 28, 175, 78, 32)) { _confirmForget = false; drawEspNowMachinesScreen(); }
    else if (isTouchInBounds(x, y, 114, 175, 98, 32)) {
      if (_selectedRow >= 0) espnow_remove_profile(_selectedRow);
      _confirmForget = false;
      _selectedRow = espnow_active_profile_index();
      drawEspNowMachinesScreen();
    }
    return;
  }
  const n = espnow_profile_count();
  for (let i = 0; i < n; i++) {
    if (isTouchInBounds(x, y, 5, machineRowY(i), 230, MROW_H)) {
      _selectedRow = i; espnow_select_profile(i); drawEspNowMachinesScreen(); return;
    }
  }
  if (isTouchInBounds(x, y, rowBtnX(2,0), MACT_Y, rowBtnWAt(2,0), MACT_H)) {
    if (n < 5) currentPendantScreen = PSCREEN_ESPNOW_PAIR;
    return;
  }
  if (isTouchInBounds(x, y, rowBtnX(2,1), MACT_Y, rowBtnWAt(2,1), MACT_H)) {
    if (_selectedRow >= 0 && _selectedRow < n) { _confirmForget = true; drawEspNowMachinesScreen(); }
    return;
  }
  if (isTouchInBounds(x, y, 5, PNAV_Y, 230, PNAV_H)) currentPendantScreen = PSCREEN_CONNECTION;
}
