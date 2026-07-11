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
  const rate = p.probeRate, seekF = p.seekRate, depth = p.cornerDepth, over = p.cornerOver, retXY = p.cornerRetXY;
  const retZ = p.retractDist, maxZ = p.maxZTravel;
  const is3D = probeIs3D();
  const platZ = is3D ? probeTipOffset3D() : p.plateThick;
  // XY edge comp: ball radius (3D) or the XYZ plate's wall thickness per axis.
  const edgeOfsX = is3D ? probeTipOffset3D() : p.plateOffX;
  const edgeOfsY = is3D ? probeTipOffset3D() : p.plateOffY;
  const cIdx = p.cornerIdx;
  const xDir = cIdx === 0 || cIdx === 2 ? 1 : -1;
  const yDir = cIdx === 0 || cIdx === 1 ? 1 : -1;
  const doXY = true;   // corner probe always does X/Y/Z
  const doZ = true;
  probeActivateWcs();          // zero into the system shown on screen
  if (doZ) {
    send_line("G91 G21");
    probeSeekFine("Z", -maxZ, seekF, rate);   // two-pass surface touch
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
    probeSeekFine("X", xDir * (over + 20), seekF, rate);   // two-pass toward X wall
    send_line("G90");
    send_line(`G10 L20 P${pNum} X${fmtF(xDir > 0 ? -edgeOfsX : edgeOfsX, 3)}`);
    send_line("G91");
    send_line(`G0 X${fmtF(-xDir * retXY, 3)} F1000`);
    send_line(`G0 Y${fmtF(-yDir * over, 3)} F1000`);
    probeSeekFine("Y", yDir * (over + 20), seekF, rate);   // two-pass toward Y wall
    send_line("G90");
    send_line(`G10 L20 P${pNum} Y${fmtF(yDir > 0 ? -edgeOfsY : edgeOfsY, 3)}`);
    send_line("G91");
    send_line(`G0 Y${fmtF(-yDir * retXY, 3)} F1000`);
    send_line(`G0 Z${fmtF(depth, 3)} F500`);
    send_line("G90");
  }
}

const cornerLabels = ["Bot-Left", "Bot-Right", "Top-Left", "Top-Right"];

// Top-down diagram of corner probing: a workpiece corner with arrows probing the
// X and Y edges, and a dot marking the found corner.
function drawCornerDiagram() {
  display.fillRect(20, 182, 38, 22, PROBE_C_DIMBLUE);   // workpiece (corner top-right)
  // Probe body/stem + stylus descending onto the corner
  display.fillRoundRect(53, 141, 10, 12, 2, PROBE_C_LBLUE);
  display.drawLine(58, 152, 58, 180, PROBE_C_YELLOW);
  display.fillCircle(58, 182, 2, PROBE_C_YELLOW);       // probe ball at the corner
  display.drawLine(80, 192, 61, 192, PROBE_C_GREEN);    // X probe → right edge
  display.drawLine(61, 192, 65, 189, PROBE_C_GREEN);
  display.drawLine(61, 192, 65, 195, PROBE_C_GREEN);
  display.drawLine(40, 164, 40, 177, PROBE_C_GREEN);    // Y probe → top edge
  display.drawLine(40, 177, 37, 173, PROBE_C_GREEN);
  display.drawLine(40, 177, 43, 173, PROBE_C_GREEN);
}

// Redraw ONLY the corner KV fields (opaque) — full draw + dial handler use it.
function updateProbeCornerFields() {
  if (currentPendantScreen !== PSCREEN_PROBE_CORNER) return;
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(122, 117, 111, 31, "Probe depth", pendantProbeV2.cornerDepth, "mm", PROBE_C_BLUE, fo === 0, 3);
  probeDrawKVTouch(122, 150, 111, 31, "Overshoot", pendantProbeV2.cornerOver, "mm", PROBE_C_BLUE, fo === 1, 3);
  probeDrawKVTouch(122, 183, 111, 31, "XY retract", pendantProbeV2.cornerRetXY, "mm", PROBE_C_BLUE, fo === 2, 3);
}

function drawProbeCornerScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("XYZ CORNER");
  probeDrawPosPanel(38);
  display.fillRoundRect(5, 70, 230, 146, 4, PROBE_BG_PANEL);
  // Left column: sequence + diagram
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 73); display.print("SEQUENCE");
  drawSeqStep(8, 87, 1, "Touch top->Z0", true);
  drawSeqStep(8, 105, 2, "Probe X & Y", false);
  drawSeqStep(8, 123, 3, "Set X0 Y0 Z0", false);
  drawCornerDiagram();
  // Right column: settings
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(122, 73); display.print("SETTINGS");
  // Corner selector (tap to cycle) — shared tappable-field style; the result
  // line was removed (sequence step 3 already reads "Set X0 Y0 Z0") so all four
  // controls get taller, evenly spaced targets.
  display.fillRoundRect(122, 84, 111, 31, 2, PROBE_BG_SCREEN);
  display.drawRoundRect(122, 84, 111, 31, 2, PROBE_C_TAPBDR);
  display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(125, 87); display.print("CORNER");
  display.setTextColor(PROBE_C_BLUE);
  display.setCursor(125, 101); display.print(cornerLabels[pendantProbeV2.cornerIdx]);
  updateProbeCornerFields();
  probeDrawWarn(220, "! Position probe above corner edge");
  drawButton(5, 239, 112, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  probeDrawWorkAreaButton(123, 239, 112, 38);
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
  if (isTouchInBounds(x, y, 122, 84, 111, 31)) { pendantProbeV2.cornerIdx = (pendantProbeV2.cornerIdx + 1) % 4; drawProbeCornerScreen(); return; }
  let redraw = false;
  if (isTouchInBounds(x, y, 122, 117, 111, 31)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0; redraw = true; }
  if (isTouchInBounds(x, y, 122, 150, 111, 31)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; redraw = true; }
  if (isTouchInBounds(x, y, 122, 183, 111, 31)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 2 ? -1 : 2; redraw = true; }
  if (redraw) { drawProbeCornerScreen(); return; }
  if (isTouchInBounds(x, y, 5, 239, 112, 38)) { pendantProbeV2.returnScreen = PSCREEN_PROBE_CORNER; currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 123, 239, 112, 38)) { probeCycleWorkArea(); drawProbeCornerScreen(); return; }
  if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
    if (!pendantConnected) { probeDrawWarn(220, "! Not connected", true); return; }
    pendantProbeV2.confirmActive = true; drawProbeCornerScreen(); return;
  }
}
