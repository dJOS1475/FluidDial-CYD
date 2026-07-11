/* screen_probe_cfg.cpp port — SCR0a 3D config / SCR0b plate config */

// ===== Probe-type illustrations =====
const kGfxCX = 120;

// 3D touch probe (compact — fits below the deflection-cal panel, y188+).
function drawProbe3DGraphic() {
  const cx = kGfxCX;
  display.fillRoundRect(5, 188, 230, 82, 4, PROBE_BG_PANEL);
  display.fillRect(cx - 7, 194, 14, 8, PROBE_C_DIMBLUE);
  display.fillRoundRect(cx - 17, 202, 34, 18, 4, COLOR_GRAY_TEXT);
  display.drawFastVLine(cx - 1, 220, 24, PROBE_C_LBLUE);
  display.drawFastVLine(cx, 220, 24, PROBE_C_LBLUE);
  display.drawFastVLine(cx + 1, 220, 24, PROBE_C_LBLUE);
  display.fillCircle(cx, 247, 5, PROBE_C_RED);
  display.drawFastHLine(cx - 50, 255, 100, COLOR_GRAY_TEXT);
  for (let i = 0; i < 7; i++)
    display.drawLine(cx - 46 + i * 14, 255, cx - 52 + i * 14, 261, PROBE_C_DIMBLUE);
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
  pendantProbeV2.calState = 0;
}
function exitProbeCfg3D() {}

// buttons: 0 = none, 1 = OK, 2 = CANCEL + affirmative (okLabel)
function drawCalOverlay(l1, l2, l2col, buttons, okLabel = "APPLY") {
  display.fillRoundRect(20, 100, 200, 120, 8, PROBE_BG_PANEL);
  display.drawRoundRect(20, 100, 200, 120, 8, PROBE_C_YELLOW);
  display.setTextSize(1); display.setTextColor(PROBE_C_YELLOW);
  display.setCursor(120 - (display.textWidth(l1) / 2 | 0), 118); display.print(l1);
  display.setTextSize(2); display.setTextColor(l2col);
  display.setCursor(120 - (display.textWidth(l2) / 2 | 0), 140); display.print(l2);
  display.setTextSize(2); display.setTextColor(COLOR_WHITE);
  if (buttons === 2) {
    display.fillRoundRect(28, 175, 78, 32, 5, COLOR_BUTTON_GRAY);
    display.fillRoundRect(114, 175, 98, 32, 5, PROBE_BTN_GREEN);
    display.setCursor(36, 183); display.print("CANCEL");
    display.setCursor(114 + ((98 - display.textWidth(okLabel)) / 2 | 0), 183); display.print(okLabel);
  } else if (buttons === 1) {
    display.fillRoundRect(71, 175, 98, 32, 5, PROBE_BTN_BLUE);
    display.setCursor(71 + ((98 - display.textWidth("OK")) / 2 | 0), 183); display.print("OK");
  }
}

// Emit the (side-effect-free) calibration probe program. In the simulator there
// is no real probe, so we also simulate a plausible measured deflection.
function runProbeCalibration() {
  if (!pendantConnected) return;
  const p = pendantProbeV2;
  const seekF = p.seekRate, fineF = p.probeRate, maxZ = p.maxZTravel, retZ = p.retractDist;
  const CLEAR = 10, DEPTH = 5, toClear = p.calGaugeWidth / 2 + CLEAR, plunge = retZ + DEPTH;
  send_line("G21 G90");
  send_line("#<sx> = #5420");
  send_line("#<sy> = #5421");
  send_line("G91");
  send_line(`G38.2 Z-${fmtF(maxZ, 3)} F${fmtF(fineF, 0)}`);
  send_line(`G0 Z${fmtF(retZ, 3)} F500`);
  send_line("G90");
  // left face → x1
  send_line("G91");
  send_line(`G0 X-${fmtF(toClear, 3)} F1000`);
  send_line(`G0 Z-${fmtF(plunge, 3)} F500`);
  send_line(`G38.2 X${fmtF(CLEAR + 3, 3)} F${fmtF(seekF, 0)}`);
  send_line("G0 X-1.5 F1000");
  send_line(`G38.2 X2.5 F${fmtF(fineF, 0)}`);
  send_line(`G0 Z${fmtF(plunge, 3)} F500`);
  send_line("G90 G0 X#<sx> Y#<sy> F1000");
  // right face → x2
  send_line("G91");
  send_line(`G0 X${fmtF(toClear, 3)} F1000`);
  send_line(`G0 Z-${fmtF(plunge, 3)} F500`);
  send_line(`G38.2 X-${fmtF(CLEAR + 3, 3)} F${fmtF(seekF, 0)}`);
  send_line("G0 X1.5 F1000");
  send_line(`G38.2 X-2.5 F${fmtF(fineF, 0)}`);
  send_line(`G0 Z${fmtF(plunge, 3)} F500`);
  send_line("G90 G0 X#<sx> Y#<sy> F1000");
  // --- simulate probe results: pretend the probe over-reads 0.013 mm per face ---
  const simDefl = 0.013;
  const x1 = -p.calGaugeWidth / 2 - p.ballDia / 2 - simDefl;
  const x2 = p.calGaugeWidth / 2 + p.ballDia / 2 + simDefl;
  p.calResult = Math.max(-1, Math.min(1, ((x2 - x1) - p.calGaugeWidth - p.ballDia) / 2));
  p.calState = 2;
}

function updateProbeCfg3DScreen() {
  if (currentPendantScreen !== PSCREEN_PROBE_CFG_3D) return;
  // (Firmware polls probe results here; the sim resolves synchronously in runProbeCalibration.)
}

// Redraw ONLY the 3D-config KV fields (opaque) — full draw + dial handler use it.
function updateProbeCfg3DFields() {
  if (currentPendantScreen !== PSCREEN_PROBE_CFG_3D) return;
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(7, 79, 112, 40, "Ball dia.", pendantProbeV2.ballDia, "mm", PROBE_C_BLUE, fo === 0, 3);
  probeDrawKVTouch(122, 79, 111, 40, "Deflection", pendantProbeV2.deflection, "mm", PROBE_C_BLUE, fo === 1, 3);
  probeDrawKVTouch(7, 140, 112, 40, "Gauge width", pendantProbeV2.calGaugeWidth, "mm", PROBE_C_BLUE, fo === 2, 1);
}

function drawProbeCfg3DScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("PROBE CONFIG");
  display.fillRoundRect(5, 38, 230, 26, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 41); display.print("PROBE HARDWARE");
  display.setTextColor(PROBE_C_GREEN);
  display.setCursor(10, 53); display.print("3D Touch Probe");

  display.fillRoundRect(5, 67, 230, 55, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 70); display.print("STYLUS");

  // Deflection-calibration panel (field spaced like the Stylus panel)
  display.fillRoundRect(5, 128, 230, 55, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 131); display.print("DEFLECTION CAL");
  drawButton(122, 140, 111, 40, "Calibrate", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  updateProbeCfg3DFields();   // both panel bgs drawn — fields last

  drawProbe3DGraphic();

  drawButton(5, 280, 112, 40, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(123, 280, 112, 40, "Save", COLOR_DARK_GREEN, COLOR_GREEN, 2);

  if (pendantProbeV2.calState === 4) drawCalOverlay("START CALIBRATION?", "PROBE MOVES", PROBE_C_YELLOW, 2, "START");
  else if (pendantProbeV2.calState === 1) drawCalOverlay("CALIBRATING...", "PROBING", PROBE_C_YELLOW, 0);
  else if (pendantProbeV2.calState === 2) drawCalOverlay("MEASURED DEFLECTION", fmtF(pendantProbeV2.calResult, 3) + " mm", PROBE_C_GREEN, 2);
  else if (pendantProbeV2.calState === 3) drawCalOverlay("CALIBRATION FAILED", "CHECK SETUP", PROBE_C_RED, 1);
}

function handleProbeCfg3DTouch(x, y) {
  if (pendantProbeV2.calState === 4) {   // confirm: CANCEL | START
    if (isTouchInBounds(x, y, 28, 175, 78, 32)) { pendantProbeV2.calState = 0; drawProbeCfg3DScreen(); }
    else if (isTouchInBounds(x, y, 114, 175, 98, 32)) { pendantProbeV2.calState = 1; pendantProbeV2.calStartMs = Date.now(); drawProbeCfg3DScreen(); runProbeCalibration(); drawProbeCfg3DScreen(); }
    return;
  }
  if (pendantProbeV2.calState === 2) {
    if (isTouchInBounds(x, y, 28, 175, 78, 32)) { pendantProbeV2.calState = 0; drawProbeCfg3DScreen(); }
    else if (isTouchInBounds(x, y, 114, 175, 98, 32)) { pendantProbeV2.deflection = pendantProbeV2.calResult; saveProbeSettings(); pendantProbeV2.calState = 0; drawProbeCfg3DScreen(); }
    return;
  }
  if (pendantProbeV2.calState === 3) {
    if (isTouchInBounds(x, y, 71, 175, 98, 32)) { pendantProbeV2.calState = 0; drawProbeCfg3DScreen(); }
    return;
  }
  if (pendantProbeV2.calState === 1) return;

  let redraw = false;
  if (isTouchInBounds(x, y, 7, 79, 112, 40)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0; redraw = true; }
  if (isTouchInBounds(x, y, 122, 79, 111, 40)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; redraw = true; }
  if (isTouchInBounds(x, y, 7, 140, 112, 40)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 2 ? -1 : 2; redraw = true; }
  if (redraw) { drawProbeCfg3DScreen(); return; }

  if (isTouchInBounds(x, y, 122, 140, 111, 40)) {   // Calibrate → confirm first
    if (!pendantConnected) { pendantProbeV2.calState = 3; drawProbeCfg3DScreen(); return; }
    pendantProbeV2.calState = 4;
    drawProbeCfg3DScreen();
    return;
  }
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

// Redraw ONLY the plate-config KV fields (opaque) — full draw + dial handler use it.
function updateProbeCfgPlateFields() {
  if (currentPendantScreen !== PSCREEN_PROBE_CFG_PLATE) return;
  const xyz = (pendantProbeV2.probeTypeIdx === PROBE_TYPE_XYZPLATE);
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(7, 79, 112, 40, "Thickness", pendantProbeV2.plateThick, "mm", PROBE_C_BLUE, fo === 0, 3);
  if (xyz) {
    probeDrawKVTouch(122, 79, 111, 40, "Width", pendantProbeV2.plateWidth, "mm", PROBE_C_DIMBLUE, fo === 1, 3);
    probeDrawKVTouch(7, 122, 112, 40, "XY offset X", pendantProbeV2.plateOffX, "mm", PROBE_C_BLUE, fo === 2, 3);
    probeDrawKVTouch(122, 122, 111, 40, "XY offset Y", pendantProbeV2.plateOffY, "mm", PROBE_C_BLUE, fo === 3, 3);
  }
}

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
  updateProbeCfgPlateFields();
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
