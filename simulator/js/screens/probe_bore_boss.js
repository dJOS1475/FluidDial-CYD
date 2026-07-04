/* screen_probe_bore_boss.cpp port — SCR3a Bore / SCR3b Boss
 *
 * Both find the XY centre of a round feature and set X0/Y0 only — Z work-zero is
 * handled by the Z Surface probe.  Every wall is reached with a G38.2 two-pass
 * (fast seek + slow re-probe), never a blind rapid.  Four points are probed at
 * 90° (±X, ±Y); the centre is the midpoint of each opposed pair, which cancels
 * the tip radius per axis and needs no division.  The X pair is probed first;
 * the tool re-centres in X before the Y pair so the +Y/-Y probes run through the
 * true vertical diameter and contact head-on even from an off-centre start. */

// Two-pass probe along a unit direction (ux,uy): fast seek, back off, slow
// re-probe. Ends at the fine trigger (machine pos in #5061/#5062). G91.
function probeRadial2Pass(ux, uy, seekDist, seekF, fineF) {
  const BACKOFF = 1.5;
  send_line(`G38.2 G91 X${fmtF(seekDist * ux, 3)} Y${fmtF(seekDist * uy, 3)} F${fmtF(seekF, 0)}`);
  send_line(`G0 G91 X${fmtF(-BACKOFF * ux, 3)} Y${fmtF(-BACKOFF * uy, 3)} F1000`);
  send_line(`G38.2 G91 X${fmtF((BACKOFF + 1) * ux, 3)} Y${fmtF((BACKOFF + 1) * uy, 3)} F${fmtF(fineF, 0)}`);
}

// Two-pass probe along (ux,uy), then store the along-axis machine-coord result:
// axisX -> #5061 (X wall), else #5062 (Y wall).
function probeWallStore(ux, uy, seek, seekF, fineF, storeVar, axisX) {
  probeRadial2Pass(ux, uy, seek, seekF, fineF);
  send_line(`${storeVar} = #${axisX ? "5061" : "5062"}`);
}

// Move to the found centre (machine #<xc>,#<yc>) and zero X/Y there.
function emitMoveCentreZero(pNum) {
  send_line("G53 G0 X#<xc> Y#<yc>");
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
  // X pair along Y = start, average to the centre X, then re-centre X.
  probeWallStore(1.0, 0.0, d, seekF, fineF, "#<ax>", true);
  send_line("G90 G0 X#<sx> Y#<sy> F1000");
  probeWallStore(-1.0, 0.0, d, seekF, fineF, "#<cx>", true);
  send_line("G90 G0 X#<sx> Y#<sy> F1000");
  send_line("#<xc> = [[#<ax> + #<cx>] / 2]");
  send_line("G53 G0 X#<xc>");                   // re-centre X (Y stays at start)
  // Y pair through the centred X (true vertical diameter).
  probeWallStore(0.0, 1.0, d, seekF, fineF, "#<by>", false);
  send_line("G90 G0 Y#<sy> F1000");             // Y back to start (X stays centred)
  probeWallStore(0.0, -1.0, d, seekF, fineF, "#<dy>", false);
  send_line("#<yc> = [[#<by> + #<dy>] / 2]");
  emitMoveCentreZero(pNum);
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
  drawSeqStep(8, 87, 1, "Probe 4 points", true);
  drawSeqStep(8, 105, 2, "Find centre", false);
  drawSeqStep(8, 123, 3, "Set X0 Y0", false);
  drawBoreDiagram();
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(122, 73); display.print("SETTINGS");
  const fo = pendantProbeV2.focusedField;
  probeDrawKVTouch(122, 84, 111, 27, "Nominal dia.", pendantProbeV2.boreDia, "mm", PROBE_C_BLUE, fo === 0, 3);
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
  probeDrawWarn(220, "! Place tip inside the bore");
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

// One boss wall: move clear along (ux,uy) at safe Z, plunge beside the boss,
// two-pass probe INWARD (-ux,-uy), store the along-axis result, then lift.
function bossWallStore(ux, uy, out, inSeek, plunge, seekF, fineF, storeVar, axisX) {
  send_line(`G0 G91 X${fmtF(out * ux, 3)} Y${fmtF(out * uy, 3)} F1000`);
  send_line(`G0 G91 Z-${fmtF(plunge, 3)} F500`);
  probeWallStore(-ux, -uy, inSeek, seekF, fineF, storeVar, axisX);   // inward
  send_line(`G0 G91 Z${fmtF(plunge, 3)} F500`);                      // lift
}

// Persistent triple-tap state for the boss shape toggle (mirrors the firmware's
// function-static shapeTapCount / shapeTapMs).
const _bossShapeTap = { count: 0, ms: 0 };

function runProbeBoss() {
  if (!pendantConnected) return;
  const p = pendantProbeV2;
  const pNum = pendantProbing.selectedCoordIndex + 1;
  const seekF = p.seekRate, fineF = p.probeRate, depth = p.bossDepth, clear = p.bossClear;
  const retZ = p.retractDist, maxZ = p.maxZTravel;
  const platZ = probeIs3D() ? probeTipOffset3D() : p.plateThick;
  // Half-size per axis. Circular: both = bossDia/2. Rect: X=bossDia/2, Y=bossSizeY/2.
  const radX = p.bossDia / 2;
  const radY = (p.bossRect ? p.bossSizeY : p.bossDia) / 2;
  const outX = radX + clear, inSeekX = clear + radX + 5;
  const outY = radY + clear, inSeekY = clear + radY + 5;
  const plunge = depth + retZ;
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
  // X pair along Y = start, average to the centre X, then re-centre X.
  bossWallStore(1.0, 0.0, outX, inSeekX, plunge, seekF, fineF, "#<ax>", true);
  send_line("G90 G0 X#<sx> Y#<sy> F1000");
  bossWallStore(-1.0, 0.0, outX, inSeekX, plunge, seekF, fineF, "#<cx>", true);
  send_line("G90 G0 X#<sx> Y#<sy> F1000");
  send_line("#<xc> = [[#<ax> + #<cx>] / 2]");
  send_line("G53 G0 X#<xc>");                   // re-centre X at safe Z (Y at start)
  // Y pair through the centred X.
  bossWallStore(0.0, 1.0, outY, inSeekY, plunge, seekF, fineF, "#<by>", false);
  send_line("G90 G0 Y#<sy> F1000");             // Y back to start (X stays centred)
  bossWallStore(0.0, -1.0, outY, inSeekY, plunge, seekF, fineF, "#<dy>", false);
  send_line("#<yc> = [[#<by> + #<dy>] / 2]");
  emitMoveCentreZero(pNum);   // sets X0 Y0; Z0 already set at the top
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

// Top-down diagram of RECTANGULAR boss probing: boss outline (plan view) with
// four inward arrows probing the ±X/±Y faces and a Z0 tick at the centre. The
// top-down projection is the visual cue that rectangular mode is active.
function drawBossDiagramRect() {
  display.drawRect(40, 161, 41, 29, PROBE_C_LBLUE);   // boss outline (plan view)
  // +X (from the right)
  display.drawLine(94, 175, 83, 175, PROBE_C_GREEN);
  display.drawLine(83, 175, 87, 172, PROBE_C_GREEN);
  display.drawLine(83, 175, 87, 178, PROBE_C_GREEN);
  // -X (from the left)
  display.drawLine(26, 175, 37, 175, PROBE_C_GREEN);
  display.drawLine(37, 175, 33, 172, PROBE_C_GREEN);
  display.drawLine(37, 175, 33, 178, PROBE_C_GREEN);
  // +Y (from the top)
  display.drawLine(60, 145, 60, 158, PROBE_C_GREEN);
  display.drawLine(60, 158, 57, 154, PROBE_C_GREEN);
  display.drawLine(60, 158, 63, 154, PROBE_C_GREEN);
  // -Y (from the bottom)
  display.drawLine(60, 205, 60, 192, PROBE_C_GREEN);
  display.drawLine(60, 192, 57, 196, PROBE_C_GREEN);
  display.drawLine(60, 192, 63, 196, PROBE_C_GREEN);
  // Z0 tick at the centre.
  display.fillCircle(60, 175, 2, PROBE_C_YELLOW);
  display.setTextSize(1); display.setTextColor(PROBE_C_YELLOW);
  display.setCursor(64, 172); display.print("Z0");
}

function drawProbeBossScreen() {
  display.fillScreen(PROBE_BG_SCREEN);
  drawTitle("BOSS");
  probeDrawPosPanel(38);
  display.fillRoundRect(5, 70, 230, 146, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, 73); display.print("SEQUENCE");
  drawSeqStep(8, 87, 1, "Touch top->Z0", true);
  drawSeqStep(8, 105, 2, "Probe 4 points", false);
  drawSeqStep(8, 123, 3, "Set X0 Y0", false);
  if (pendantProbeV2.bossRect) drawBossDiagramRect();
  else drawBossDiagram();
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(122, 73); display.print("SETTINGS");
  const fo = pendantProbeV2.focusedField;
  if (pendantProbeV2.bossRect) {
    // 0=X size (bossDia) 1=Y size (bossSizeY) 2=Probe depth Z 3=Clearance
    probeDrawKVTouch(122, 84, 111, 27, "X size", pendantProbeV2.bossDia, "mm", PROBE_C_BLUE, fo === 0, 3);
    probeDrawKVTouch(122, 113, 111, 27, "Y size", pendantProbeV2.bossSizeY, "mm", PROBE_C_BLUE, fo === 1, 3);
    probeDrawKVTouch(122, 142, 111, 27, "Probe depth Z", pendantProbeV2.bossDepth, "mm", PROBE_C_BLUE, fo === 2, 3);
    probeDrawKVTouch(122, 171, 111, 27, "Clearance", pendantProbeV2.bossClear, "mm", PROBE_C_BLUE, fo === 3, 3);
  } else {
    // 0=Nominal dia. (bossDia) 1=Probe depth Z 2=Clearance
    probeDrawKVTouch(122, 84, 111, 27, "Nominal dia.", pendantProbeV2.bossDia, "mm", PROBE_C_BLUE, fo === 0, 3);
    probeDrawKVTouch(122, 113, 111, 27, "Probe depth Z", pendantProbeV2.bossDepth, "mm", PROBE_C_BLUE, fo === 1, 3);
    probeDrawKVTouch(122, 142, 111, 27, "Clearance", pendantProbeV2.bossClear, "mm", PROBE_C_BLUE, fo === 2, 3);
  }
  {
    const s = "Sets X0 Y0 Z0";
    display.setTextSize(1); display.setTextColor(PROBE_C_GREEN);
    display.setCursor(177 - (display.textWidth(s) / 2 | 0), pendantProbeV2.bossRect ? 205 : 182);
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
  // Field 0 (Nominal dia. / X size) doubles as the shape toggle: triple-tap
  // within 600 ms flips circular <-> rectangular. Single taps focus for editing.
  let redraw = false;
  if (isTouchInBounds(x, y, 122, 84, 111, 27)) {
    const now = (typeof millis === "function") ? millis() : Date.now();
    _bossShapeTap.count = (now - _bossShapeTap.ms < 600) ? _bossShapeTap.count + 1 : 1;
    _bossShapeTap.ms = now;
    if (_bossShapeTap.count >= 3) {
      _bossShapeTap.count = 0;
      pendantProbeV2.bossRect = !pendantProbeV2.bossRect;
      const maxField = pendantProbeV2.bossRect ? 3 : 2;
      if (pendantProbeV2.focusedField > maxField) pendantProbeV2.focusedField = -1;
      saveProbeSettings();
      drawProbeBossScreen();
      return;
    }
    pendantProbeV2.focusedField = pendantProbeV2.focusedField === 0 ? -1 : 0;
    redraw = true;
  } else {
    _bossShapeTap.count = 0;   // any other tap breaks the triple-tap sequence
    if (isTouchInBounds(x, y, 122, 113, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 1 ? -1 : 1; redraw = true; }
    if (isTouchInBounds(x, y, 122, 142, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 2 ? -1 : 2; redraw = true; }
    if (pendantProbeV2.bossRect && isTouchInBounds(x, y, 122, 171, 111, 27)) { pendantProbeV2.focusedField = pendantProbeV2.focusedField === 3 ? -1 : 3; redraw = true; }
  }
  if (redraw) { drawProbeBossScreen(); return; }
  if (isTouchInBounds(x, y, 5, 239, 112, 38)) { pendantProbeV2.returnScreen = PSCREEN_PROBE_BOSS; currentPendantScreen = PSCREEN_PROBE; return; }
  if (isTouchInBounds(x, y, 123, 239, 112, 38)) { probeCycleWorkArea(); drawProbeBossScreen(); return; }
  if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
    if (!pendantConnected) { probeDrawWarn(204, "! Not connected", true); return; }
    pendantProbeV2.confirmActive = true; drawProbeBossScreen(); return;
  }
}
