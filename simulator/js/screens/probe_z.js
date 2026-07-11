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
  const zOffset = probeIs3D() ? probeTipOffset3D() : pendantProbeV2.plateThick;
  probeActivateWcs();          // zero into the system shown on screen
  send_line("G91 G21");
  // Two-pass: fast seek down to the surface, then slow re-probe for precision.
  probeSeekFine("Z", -pendantProbeV2.maxZTravel, pendantProbeV2.seekRate, pendantProbeV2.probeRate);
  send_line("G90");
  send_line(`G10 L20 P${pNum} Z${fmtF(zOffset, 3)}`);
  send_line("G91");
  send_line(`G0 Z${fmtF(pendantProbeV2.retractDist, 3)}`);
  send_line("G90");
}

function drawZParamButton(x, y, w, h, label, valueMm, focused) {
  const inInch = pendantMachine.inInches;
  const bg = focused ? PROBE_SEL_BG : PROBE_BG_SCREEN;
  const bdr = focused ? PROBE_C_YELLOW : PROBE_C_TAPBDR;
  const vc = focused ? PROBE_C_YELLOW : PROBE_C_BLUE;
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

// Side-view diagram of a Z-surface probe: stylus descending onto the surface.
function drawZDiagram() {
  display.fillRect(22, 192, 76, 10, PROBE_C_DIMBLUE);   // work surface
  display.fillRoundRect(55, 150, 10, 12, 2, PROBE_C_LBLUE);  // probe body/stem
  display.drawLine(60, 161, 60, 189, PROBE_C_YELLOW);   // stylus
  display.fillCircle(60, 191, 2, PROBE_C_YELLOW);       // ball touching surface
  display.drawLine(42, 166, 42, 184, PROBE_C_GREEN);    // down arrow shaft
  display.drawLine(42, 184, 39, 180, PROBE_C_GREEN);
  display.drawLine(42, 184, 45, 180, PROBE_C_GREEN);
}

// Redraw ONLY the Z-Surface KV fields (opaque) — full draw + dial handler use it.
function updateProbeZFields() {
  if (currentPendantScreen !== PSCREEN_PROBE_Z) return;
  const fo = pendantProbeV2.focusedField;
  drawZParamButton(122, 84, 111, 33, "Max Z travel", pendantProbeV2.maxZTravel, fo === 0);
  drawZParamButton(122, 120, 111, 33, "Retract dist", pendantProbeV2.retractDist, fo === 1);
}

function drawProbeZScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("Z SURFACE");
  probeDrawPosPanel(38);
  display.fillRoundRect(5, 70, 230, 146, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 73); display.print("SEQUENCE");
  drawSeqStep(8, 87, 1, "Fast seek -Z", true);
  drawSeqStep(8, 105, 2, "Slow re-probe", false);
  drawSeqStep(8, 123, 3, "Set Z0", false);
  drawZDiagram();
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(122, 73); display.print("SETTINGS");
  updateProbeZFields();
  {
    const s = "Sets Z0";
    display.setTextSize(1); display.setTextColor(PROBE_C_GREEN);
    display.setCursor(177 - (display.textWidth(s) / 2 | 0), 166);
    display.print(s);
  }
  probeDrawWarn(220, probeIs3D() ? "! Verify probe is connected"
                                 : "! Verify plate clip is connected");
  drawButton(5, 239, 112, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  probeDrawWorkAreaButton(123, 239, 112, 38);
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
  if (isTouchInBounds(x, y, 122, 84, 111, 33)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0; drawProbeZScreen(); return; }
  if (isTouchInBounds(x, y, 122, 120, 111, 33)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; drawProbeZScreen(); return; }
  if (isTouchInBounds(x, y, 5, 239, 112, 38)) { pendantProbeV2.returnScreen = PSCREEN_PROBE_Z; currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 123, 239, 112, 38)) { probeCycleWorkArea(); drawProbeZScreen(); return; }
  if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
    if (!pendantConnected) { probeDrawWarn(220, "! Not connected", true); return; }
    pendantProbeV2.confirmActive = true; drawProbeZScreen(); return;
  }
}
