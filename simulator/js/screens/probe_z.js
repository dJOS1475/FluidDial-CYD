/* screen_probe_z.cpp port — SCR1 Z surface probe */

function enterProbeZ() {
  pendantProbeV2.focusedField = 0;
  pendantProbeV2.confirmActive = false;
  pendantProbeV2.dialAccelCount = 0;
}
function exitProbeZ() {}

function runProbeZ() {
  if (!pendantConnected) return;
  const pNum = pendantProbing.selectedCoordIndex + 1;
  const zOffset = probeIs3D() ? pendantProbeV2.ballDia / 2.0 : pendantProbeV2.plateThick;
  send_line("G91 G21");
  send_line(`G38.2 Z-${fmtF(pendantProbeV2.maxZTravel, 3)} F${fmtF(pendantProbeV2.probeRate, 0)}`);
  send_line("G90");
  send_line(`G10 L20 P${pNum} Z${fmtF(zOffset, 3)}`);
  send_line("G91");
  send_line(`G0 Z${fmtF(pendantProbeV2.retractDist, 3)}`);
  send_line("G90");
}

function drawZParamButton(x, y, w, h, label, valueMm, focused) {
  const inInch = pendantMachine.inInches;
  const bg = focused ? PROBE_SEL_BG : PROBE_BG_SCREEN;
  const bdr = focused ? PROBE_C_YELLOW : PROBE_C_DIMBLUE;
  const vc = focused ? PROBE_C_YELLOW : PROBE_C_RED;
  display.fillRoundRect(x, y, w, h, 8, bg);
  display.drawRoundRect(x, y, w, h, 8, bdr);
  display.setTextSize(1);
  display.setTextColor(focused ? COLOR_WHITE : PROBE_C_LBLUE);
  let lw = display.textWidth(label);
  display.setCursor(x + ((w - lw) / 2 | 0), y + 5);
  display.print(label);
  const valBuf = inInch ? fmtF(valueMm / 25.4, 3) + " in" : fmtF(valueMm, 0) + " mm";
  display.setTextSize(2);
  display.setTextColor(vc);
  let vw = display.textWidth(valBuf);
  display.setCursor(x + ((w - vw) / 2 | 0), y + 17);
  display.print(valBuf);
}

function drawSetsRow() {
  display.fillRoundRect(5, 120, 230, 38, 8, PROBE_BG_SCREEN);
  display.drawRoundRect(5, 120, 230, 38, 8, PROBE_C_DIMBLUE);
  display.setTextSize(1);
  display.setTextColor(PROBE_C_LBLUE);
  let lw = display.textWidth("Sets");
  display.setCursor(120 - (lw / 2 | 0), 125);
  display.print("Sets");
  const setsBuf = `${pendantProbing.selectedCoordSystem} Z0`;
  display.setTextSize(2);
  display.setTextColor(PROBE_C_BLUE);
  let vw = display.textWidth(setsBuf);
  display.setCursor(120 - (vw / 2 | 0), 137);
  display.print(setsBuf);
}

function drawProbeZScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("Z SURFACE");
  const fo = pendantProbeV2.focusedField;
  probeDrawPosPanel(38);
  drawZParamButton(5, 79, 112, 38, "Max Z travel", pendantProbeV2.maxZTravel, fo === 0);
  drawZParamButton(122, 79, 113, 38, "Retract dist", pendantProbeV2.retractDist, fo === 1);
  drawSetsRow();
  probeDrawWarn(198, probeIs3D() ? "! Verify probe is connected"
                                 : "! Verify plate clip is connected", false, 38);
  drawButton(5, 239, 230, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(5, 280, 112, 38, "Main Menu", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  drawButton(123, 280, 112, 38, "Probe", PROBE_BTN_GREEN, COLOR_WHITE, 2);
  if (pendantProbeV2.confirmActive) probeDrawConfirmOverlay("Z SURFACE");
}

function updateProbeZScreen() {
  if (currentPendantScreen !== PSCREEN_PROBE_Z) return;
  probeDrawPosPanel(38);
}

function handleProbeZTouch(x, y) {
  if (pendantProbeV2.confirmActive) {
    if (isTouchInBounds(x, y, 28, 175, 78, 32)) { pendantProbeV2.confirmActive = false; drawProbeZScreen(); }
    else if (isTouchInBounds(x, y, 114, 175, 98, 32)) { pendantProbeV2.confirmActive = false; runProbeZ(); currentPendantScreen = PSCREEN_STATUS; }
    return;
  }
  if (isTouchInBounds(x, y, 5, 79, 112, 38)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0; drawProbeZScreen(); return; }
  if (isTouchInBounds(x, y, 122, 79, 113, 38)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; drawProbeZScreen(); return; }
  if (isTouchInBounds(x, y, 5, 239, 230, 38)) { pendantProbeV2.returnScreen = PSCREEN_PROBE_Z; currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
    if (!pendantConnected) { probeDrawWarn(198, "! Not connected", true, 38); return; }
    pendantProbeV2.confirmActive = true; drawProbeZScreen(); return;
  }
}
