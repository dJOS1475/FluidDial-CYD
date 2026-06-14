/* screen_probe.cpp port — SCR0 probe hub */

const kProbeTypeLabels = ["Z Plate", "XYZ Plate", "3D Probe"];
const kProbeTypeX = [5, 82, 159];
const kProbeTypeW = [74, 74, 76];

function enterProbe() {
  pendantProbeV2.focusedField = -1;
  pendantProbeV2.confirmActive = false;
  pendantProbeV2.dialAccelCount = 0;
}
function exitProbe() {}

function drawProbeTypeRow() {
  for (let i = 0; i < PROBE_TYPE_COUNT; i++) {
    const sel = pendantProbeV2.probeTypeIdx === i;
    drawButton(kProbeTypeX[i], 38, kProbeTypeW[i], 40, kProbeTypeLabels[i],
      sel ? PROBE_C_YELLOW : COLOR_BUTTON_GRAY, COLOR_WHITE, 1);
  }
}

function drawSharedKVPanel() {
  display.fillRoundRect(5, 82, 230, 84, 4, PROBE_BG_PANEL);
  display.setTextSize(1);
  display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 84);
  display.print("SHARED SETTINGS");
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(7, 94, 112, 33, "Probe rate", pendantProbeV2.probeRate, "mm/m", PROBE_C_GREEN, fo === 0, 0);
  probeDrawKVTouch(122, 94, 111, 33, "Seek rate", pendantProbeV2.seekRate, "mm/m", PROBE_C_GREEN, fo === 1, 0);
  probeDrawKVTouch(7, 130, 112, 33, "Retract", pendantProbeV2.retractDist, "mm", PROBE_C_BLUE, fo === 2, 3);
  probeDrawKVTouch(122, 130, 111, 33, "Max Z trvl", pendantProbeV2.maxZTravel, "mm", PROBE_C_RED, fo === 3, 3);
}

function drawRoutineButtons() {
  display.fillRoundRect(5, 170, 230, 104, 4, PROBE_BG_PANEL);
  display.setTextSize(1);
  display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 172);
  display.print("PROBE ROUTINES");
  const n = probeRoutineCount();
  if (n === 1) {
    drawButton(7, 186, 226, 40, "Z Surface", PROBE_BTN_GREEN, COLOR_WHITE, 2);
  } else if (n === 2) {
    // XYZ Plate — two full-width buttons, one per row.
    drawButton(7, 186, 226, 40, "Z Surface", PROBE_BTN_GREEN, COLOR_WHITE, 2);
    drawButton(7, 230, 226, 40, "XYZ Corner", PROBE_BTN_YELLOW, COLOR_WHITE, 2);
  } else {  // n === 4 (3D probe) — 2x2 grid
    drawButton(7, 186, 112, 40, "Z Surf", PROBE_BTN_GREEN, COLOR_WHITE, 2);
    drawButton(121, 186, 112, 40, "XYZ Cnr", PROBE_BTN_YELLOW, COLOR_WHITE, 2);
    drawButton(7, 230, 112, 40, "Bore", PROBE_BTN_BLUE, COLOR_WHITE, 2);
    drawButton(121, 230, 112, 40, "Boss", 0x8010, COLOR_WHITE, 2);
  }
}

function drawProbeScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("PROBE");
  drawProbeTypeRow();
  drawSharedKVPanel();
  drawRoutineButtons();
  // Bottom row — two equal halves: Main Menu | Configure
  drawButton(5, 280, 112, 38, "Main Menu", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(123, 280, 112, 38, "Configure", PROBE_BTN_TEAL, COLOR_WHITE, 2);
}

function handleProbeTouch(x, y) {
  // Bottom row — Main Menu (left half) | Configure (right half)
  if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
    pendantProbeV2.returnScreen = PSCREEN_PROBE;
    currentPendantScreen = probeIs3D() ? PSCREEN_PROBE_CFG_3D : PSCREEN_PROBE_CFG_PLATE;
    return;
  }
  for (let i = 0; i < PROBE_TYPE_COUNT; i++) {
    if (isTouchInBounds(x, y, kProbeTypeX[i], 38, kProbeTypeW[i], 40)) {
      if (pendantProbeV2.probeTypeIdx !== i) { pendantProbeV2.probeTypeIdx = i; drawProbeScreen(); }
      return;
    }
  }
  let redraw = false;
  if (isTouchInBounds(x, y, 7, 94, 112, 33)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0; redraw = true; }
  if (isTouchInBounds(x, y, 122, 94, 111, 33)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; redraw = true; }
  if (isTouchInBounds(x, y, 7, 130, 112, 33)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 2 ? -1 : 2; redraw = true; }
  if (isTouchInBounds(x, y, 122, 130, 111, 33)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 3 ? -1 : 3; redraw = true; }
  if (redraw) { drawSharedKVPanel(); return; }
  const n = probeRoutineCount();
  if (n === 1) {
    if (isTouchInBounds(x, y, 7, 186, 226, 40)) { currentPendantScreen = PSCREEN_PROBE_Z; return; }
  } else if (n === 2) {
    if (isTouchInBounds(x, y, 7, 186, 226, 40)) { currentPendantScreen = PSCREEN_PROBE_Z; return; }
    if (isTouchInBounds(x, y, 7, 230, 226, 40)) { currentPendantScreen = PSCREEN_PROBE_CORNER; return; }
  } else {  // n === 4
    if (isTouchInBounds(x, y, 7, 186, 112, 40)) { currentPendantScreen = PSCREEN_PROBE_Z; return; }
    if (isTouchInBounds(x, y, 121, 186, 112, 40)) { currentPendantScreen = PSCREEN_PROBE_CORNER; return; }
    if (isTouchInBounds(x, y, 7, 230, 112, 40)) { currentPendantScreen = PSCREEN_PROBE_BORE; return; }
    if (isTouchInBounds(x, y, 121, 230, 112, 40)) { currentPendantScreen = PSCREEN_PROBE_BOSS; return; }
  }
  if (y >= 82 && y <= 166) { pendantProbeV2.focusedField = -1; drawSharedKVPanel(); }
}
