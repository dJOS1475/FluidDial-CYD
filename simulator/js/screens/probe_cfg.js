/* screen_probe_cfg.cpp port — SCR0a 3D config / SCR0b plate config */

// ===== Probe-type illustrations =====
const kGfxCX = 120;

function drawProbe3DGraphic() {
  const cx = kGfxCX;
  display.fillRoundRect(5, 170, 230, 100, 4, PROBE_BG_PANEL);
  display.fillRect(cx - 7, 182, 14, 10, PROBE_C_DIMBLUE);
  display.fillRoundRect(cx - 17, 192, 34, 22, 4, COLOR_GRAY_TEXT);
  display.drawFastVLine(cx - 1, 214, 30, PROBE_C_LBLUE);
  display.drawFastVLine(cx, 214, 30, PROBE_C_LBLUE);
  display.drawFastVLine(cx + 1, 214, 30, PROBE_C_LBLUE);
  display.fillCircle(cx, 248, 5, PROBE_C_RED);
  display.drawFastHLine(cx - 50, 256, 100, COLOR_GRAY_TEXT);
  for (let i = 0; i < 7; i++)
    display.drawLine(cx - 46 + i * 14, 256, cx - 52 + i * 14, 262, PROBE_C_DIMBLUE);
}

function drawPlateZGraphic() {
  const cx = kGfxCX;
  display.fillRoundRect(5, 128, 230, 142, 4, PROBE_BG_PANEL);
  display.fillRect(cx - 45, 206, 90, 32, COLOR_BUTTON_GRAY);
  display.fillRect(cx - 55, 194, 110, 12, PROBE_C_BLUE);
  display.fillRect(cx - 7, 164, 14, 30, COLOR_GRAY_TEXT);
}

function drawPlateXYZGraphic() {
  const cx = kGfxCX;
  display.fillRoundRect(5, 170, 230, 100, 4, PROBE_BG_PANEL);
  display.fillRect(cx - 28, 213, 90, 43, COLOR_BUTTON_GRAY);
  display.fillRect(cx - 40, 203, 78, 10, PROBE_C_BLUE);
  display.fillRect(cx - 40, 203, 10, 53, PROBE_C_BLUE);
  display.fillRect(cx - 10, 180, 12, 23, COLOR_GRAY_TEXT);
}

// ===== 3D PROBE CONFIG =====
function enterProbeCfg3D() {
  pendantProbeV2.focusedField = 0;
  pendantProbeV2.confirmActive = false;
  pendantProbeV2.dialAccelCount = 0;
}
function exitProbeCfg3D() {}

function drawProbeCfg3DScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("PROBE CONFIG");
  display.fillRoundRect(5, 38, 230, 26, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 41); display.print("PROBE HARDWARE");
  display.setTextColor(PROBE_C_GREEN);
  display.setCursor(10, 53); display.print("3D Touch Probe");

  // Ball dia. is the trigger offset; Deflection (default 0) is subtracted from
  // the ball radius to correct for stylus flex — optional accuracy tune.
  display.fillRoundRect(5, 67, 230, 55, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 70); display.print("STYLUS");
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(7, 79, 112, 40, "Ball dia.", pendantProbeV2.ballDia, "mm", PROBE_C_BLUE, fo === 0, 3);
  probeDrawKVTouch(122, 79, 111, 40, "Deflection", pendantProbeV2.deflection, "mm", PROBE_C_BLUE, fo === 1, 3);

  drawProbe3DGraphic();

  drawButton(5, 280, 112, 40, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(123, 280, 112, 40, "Save", COLOR_DARK_GREEN, COLOR_GREEN, 2);
}

function handleProbeCfg3DTouch(x, y) {
  let redraw = false;
  if (isTouchInBounds(x, y, 7, 79, 112, 40)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0; redraw = true; }
  if (isTouchInBounds(x, y, 122, 79, 111, 40)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; redraw = true; }
  if (redraw) { drawProbeCfg3DScreen(); return; }
  if (isTouchInBounds(x, y, 5, 280, 112, 40)) { currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 122, 280, 113, 40)) { saveProbeSettings(); currentPendantScreen = PSCREEN_PROBE; return; }
}

// ===== TOUCH PLATE CONFIG =====
function enterProbeCfgPlate() {
  pendantProbeV2.focusedField = 0;
  pendantProbeV2.confirmActive = false;
  pendantProbeV2.dialAccelCount = 0;
}
function exitProbeCfgPlate() {}

function drawProbeCfgPlateScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("PROBE CONFIG");
  const xyz = pendantProbeV2.probeTypeIdx === PROBE_TYPE_XYZPLATE;

  display.fillRoundRect(5, 38, 230, 26, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 41); display.print("PROBE TYPE");
  display.setTextColor(PROBE_C_GREEN);
  display.setCursor(10, 53); display.print(xyz ? "XYZ Touch Plate" : "Z-Height Touch Plate");

  const panelH = xyz ? 98 : 55;
  display.fillRoundRect(5, 67, 230, panelH, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 70); display.print(xyz ? "PLATE DIMENSIONS" : "PLATE THICKNESS");
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(7, 79, 112, 40, "Thickness", pendantProbeV2.plateThick, "mm", PROBE_C_BLUE, fo === 0, 3);
  if (xyz) {
    probeDrawKVTouch(122, 79, 111, 40, "Width", pendantProbeV2.plateWidth, "mm", PROBE_C_DIMBLUE, fo === 1, 3);
    probeDrawKVTouch(7, 122, 112, 40, "XY offset X", pendantProbeV2.plateOffX, "mm", PROBE_C_BLUE, fo === 2, 3);
    probeDrawKVTouch(122, 122, 111, 40, "XY offset Y", pendantProbeV2.plateOffY, "mm", PROBE_C_BLUE, fo === 3, 3);
  }
  if (xyz) drawPlateXYZGraphic();
  else drawPlateZGraphic();
  drawButton(5, 280, 112, 40, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(123, 280, 112, 40, "Save", COLOR_DARK_GREEN, COLOR_GREEN, 2);
}

function handleProbeCfgPlateTouch(x, y) {
  const xyz = pendantProbeV2.probeTypeIdx === PROBE_TYPE_XYZPLATE;
  let redraw = false;
  if (isTouchInBounds(x, y, 7, 79, 112, 40)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0; redraw = true; }
  if (xyz) {
    if (isTouchInBounds(x, y, 122, 79, 111, 40)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; redraw = true; }
    if (isTouchInBounds(x, y, 7, 122, 112, 40)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 2 ? -1 : 2; redraw = true; }
    if (isTouchInBounds(x, y, 122, 122, 111, 40)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 3 ? -1 : 3; redraw = true; }
  }
  if (redraw) { drawProbeCfgPlateScreen(); return; }
  if (isTouchInBounds(x, y, 5, 280, 112, 40)) { currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 122, 280, 113, 40)) { saveProbeSettings(); currentPendantScreen = PSCREEN_PROBE; return; }
}
