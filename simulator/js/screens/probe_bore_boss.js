/* screen_probe_bore_boss.cpp port — SCR3a Bore / SCR3b Boss
 *
 * Both find the XY centre of a round feature and set X0/Y0 only — Z work-zero is
 * handled by the Z Surface probe.  Every wall is reached with a G38.2 two-pass
 * (fast seek + slow re-probe), never a blind rapid, and the X centre is found
 * before the Y pair so Y runs through the true diameter. */

// Three probe directions, 120° apart (unit vectors). Shared by bore (outward)
// and boss (inward = negated).
const kDir3 = [[0.0, 1.0], [-0.866025, -0.5], [0.866025, -0.5]];
const kPX = ["#<ax>", "#<bx>", "#<cx>"];
const kPY = ["#<ay>", "#<by>", "#<cy>"];

// Two-pass probe along a unit direction (ux,uy): fast seek, back off, slow
// re-probe. Ends at the fine trigger (machine pos in #5061/#5062). G91.
function probeRadial2Pass(ux, uy, seekDist, seekF, fineF) {
  const BACKOFF = 1.5;
  send_line(`G38.2 G91 X${fmtF(seekDist * ux, 3)} Y${fmtF(seekDist * uy, 3)} F${fmtF(seekF, 0)}`);
  send_line(`G0 G91 X${fmtF(-BACKOFF * ux, 3)} Y${fmtF(-BACKOFF * uy, 3)} F1000`);
  send_line(`G38.2 G91 X${fmtF((BACKOFF + 1) * ux, 3)} Y${fmtF((BACKOFF + 1) * uy, 3)} F${fmtF(fineF, 0)}`);
}

// From the 3 saved machine-coord points, fit the circle circumcentre (determinant
// form), G53-move to it, and zero X/Y there.
function emitCircleCentreAndZero(pNum) {
  send_line("#<a2> = [#<ax>*#<ax> + #<ay>*#<ay>]");
  send_line("#<b2> = [#<bx>*#<bx> + #<by>*#<by>]");
  send_line("#<c2> = [#<cx>*#<cx> + #<cy>*#<cy>]");
  send_line("#<dd> = [2 * [#<ax>*[#<by>-#<cy>] + #<bx>*[#<cy>-#<ay>] + #<cx>*[#<ay>-#<by>]]]");
  send_line("#<ux> = [[#<a2>*[#<by>-#<cy>] + #<b2>*[#<cy>-#<ay>] + #<c2>*[#<ay>-#<by>]] / #<dd>]");
  send_line("#<uy> = [[#<a2>*[#<cx>-#<bx>] + #<b2>*[#<ax>-#<cx>] + #<c2>*[#<bx>-#<ax>]] / #<dd>]");
  send_line("G53 G0 X#<ux> Y#<uy>");
  send_line(`G10 L20 P${pNum} X0 Y0`);
}

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
  const seekF = p.seekRate, fineF = p.probeRate, rad = p.boreDia / 2, wallOf = p.boreOffset;
  const d = 2 * rad + wallOf + 3;   // generous outward seek
  probeActivateWcs();          // zero into the system shown on screen
  send_line("G21 G90");
  send_line("#<sx> = #5420");
  send_line("#<sy> = #5421");
  for (let i = 0; i < 3; i++) {
    probeRadial2Pass(kDir3[i][0], kDir3[i][1], d, seekF, fineF);
    send_line(`${kPX[i]} = #5061`);
    send_line(`${kPY[i]} = #5062`);
    send_line("G90 G0 X#<sx> Y#<sy> F1000");
  }
  emitCircleCentreAndZero(pNum);
}

// Top-down diagram of bore probing: hole outline, outward arrows to the walls,
// and a centre mark (XY centre finding).
function drawBoreDiagram() {
  // Inverted boss: a pocket recessed into the stock (cross-section). Stylus into
  // the hole; outward arrows probe the side walls (XY).
  display.fillRect(18, 182, 84, 20, PROBE_C_DIMBLUE);   // stock block
  display.fillRect(46, 182, 28, 13, PROBE_BG_PANEL);    // bored pocket (void, boss-width)
  display.fillRoundRect(55, 150, 10, 12, 2, PROBE_C_LBLUE);  // probe body/stem
  display.drawLine(60, 161, 60, 189, PROBE_C_YELLOW);   // stylus
  display.fillCircle(60, 191, 2, PROBE_C_YELLOW);
  display.drawLine(60, 188, 48, 188, PROBE_C_GREEN);    // left shaft (outward)
  display.drawLine(48, 188, 52, 185, PROBE_C_GREEN);
  display.drawLine(48, 188, 52, 191, PROBE_C_GREEN);
  display.drawLine(60, 188, 72, 188, PROBE_C_GREEN);    // right shaft (outward)
  display.drawLine(72, 188, 68, 185, PROBE_C_GREEN);
  display.drawLine(72, 188, 68, 191, PROBE_C_GREEN);
}

function drawProbeBoreScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("BORE");
  probeDrawPosPanel(38);
  display.fillRoundRect(5, 70, 230, 146, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 73); display.print("SEQUENCE");
  drawSeqStep(8, 87, 1, "Probe 3 points", true);
  drawSeqStep(8, 105, 2, "Find centre", false);
  drawSeqStep(8, 123, 3, "Set X0 Y0", false);
  drawBoreDiagram();
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(122, 73); display.print("SETTINGS");
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(122, 84, 111, 27, "Nominal dia.", pendantProbeV2.boreDia, "mm", PROBE_C_YELLOW, fo === 0, 3);
  probeDrawKVTouch(122, 113, 111, 27, "Wall offset", pendantProbeV2.boreOffset, "mm", PROBE_C_BLUE, fo === 1, 3);
  display.setTextSize(1);
  {
    const s = "Sets X0 Y0";
    display.setTextColor(PROBE_C_GREEN);
    display.setCursor(177 - (display.textWidth(s) / 2 | 0), 150);
    display.print(s);
    const a = "Z work-zero:";
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(177 - (display.textWidth(a) / 2 | 0), 168);
    display.print(a);
    display.setTextColor(PROBE_C_GREEN);
    const b = "use Z Surface";
    display.setCursor(177 - (display.textWidth(b) / 2 | 0), 182);
    display.print(b);
    const c = "probe routine";
    display.setCursor(177 - (display.textWidth(c) / 2 | 0), 196);
    display.print(c);
  }
  probeDrawWarn(220, "! Place tip inside the bore", true);
  drawButton(5, 239, 112, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  probeDrawWorkAreaButton(123, 239, 112, 38);
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
  if (redraw) { drawProbeBoreScreen(); return; }
  if (isTouchInBounds(x, y, 5, 239, 112, 38)) { pendantProbeV2.returnScreen = PSCREEN_PROBE_BORE; currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 123, 239, 112, 38)) { probeCycleWorkArea(); drawProbeBoreScreen(); return; }
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
  const seekF = p.seekRate, fineF = p.probeRate, depth = p.bossDepth, rad = p.bossDia / 2, clear = p.bossClear;
  const retZ = p.retractDist, maxZ = p.maxZTravel;
  const platZ = probeIs3D() ? p.ballDia / 2 : p.plateThick;
  const out = rad + clear, inSeek = clear + rad + 5, plunge = depth + retZ;
  probeActivateWcs();          // zero into the system shown on screen
  send_line("G21 G90");
  send_line("#<sx> = #5420");
  send_line("#<sy> = #5421");
  // Touch the top and set Z0 there (the boss top is the Z datum), then lift.
  send_line("G91");
  send_line(`G38.2 Z-${fmtF(maxZ, 3)} F${fmtF(fineF, 0)}`);
  send_line("G90");
  send_line(`G10 L20 P${pNum} Z${fmtF(platZ, 3)}`);
  send_line("G91");
  send_line(`G0 Z${fmtF(retZ, 3)} F500`);
  send_line("G90");
  for (let i = 0; i < 3; i++) {
    const ux = kDir3[i][0], uy = kDir3[i][1];
    send_line(`G0 G91 X${fmtF(out * ux, 3)} Y${fmtF(out * uy, 3)} F1000`);
    send_line(`G0 G91 Z-${fmtF(plunge, 3)} F500`);
    probeRadial2Pass(-ux, -uy, inSeek, seekF, fineF);
    send_line(`${kPX[i]} = #5061`);
    send_line(`${kPY[i]} = #5062`);
    send_line(`G0 G91 Z${fmtF(plunge, 3)} F500`);
    send_line("G90 G0 X#<sx> Y#<sy> F1000");
  }
  emitCircleCentreAndZero(pNum);   // sets X0 Y0; Z0 already set at the top
}

// Side-view diagram of boss probing: stylus touching the raised top (Z0) and
// inward arrows probing the side walls (XY centre).
function drawBossDiagram() {
  // Raised boss on the stock (cross-section). Stylus touches the top (Z0);
  // arrows probe inward to the side walls (XY).
  display.fillRect(18, 196, 84, 9, PROBE_C_DIMBLUE);   // stock slab
  display.fillRect(46, 182, 28, 14, PROBE_C_LBLUE);    // raised boss
  display.fillRoundRect(55, 141, 10, 12, 2, PROBE_C_LBLUE);  // probe body/stem
  display.drawLine(60, 152, 60, 180, PROBE_C_YELLOW);  // stylus
  display.fillCircle(60, 182, 2, PROBE_C_YELLOW);      // ball on top
  display.drawLine(57, 165, 60, 169, PROBE_C_YELLOW);  // down chevron
  display.drawLine(63, 165, 60, 169, PROBE_C_YELLOW);
  display.drawLine(26, 189, 44, 189, PROBE_C_GREEN);   // left shaft (inward)
  display.drawLine(44, 189, 40, 186, PROBE_C_GREEN);
  display.drawLine(44, 189, 40, 192, PROBE_C_GREEN);
  display.drawLine(94, 189, 76, 189, PROBE_C_GREEN);   // right shaft (inward)
  display.drawLine(76, 189, 80, 186, PROBE_C_GREEN);
  display.drawLine(76, 189, 80, 192, PROBE_C_GREEN);
}

function drawProbeBossScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("BOSS");
  probeDrawPosPanel(38);
  display.fillRoundRect(5, 70, 230, 146, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 73); display.print("SEQUENCE");
  drawSeqStep(8, 87, 1, "Touch top->Z0", true);
  drawSeqStep(8, 105, 2, "Probe 3 points", false);
  drawSeqStep(8, 123, 3, "Set X0 Y0", false);
  drawBossDiagram();
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(122, 73); display.print("SETTINGS");
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(122, 84, 111, 27, "Nominal dia.", pendantProbeV2.bossDia, "mm", PROBE_C_YELLOW, fo === 0, 3);
  probeDrawKVTouch(122, 113, 111, 27, "Probe depth", pendantProbeV2.bossDepth, "mm", PROBE_C_RED, fo === 1, 3);
  probeDrawKVTouch(122, 142, 111, 27, "Clearance", pendantProbeV2.bossClear, "mm", PROBE_C_BLUE, fo === 2, 3);
  {
    const s = "Sets X0 Y0 Z0";
    display.setTextSize(1); display.setTextColor(PROBE_C_GREEN);
    display.setCursor(177 - (display.textWidth(s) / 2 | 0), 182);
    display.print(s);
  }
  probeDrawWarn(220, "! Start above centre of boss");
  drawButton(5, 239, 112, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
  probeDrawWorkAreaButton(123, 239, 112, 38);
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
  if (isTouchInBounds(x, y, 5, 239, 112, 38)) { pendantProbeV2.returnScreen = PSCREEN_PROBE_BOSS; currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 123, 239, 112, 38)) { probeCycleWorkArea(); drawProbeBossScreen(); return; }
  if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
    if (!pendantConnected) { probeDrawWarn(204, "! Not connected", true); return; }
    pendantProbeV2.confirmActive = true; drawProbeBossScreen(); return;
  }
}
