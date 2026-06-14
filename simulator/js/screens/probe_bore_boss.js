/* screen_probe_bore_boss.cpp port — SCR3a Bore / SCR3b Boss */

// ===== BORE =====
function enterProbeBore() {
  pendantProbeV2.focusedField = 0;
  pendantProbeV2.confirmActive = false;
  pendantProbeV2.dialAccelCount = 0;
}
function exitProbeBore() {}

function runProbeBore() {
  if (!pendantConnected) return;
  const p = pendantProbeV2;
  const pNum = pendantProbing.selectedCoordIndex + 1;
  const rate = p.probeRate, depth = p.boreDepth, rad = p.boreDia / 2, wallOf = p.boreOffset;
  const retZ = p.retractDist, maxZ = p.maxZTravel;
  const platZ = probeIs3D() ? p.ballDia / 2 : p.plateThick;
  const approach = rad + wallOf + 3;
  send_line("G91 G21");
  send_line(`G0 Z-${fmtF(depth, 3)} F500`);
  send_line(`G38.2 X${fmtF(approach, 3)} F${fmtF(rate, 0)}`);
  send_line("#<bx_pos> = #5061");
  send_line(`G0 X-${fmtF(approach + wallOf + 3, 3)} F1000`);
  send_line(`G38.2 X-${fmtF(wallOf + 6, 3)} F${fmtF(rate, 0)}`);
  send_line("#<bx_neg> = #5061");
  send_line(`G0 X${fmtF(wallOf + 3, 3)} F1000`);
  send_line(`G38.2 Y${fmtF(approach, 3)} F${fmtF(rate, 0)}`);
  send_line("#<by_pos> = #5062");
  send_line(`G0 Y-${fmtF(approach + wallOf + 3, 3)} F1000`);
  send_line(`G38.2 Y-${fmtF(wallOf + 6, 3)} F${fmtF(rate, 0)}`);
  send_line("#<by_neg> = #5062");
  send_line(`G0 Y${fmtF(wallOf + 3, 3)} F1000`);
  send_line(`G0 Z${fmtF(depth, 3)} F500`);
  send_line("G90");
  send_line("G0 X[{#<bx_pos>+#<bx_neg>}/2] Y[{#<by_pos>+#<by_neg>}/2] F1000");
  send_line("G91");
  send_line(`G38.2 Z-${fmtF(maxZ, 3)} F${fmtF(rate, 0)}`);
  send_line("G90");
  send_line(`G10 L20 P${pNum} X0 Y0 Z${fmtF(platZ, 3)}`);
  send_line("G91");
  send_line(`G0 Z${fmtF(retZ, 3)} F500`);
  send_line("G90");
}

function drawSeqStep(x, y, num, txt, active) {
  const bg = active ? PROBE_AMBER : PROBE_BG_PANEL;
  const fg = active ? COLOR_WHITE : PROBE_C_DIMBLUE;
  const tc = active ? PROBE_C_YELLOW : PROBE_C_DIMBLUE;
  display.fillCircle(x + 6, y + 6, 6, bg);
  display.setTextSize(1); display.setTextColor(fg);
  const nw = display.textWidth(String(num));
  display.setCursor(x + 6 - (nw / 2 | 0), y + 2); display.print(num);
  display.setTextColor(tc);
  display.setCursor(x + 16, y + 2); display.print(txt);
}

function drawProbeBoreScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("BORE");
  probeDrawPosPanel(38);
  display.fillRoundRect(5, 70, 230, 130, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 73); display.print("SEQUENCE");
  drawSeqStep(8, 87, 1, "Lower to depth", true);
  drawSeqStep(8, 105, 2, "Sweep XY walls", false);
  drawSeqStep(8, 123, 3, "Touch top->Z0", false);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(122, 73); display.print("SETTINGS");
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(122, 84, 111, 27, "Nominal dia.", pendantProbeV2.boreDia, "mm", PROBE_C_YELLOW, fo === 0, 3);
  probeDrawKVTouch(122, 113, 111, 27, "Probe depth", pendantProbeV2.boreDepth, "mm", PROBE_C_RED, fo === 1, 3);
  probeDrawKVTouch(122, 142, 111, 27, "Wall offset", pendantProbeV2.boreOffset, "mm", PROBE_C_BLUE, fo === 2, 3);
  probeDrawKVTouchInt(122, 171, 111, 27, "Passes", pendantProbeV2.borePasses, PROBE_C_DIMBLUE, fo === 3);
  probeDrawWarn(204, "! Tip clear of walls at start", true);
  {
    const s = "Sets " + pendantProbing.selectedCoordSystem + " X0 Y0 Z0";
    display.setTextSize(1); display.setTextColor(PROBE_C_GREEN);
    display.setCursor(120 - (display.textWidth(s) / 2 | 0), 224);
    display.print(s);
  }
  drawButton(5, 239, 230, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(5, 280, 112, 38, "Main Menu", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(123, 280, 112, 38, "Probe", PROBE_BTN_GREEN, COLOR_WHITE, 2);
  if (pendantProbeV2.confirmActive) probeDrawConfirmOverlay("BORE");
}

function updateProbeBoreScreen() {
  if (currentPendantScreen !== PSCREEN_PROBE_BORE) return;
  probeDrawPosPanel(38);
}

function handleProbeBoreTouch(x, y) {
  if (pendantProbeV2.confirmActive) {
    if (isTouchInBounds(x, y, 28, 175, 78, 32)) { pendantProbeV2.confirmActive = false; drawProbeBoreScreen(); }
    else if (isTouchInBounds(x, y, 114, 175, 98, 32)) { pendantProbeV2.confirmActive = false; runProbeBore(); currentPendantScreen = PSCREEN_STATUS; }
    return;
  }
  let redraw = false;
  if (isTouchInBounds(x, y, 122, 84, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0; redraw = true; }
  if (isTouchInBounds(x, y, 122, 113, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; redraw = true; }
  if (isTouchInBounds(x, y, 122, 142, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 2 ? -1 : 2; redraw = true; }
  if (isTouchInBounds(x, y, 122, 171, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 3 ? -1 : 3; redraw = true; }
  if (redraw) { drawProbeBoreScreen(); return; }
  if (isTouchInBounds(x, y, 5, 239, 230, 38)) { pendantProbeV2.returnScreen = PSCREEN_PROBE_BORE; currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
    if (!pendantConnected) { probeDrawWarn(204, "! Not connected", true); return; }
    pendantProbeV2.confirmActive = true; drawProbeBoreScreen(); return;
  }
}

// ===== BOSS =====
function enterProbeBoss() {
  pendantProbeV2.focusedField = 0;
  pendantProbeV2.confirmActive = false;
  pendantProbeV2.dialAccelCount = 0;
}
function exitProbeBoss() {}

function runProbeBoss() {
  if (!pendantConnected) return;
  const p = pendantProbeV2;
  const pNum = pendantProbing.selectedCoordIndex + 1;
  const rate = p.probeRate, depth = p.bossDepth, rad = p.bossDia / 2, clear = p.bossClear;
  const retZ = p.retractDist, maxZ = p.maxZTravel;
  const platZ = probeIs3D() ? p.ballDia / 2 : p.plateThick;
  const edgeOfs = probeIs3D() ? p.ballDia / 2 : 0;
  send_line("G91 G21");
  send_line(`G38.2 Z-${fmtF(maxZ, 3)} F${fmtF(rate, 0)}`);
  send_line("G90");
  send_line("#<boss_z> = #5063");
  send_line("G91");
  send_line(`G0 Z${fmtF(retZ, 3)} F500`);
  send_line(`G0 X${fmtF(rad + clear, 3)} F1000`);
  send_line(`G0 Z-${fmtF(depth + retZ, 3)} F500`);
  send_line(`G38.2 X-${fmtF(clear + rad + 5, 3)} F${fmtF(rate, 0)}`);
  send_line("#<px_pos> = #5061");
  send_line(`G0 X${fmtF(clear + rad + 3, 3)} F1000`);
  send_line(`G0 Z${fmtF(depth + retZ, 3)} F500`);
  send_line(`G0 X-${fmtF(2 * (rad + clear) + 3, 3)} F1000`);
  send_line(`G0 Z-${fmtF(depth + retZ, 3)} F500`);
  send_line(`G38.2 X${fmtF(clear + rad + 5, 3)} F${fmtF(rate, 0)}`);
  send_line("#<px_neg> = #5061");
  send_line(`G0 X-${fmtF(clear + rad + 3, 3)} F1000`);
  send_line(`G0 Z${fmtF(depth + retZ, 3)} F500`);
  send_line(`G0 X${fmtF(rad + clear, 3)} F1000`);
  send_line(`G0 Y${fmtF(rad + clear, 3)} F1000`);
  send_line(`G0 Z-${fmtF(depth + retZ, 3)} F500`);
  send_line(`G38.2 Y-${fmtF(clear + rad + 5, 3)} F${fmtF(rate, 0)}`);
  send_line("#<py_pos> = #5062");
  send_line(`G0 Y${fmtF(clear + rad + 3, 3)} F1000`);
  send_line(`G0 Z${fmtF(depth + retZ, 3)} F500`);
  send_line(`G0 Y-${fmtF(2 * (rad + clear) + 3, 3)} F1000`);
  send_line(`G0 Z-${fmtF(depth + retZ, 3)} F500`);
  send_line(`G38.2 Y${fmtF(clear + rad + 5, 3)} F${fmtF(rate, 0)}`);
  send_line("#<py_neg> = #5062");
  send_line(`G0 Y-${fmtF(clear + rad + 3, 3)} F1000`);
  send_line(`G0 Z${fmtF(depth + retZ, 3)} F500`);
  send_line("G90");
  send_line("G0 X[{#<px_pos>+#<px_neg>}/2] Y[{#<py_pos>+#<py_neg>}/2] F1000");
  send_line("G0 Z[#<boss_z>] F500");
  send_line(`G10 L20 P${pNum} X${fmtF(edgeOfs, 3)} Y${fmtF(edgeOfs, 3)} Z${fmtF(platZ, 3)}`);
  send_line("G91");
  send_line(`G0 Z${fmtF(retZ, 3)} F500`);
  send_line("G90");
}

function drawProbeBossScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("BOSS");
  probeDrawPosPanel(38);
  display.fillRoundRect(5, 70, 230, 130, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 73); display.print("SEQUENCE");
  drawSeqStep(8, 87, 1, "Touch top->Z0", true);
  drawSeqStep(8, 105, 2, "Retract & move", false);
  drawSeqStep(8, 123, 3, "Sweep XY walls", false);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(122, 73); display.print("SETTINGS");
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(122, 84, 111, 27, "Nominal dia.", pendantProbeV2.bossDia, "mm", PROBE_C_YELLOW, fo === 0, 3);
  probeDrawKVTouch(122, 113, 111, 27, "Probe depth", pendantProbeV2.bossDepth, "mm", PROBE_C_RED, fo === 1, 3);
  probeDrawKVTouch(122, 142, 111, 27, "Clearance", pendantProbeV2.bossClear, "mm", PROBE_C_BLUE, fo === 2, 3);
  probeDrawKVTouchInt(122, 171, 111, 27, "Passes", pendantProbeV2.bossPasses, PROBE_C_DIMBLUE, fo === 3);
  probeDrawWarn(204, "! Start above centre of boss");
  {
    const s = "Sets " + pendantProbing.selectedCoordSystem + " X0 Y0 Z0";
    display.setTextSize(1); display.setTextColor(PROBE_C_GREEN);
    display.setCursor(120 - (display.textWidth(s) / 2 | 0), 224);
    display.print(s);
  }
  drawButton(5, 239, 230, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(5, 280, 112, 38, "Main Menu", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(123, 280, 112, 38, "Probe", PROBE_BTN_GREEN, COLOR_WHITE, 2);
  if (pendantProbeV2.confirmActive) probeDrawConfirmOverlay("BOSS");
}

function updateProbeBossScreen() {
  if (currentPendantScreen !== PSCREEN_PROBE_BOSS) return;
  probeDrawPosPanel(38);
}

function handleProbeBossTouch(x, y) {
  if (pendantProbeV2.confirmActive) {
    if (isTouchInBounds(x, y, 28, 175, 78, 32)) { pendantProbeV2.confirmActive = false; drawProbeBossScreen(); }
    else if (isTouchInBounds(x, y, 114, 175, 98, 32)) { pendantProbeV2.confirmActive = false; runProbeBoss(); currentPendantScreen = PSCREEN_STATUS; }
    return;
  }
  let redraw = false;
  if (isTouchInBounds(x, y, 122, 84, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0; redraw = true; }
  if (isTouchInBounds(x, y, 122, 113, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; redraw = true; }
  if (isTouchInBounds(x, y, 122, 142, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 2 ? -1 : 2; redraw = true; }
  if (isTouchInBounds(x, y, 122, 171, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 3 ? -1 : 3; redraw = true; }
  if (redraw) { drawProbeBossScreen(); return; }
  if (isTouchInBounds(x, y, 5, 239, 230, 38)) { pendantProbeV2.returnScreen = PSCREEN_PROBE_BOSS; currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
    if (!pendantConnected) { probeDrawWarn(204, "! Not connected", true); return; }
    pendantProbeV2.confirmActive = true; drawProbeBossScreen(); return;
  }
}
