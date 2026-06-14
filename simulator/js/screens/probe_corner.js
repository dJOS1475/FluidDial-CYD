/* screen_probe_corner.cpp port — SCR2 XYZ corner probe */

function enterProbeCorner() {
  pendantProbeV2.focusedField = 0;
  pendantProbeV2.confirmActive = false;
  pendantProbeV2.dialAccelCount = 0;
}
function exitProbeCorner() {}

function runProbeCorner() {
  if (!pendantConnected) return;
  const p = pendantProbeV2;
  const pNum = pendantProbing.selectedCoordIndex + 1;
  const rate = p.probeRate, depth = p.cornerDepth, over = p.cornerOver, retXY = p.cornerRetXY;
  const retZ = p.retractDist, maxZ = p.maxZTravel;
  const is3D = probeIs3D();
  const platZ = is3D ? p.ballDia / 2 : p.plateThick;
  const edgeOfs = is3D ? p.ballDia / 2 : 0;
  const cIdx = p.cornerIdx, aIdx = p.axesIdx;
  const xDir = cIdx === 0 || cIdx === 2 ? 1 : -1;
  const yDir = cIdx === 0 || cIdx === 1 ? 1 : -1;
  const doXY = aIdx === 0 || aIdx === 1;
  const doZ = aIdx === 0 || aIdx === 2;
  if (doZ) {
    send_line("G91 G21");
    send_line(`G38.2 Z-${fmtF(maxZ, 3)} F${fmtF(rate, 0)}`);
    send_line("G90");
    send_line(`G10 L20 P${pNum} Z${fmtF(platZ, 3)}`);
    send_line("G91");
    send_line(`G0 Z${fmtF(retZ, 3)} F1000`);
  }
  if (doXY) {
    const dropZ = doZ ? depth + retZ : depth;
    send_line("G91");
    send_line(`G0 Z-${fmtF(dropZ, 3)} F500`);
    send_line(`G0 X${fmtF(-xDir * over, 3)} F1000`);
    send_line(`G38.2 X${fmtF(xDir * (over + 20), 3)} F${fmtF(rate, 0)}`);
    send_line("G90");
    send_line(`G10 L20 P${pNum} X${fmtF(xDir > 0 ? -edgeOfs : edgeOfs, 3)}`);
    send_line("G91");
    send_line(`G0 X${fmtF(-xDir * retXY, 3)} F1000`);
    send_line(`G0 Y${fmtF(-yDir * over, 3)} F1000`);
    send_line(`G38.2 Y${fmtF(yDir * (over + 20), 3)} F${fmtF(rate, 0)}`);
    send_line("G90");
    send_line(`G10 L20 P${pNum} Y${fmtF(yDir > 0 ? -edgeOfs : edgeOfs, 3)}`);
    send_line("G91");
    send_line(`G0 Y${fmtF(-yDir * retXY, 3)} F1000`);
    send_line(`G0 Z${fmtF(depth, 3)} F500`);
    send_line("G90");
  }
}

const cornerLabels = ["Bot-Left", "Bot-Right", "Top-Left", "Top-Right"];
const axesLabels = ["X+Y+Z", "X+Y", "Z"];

function drawCyclePair() {
  display.fillRoundRect(5, 69, 230, 46, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 72); display.print("TAP TO CYCLE");

  display.fillRoundRect(7, 81, 110, 30, 4, PROBE_BG_PANEL);
  display.drawRoundRect(7, 81, 110, 30, 4, PROBE_C_YELLOW);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(12, 84); display.print("CORNER");
  display.setTextColor(PROBE_C_YELLOW);
  display.setCursor(12, 95); display.print(cornerLabels[pendantProbeV2.cornerIdx]);

  display.fillRoundRect(120, 81, 113, 30, 4, PROBE_BG_PANEL);
  display.drawRoundRect(120, 81, 113, 30, 4, COLOR_GREEN);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(125, 84); display.print("AXES");
  display.setTextColor(PROBE_C_GREEN);
  display.setCursor(125, 95); display.print(axesLabels[pendantProbeV2.axesIdx]);
}

function drawCornerSpecificPanel() {
  display.fillRoundRect(5, 118, 230, 80, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 121); display.print("CORNER - SPECIFIC");
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(7, 130, 112, 30, "Probe depth", pendantProbeV2.cornerDepth, "mm", PROBE_C_RED, fo === 0, 3);
  probeDrawKVTouch(122, 130, 111, 30, "Overshoot", pendantProbeV2.cornerOver, "mm", PROBE_C_BLUE, fo === 1, 3);
  probeDrawKVTouch(7, 163, 112, 30, "XY retract", pendantProbeV2.cornerRetXY, "mm", PROBE_C_BLUE, fo === 2, 3);

  display.fillRoundRect(122, 163, 111, 30, 4, PROBE_BG_SCREEN);
  display.drawRoundRect(122, 163, 111, 30, 4, PROBE_C_DIMBLUE);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(127, 166); display.print("Sets");
  display.setTextColor(PROBE_C_BLUE);
  const axLbl = pendantProbeV2.axesIdx === 0 ? "XYZ0" : pendantProbeV2.axesIdx === 1 ? "XY0" : "Z0";
  display.setCursor(127, 178); display.print(`${pendantProbing.selectedCoordSystem} ${axLbl}`);
}

function drawProbeCornerScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("XYZ CORNER");
  probeDrawPosPanel(38);
  drawCyclePair();
  drawCornerSpecificPanel();
  // (Redundant sel-bar removed — KV field shows the value live.)
  probeDrawWarn(205, "! Position probe above corner edge");
  drawButton(5, 239, 230, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(5, 280, 112, 38, "Main Menu", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(123, 280, 112, 38, "Probe", PROBE_BTN_GREEN, COLOR_WHITE, 2);
  if (pendantProbeV2.confirmActive) probeDrawConfirmOverlay("XYZ CORNER");
}

function updateProbeCornerScreen() {
  if (currentPendantScreen !== PSCREEN_PROBE_CORNER) return;
  probeDrawPosPanel(38);
}

function handleProbeCornerTouch(x, y) {
  if (pendantProbeV2.confirmActive) {
    if (isTouchInBounds(x, y, 28, 175, 78, 32)) { pendantProbeV2.confirmActive = false; drawProbeCornerScreen(); }
    else if (isTouchInBounds(x, y, 114, 175, 98, 32)) { pendantProbeV2.confirmActive = false; runProbeCorner(); currentPendantScreen = PSCREEN_STATUS; }
    return;
  }
  if (isTouchInBounds(x, y, 7, 81, 110, 30)) { pendantProbeV2.cornerIdx = (pendantProbeV2.cornerIdx + 1) % 4; drawCyclePair(); drawCornerSpecificPanel(); return; }
  if (isTouchInBounds(x, y, 120, 81, 113, 30)) { pendantProbeV2.axesIdx = (pendantProbeV2.axesIdx + 1) % 3; drawCyclePair(); drawCornerSpecificPanel(); return; }
  let redraw = false;
  if (isTouchInBounds(x, y, 7, 130, 112, 30)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0; redraw = true; }
  if (isTouchInBounds(x, y, 122, 130, 111, 30)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; redraw = true; }
  if (isTouchInBounds(x, y, 7, 163, 112, 30)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 2 ? -1 : 2; redraw = true; }
  if (redraw) { drawProbeCornerScreen(); return; }
  if (isTouchInBounds(x, y, 5, 239, 230, 38)) { pendantProbeV2.returnScreen = PSCREEN_PROBE_CORNER; currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
    if (!pendantConnected) { probeDrawWarn(205, "! Not connected", true); return; }
    pendantProbeV2.confirmActive = true; drawProbeCornerScreen(); return;
  }
}
