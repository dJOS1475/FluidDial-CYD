/* screen_jog_homing.cpp port */

// HOME row label for slot i: one per reported axis, plus "ALL" when there is
// room for it.  Mirrors the firmware — sized from numAx, never a fixed table.
function homeLabel(numAx, i) { return (numAx < 4 && i === numAx) ? "ALL" : AXIS_NAMES[i]; }

// Slots 0-2 are fixed fine increments; slot 3 is the dial box, holding whichever
// coarse value pendantJog.coarseIdx selects.  Imperial mirrors metric one-for-one
// in real terms (.001"~0.01mm ... 4.0"~100mm) using round inch numbers.
const JOG_COARSE_COUNT = 3;
const kCoarseLabelsMm = ["10", "50", "100"];
const kCoarseValuesMm = [10.0, 50.0, 100.0];
const kCoarseLabelsIn = [".5", "2.0", "4.0"];
const kCoarseValuesIn = [0.5, 2.0, 4.0];

function currentIncrements() {
  const s = { labels: [], values: [] };
  const ci = constrain(pendantJog.coarseIdx, 0, JOG_COARSE_COUNT - 1);
  if (pendantMachine.inInches) {
    s.labels = [".001", ".010", ".100", kCoarseLabelsIn[ci]];
    s.values = [0.001, 0.010, 0.100, kCoarseValuesIn[ci]];
  } else {
    s.labels = ["0.01", "0.1", "1", kCoarseLabelsMm[ci]];
    s.values = [0.01, 0.1, 1.0, kCoarseValuesMm[ci]];
  }
  return s;
}

function jogApplyCoarseIncrement() {
  if (pendantJog.selectedIncrement !== 3) return;
  pendantJog.increment = currentIncrements().values[3];
}

// Layout (port of screen_jog_homing.cpp): MPos/WPos panel, then HOME, JOG
// AXIS and JOG INCREMENT rows under section labels, then Main Menu | SPEED |
// Work Area.  Button rows use the shared rowBtnX()/rowBtnWAt() grid.
const MM_X = 5, MM_W = 70, SPD_X = 78, SPD_W = 84, WA_X = 165, WA_W = 70;
const SPD_Y = 266, SPD_H = 50;
const ROW_H = 36;
const HOME_Y = 110, AXIS_Y = 166;
const AXIS_TS = 4, INC_TS = 2;        // text sizes: axis letters, increment labels

// Increment row: 4 slots on the shared grid — rowBtnW(4) === 56, pitch 58.
const INC_Y = 222, INC_H = 38;
const INC_W = 56;
function incX(i) { return rowBtnX(4, i); }

// Axis to restore when the coarse dial box hands the encoder back to jogging.
let _incDialPrevAxis = 0;

function drawIncDialButton() { drawIncDialButtonNew(); }
function redrawJogSpeedButton() { drawJogSpeedBoxNew(); }

function enterJogHoming() {
  releasePanelSprites();
  pendantJog.increment = currentIncrements().values[pendantJog.selectedIncrement];
  if (pendantJog.speedDialMode || pendantJog.incDialMode || pendantJog.selectedAxis < 0) {
    pendantJog.speedDialMode = false;
    pendantJog.incDialMode = false;
    pendantJog.selectedAxis = 0;
  }
  releasePanelSprites();
}
function exitJogHoming() { releasePanelSprites(); }

// Redraws just the "JOG INCREMENT (unit)" label row — refreshes on axis change
// (A → "deg", X/Y/Z → "mm"/"in") without a full-screen redraw.
function redrawJogIncrementLabel() { redrawJogIncrementLabelNew(); }

function drawJogHomingScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("JOG & HOMING");
  drawJogHomingNew();
}

function updateJogAxisDisplay() {
  if (currentPendantScreen !== PSCREEN_JOG_HOMING) return;
  updateJogPanelNew();
}

function redrawJogAxisButtons() {
  if (currentPendantScreen !== PSCREEN_JOG_HOMING) return;
  const numAx = pendantMachine.numAxes;
  for (let i = 0; i < numAx; i++) {
    const bg = (!pendantJog.speedDialMode && i === pendantJog.selectedAxis) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(rowBtnX(numAx, i), AXIS_Y, rowBtnWAt(numAx, i), ROW_H, AXIS_NAMES[i], bg, COLOR_WHITE, AXIS_TS);
  }
  updateJogAxisDisplay();
}

function redrawJogIncrementButtons() {
  if (currentPendantScreen !== PSCREEN_JOG_HOMING) return;
  const incs = currentIncrements();
  for (let i = 0; i < 3; i++) {
    const bg = i === pendantJog.selectedIncrement ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(incX(i), INC_Y, INC_W, INC_H, incs.labels[i], bg, COLOR_WHITE, INC_TS);
  }
  drawIncDialButton();   // slot 3 — coarse dial box
}

function handleJogHomingTouch(x, y) {
  const numAx = pendantMachine.numAxes;
  {
    const numHome = numAx < 4 ? numAx + 1 : numAx;
    for (let i = 0; i < numHome; i++) {
      if (isTouchInBounds(x, y, rowBtnX(numHome, i), HOME_Y, rowBtnWAt(numHome, i), ROW_H)) {
        if (!pendantConnected) return;
        if (i === numAx) { send_line("$H"); pendantJog.homingAxis = 0; }
        else { const an = ["X", "Y", "Z", "A"]; send_line("$H" + an[i]); pendantJog.homingAxis = i; }
        updateJogAxisDisplay();
        return;
      }
    }
  }
  for (let i = 0; i < numAx; i++) {
    if (isTouchInBounds(x, y, rowBtnX(numAx, i), AXIS_Y, rowBtnWAt(numAx, i), ROW_H)) {
      const leavingIncDial = pendantJog.incDialMode;
      pendantJog.speedDialMode = false;
      pendantJog.incDialMode = false;   // picking an axis hands the dial back to jogging
      pendantJog.selectedAxis = i;
      pendantJog.homingAxis = -1;
      redrawJogAxisButtons();
      redrawJogSpeedButton();
      if (leavingIncDial) redrawJogIncrementButtons();
      redrawJogIncrementLabel();   // A<->X/Y/Z changes the unit (deg vs mm/in)
      return;
    }
  }
  // Slots 0-2 are plain picks.  Slot 3 is the coarse dial box, which walks three
  // states so a tap can never take the encoder away mid-jog:
  //   tap 1 (not yet selected) — just SELECT the coarse value; you keep jogging
  //   tap 2 (already selected) — encoder steps 10/50/100 (axis deselected)
  //   tap 3 (adjusting)        — back to jogging, restoring the axis you were on
  for (let i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, incX(i), INC_Y, INC_W, INC_H)) {
      const wasSelected = pendantJog.selectedIncrement === i;
      pendantJog.selectedIncrement = i;
      pendantJog.increment = currentIncrements().values[i];
      saveJogPrefs();
      if (i === 3) {
        if (!wasSelected) {
          pendantJog.incDialMode = false;             // tap 1 — select only
        } else if (pendantJog.incDialMode) {
          // tap 3 — back to jogging with the value just set.
          pendantJog.incDialMode = false;
          pendantJog.selectedAxis = constrain(_incDialPrevAxis, 0, pendantMachine.numAxes - 1);
        } else {
          // tap 2 — the encoder now steps the coarse value.
          _incDialPrevAxis = pendantJog.selectedAxis >= 0 ? pendantJog.selectedAxis : 0;
          pendantJog.incDialMode = true;
          pendantJog.speedDialMode = false;
          pendantJog.selectedAxis = -1;
        }
        redrawJogAxisButtons();
        redrawJogSpeedButton();
      } else if (pendantJog.incDialMode) {
        pendantJog.incDialMode = false;
      }
      redrawJogIncrementButtons();
      return;
    }
  }
  if (isTouchInBounds(x, y, SPD_X, SPD_Y, SPD_W, SPD_H)) {
    const leavingIncDial = pendantJog.incDialMode;
    pendantJog.speedDialMode = true;
    pendantJog.incDialMode = false;   // only one dial mode at a time
    pendantJog.selectedAxis = -1;
    redrawJogAxisButtons();
    redrawJogSpeedButton();
    if (leavingIncDial) redrawJogIncrementButtons();
    updateJogAxisDisplay();
    return;
  }
  if (isTouchInBounds(x, y, MM_X, SPD_Y, MM_W, SPD_H)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, WA_X, SPD_Y, WA_W, SPD_H)) { currentPendantScreen = PSCREEN_PROBING_WORK; return; }
}

// ============================== Drawing ==============================

const JOG_PANEL_Y = 38, JOG_PANEL_H = 52;

// Section label: GLCD size 1 in the label colour, 4 px above its row — the
// same treatment every other screen uses (e.g. Work Area).
function jogSectionLabel(rowY, text, color = COLOR_GRAY_TEXT) {
  const g = display, y = rowY - 12;
  g.fillRect(5, y - 1, 230, 10, COLOR_BACKGROUND);
  g.setFont(null); g.setTextSize(1); g.setTextColor(color);
  g.setCursor(5, y); g.print(text);
  return 5 + g.textWidth(text);
}

function drawJogHomingNew() {
  uiDrawPanel(5, JOG_PANEL_Y, 230, JOG_PANEL_H);
  updateJogAxisDisplay();

  const numAx = pendantMachine.numAxes;
  jogSectionLabel(HOME_Y, "HOME");
  const numHome = numAx < 4 ? numAx + 1 : numAx;
  for (let i = 0; i < numHome; i++) {
    const isAll = i === numAx && numAx < 4;
    drawButton(rowBtnX(numHome, i), HOME_Y, rowBtnWAt(numHome, i), ROW_H,
               homeLabel(numAx, i), COLOR_DARK_GREEN, COLOR_WHITE, isAll ? 3 : AXIS_TS);
  }

  jogSectionLabel(AXIS_Y, "JOG AXIS");
  for (let i = 0; i < numAx; i++) {
    const bg = (!pendantJog.speedDialMode && i === pendantJog.selectedAxis) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(rowBtnX(numAx, i), AXIS_Y, rowBtnWAt(numAx, i), ROW_H, AXIS_NAMES[i], bg, COLOR_WHITE, AXIS_TS);
  }

  redrawJogIncrementLabel();
  const incs = currentIncrements();
  for (let i = 0; i < 3; i++) {
    const bg = i === pendantJog.selectedIncrement ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(incX(i), INC_Y, INC_W, INC_H, incs.labels[i], bg, COLOR_WHITE, INC_TS);
  }
  drawIncDialButton();

  jogSmallButton(MM_X, MM_W, "Main Menu");
  redrawJogSpeedButton();
  jogSmallButton(WA_X, WA_W, "Work Area");
}

// Bottom-row nav button: two lines of FreeSansBold, like the Main Menu buttons
// (one line won't fit the 70 px width in the bold font).
function jogSmallButton(x, w, label) {
  uiDrawButton(x, SPD_Y, w, SPD_H, label.split(" "), COLOR_BLUE, COLOR_WHITE, 2, null);
}

function redrawJogIncrementLabelNew() {
  const unitStr = pendantJog.selectedAxis === 3 ? "deg" : (pendantMachine.inInches ? "in" : "mm");
  if (pendantMachine.status.startsWith("Alarm")) {
    const g = display;
    const end = jogSectionLabel(INC_Y, "JOG INCREMENT", TFT_RED);
    g.setTextColor(TFT_RED);
    g.setCursor(end + 6, INC_Y - 12); g.print("*** " + pendantMachine.status + " ***");
  } else {
    jogSectionLabel(INC_Y, "JOG INCREMENT (" + unitStr + ")");
  }
}

// DIAL box: blue (or yellow when selected) button with a small "DIAL" caption
// over the coarse value; an inset field with a yellow edge while the encoder
// is stepping it.
function drawIncDialButtonNew() {
  const g = display;
  const selected = pendantJog.selectedIncrement === 3;
  const active = pendantJog.incDialMode;
  const x = incX(3);
  let fg;
  if (active) {
    g.fillRoundRect(x, INC_Y, INC_W, INC_H, 7, UI_FIELD_BG);
    g.drawRoundRect(x, INC_Y, INC_W, INC_H, 7, UI_BTN_YELLOW);
    g.drawRoundRect(x + 1, INC_Y + 1, INC_W - 2, INC_H - 2, 6, UI_BTN_YELLOW);
    fg = UI_BTN_YELLOW;
  } else {
    const c = uiButtonColors(selected ? COLOR_ORANGE : COLOR_BUTTON_GRAY, COLOR_WHITE);
    uiButtonFrame(x, INC_Y, INC_W, INC_H, c);
    fg = c.text;
  }
  g.setTextColor(fg); g.setTextSize(1);
  const v = currentIncrements().labels[3];
  jogCaptionValue(g, x, INC_W, INC_Y + INC_H / 2, "DIAL", fg,
                  [[GFX_FONTS.FreeSansBold9pt7b, v, fg]]);
}

// SPEED box: panel-style field, "SPEED" caption, value in green bold with the
// unit in Font2; yellow edge while the encoder is setting it.
function drawJogSpeedBoxNew() {
  const g = display;
  const active = pendantJog.speedDialMode;
  const spd = String(pendantMachine.inInches ? pendantJog.jogSpeedIn : pendantJog.jogSpeedMm);
  const unit = pendantMachine.inInches ? "ipm" : "mm/m";
  const x = SPD_X, y = SPD_Y, w = SPD_W, h = SPD_H;
  g.fillRoundRect(x, y, w, h, 7, UI_FIELD_BG);
  g.drawRoundRect(x, y, w, h, 7, active ? UI_BTN_YELLOW : UI_EDGE_LT);
  g.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 6, active ? UI_BTN_YELLOW : UI_EDGE);
  g.setTextSize(1);
  const vc = active ? UI_BTN_YELLOW : COLOR_GREEN;
  jogCaptionValue(g, x, w, y + h / 2, "SPEED", active ? UI_BTN_YELLOW : COLOR_WHITE,
                  [[GFX_FONTS.FreeSansBold9pt7b, spd, vc], [GFX_FONTS.Font2, unit, vc]]);
}

// A small Font2 caption over a value, the pair centred (caps to caps) on cy
// and each line centred across [x, x+w).  The value is a run of
// [font, text, colour] parts sharing one baseline, 3 px apart — e.g. "5000"
// in bold with "mm/m" in Font2.
function jogCaptionValue(g, x, w, cy, caption, capColor, parts) {
  const GAP = 5;
  g.setFont(GFX_FONTS.Font2);
  const [ca, cb] = uiCapBox(g);
  g.setFont(parts[0][0]);
  const [va, vb] = uiCapBox(g);
  const blockH = (cb - ca) + GAP + (vb - va);
  const capTop = Math.round(cy - blockH / 2);
  const valCapTop = capTop + (cb - ca) + GAP;
  // caption
  g.setFont(GFX_FONTS.Font2); g.setTextColor(capColor);
  g.setCursor(x + ((w - g.textWidth(caption)) / 2 | 0), capTop - ca); g.print(caption);
  // value parts on a shared baseline (baseline = cap bottom)
  const baseline = valCapTop + (vb - va);
  let total = 0;
  for (const [f, t] of parts) { g.setFont(f); total += g.textWidth(t); }
  total += 3 * (parts.length - 1);
  let px = x + ((w - total) / 2 | 0);
  for (const [f, t, col] of parts) {
    g.setFont(f); g.setTextColor(col);
    const [a, b] = uiCapBox(g);
    g.setCursor(px, baseline - b);
    g.print(t);
    px += g.textWidth(t) + 3;
  }
  g.setFont(null);
}

// Faux-bold Font2 (drawn twice, 1 px apart) for the MPos / WPos captions.
function jogBoldFont2(g, x, y, text, color) {
  g.setFont(GFX_FONTS.Font2); g.setTextColor(color);
  g.setCursor(x, y); g.print(text);
  g.setCursor(x + 1, y); g.print(text);
}

function updateJogPanelNew() {
  const g = display, ox = 5, oy = JOG_PANEL_Y, cx = 120;
  uiDrawPanel(ox, oy, 230, JOG_PANEL_H);
  g.setTextSize(1);

  // Dial modes take the panel over, so it is never
  // ambiguous what the knob is driving.
  if (pendantJog.speedDialMode || pendantJog.incDialMode) {
    let cap, val, col, hint;
    if (pendantJog.speedDialMode) {
      cap = "JOG SPEED"; col = COLOR_GREEN; hint = "tap an axis to jog";
      val = pendantMachine.inInches ? pendantJog.jogSpeedIn + " ipm" : pendantJog.jogSpeedMm + " mm/min";
    } else {
      const unit = pendantJog.selectedAxis === 3 ? "deg" : (pendantMachine.inInches ? "in" : "mm");
      cap = "JOG INCREMENT"; col = UI_BTN_YELLOW; hint = "tap DIAL to jog";
      val = currentIncrements().labels[3] + " " + unit;
    }
    g.setFont(GFX_FONTS.Font2);
    const line = cap + "  -  " + hint;
    g.setTextColor(col);
    g.setCursor(cx - (g.textWidth(line) / 2 | 0), oy + 4); g.print(line);
    g.setFont(GFX_FONTS.FreeSansBold12pt7b);
    g.setCursor(cx - (g.textWidth(val) / 2 | 0), oy + 23); g.print(val);
    g.setFont(null);
    return;
  }

  const numAx = pendantMachine.numAxes;
  const dec = pendantMachine.inInches ? 3 : 2;
  const hot = pendantJog.homingAxis >= 0 ? pendantJog.homingAxis : pendantJog.selectedAxis;
  const rows = [["MPos", UI_BTN_YELLOW, pendantMachine.mpos], ["WPos", UI_LABEL, pendantMachine.wpos]];
  const COL0 = ox + 42, PITCH = 61;

  if (numAx <= 3) {
    // His layout: one row per coordinate system, Font2 values in columns.
    rows.forEach(([lab, labCol, pos], r) => {
      const y = oy + 5 + r * 25;
      jogBoldFont2(g, ox + 5, y, lab, labCol);
      g.setFont(GFX_FONTS.Font2);
      for (let i = 0; i < numAx; i++) {
        g.setTextColor(i === hot ? COLOR_GREEN : COLOR_WHITE);   // the axis the knob moves
        g.setCursor(COL0 + i * PITCH, y);
        g.print(`${AXIS_NAMES[i]}:${fmtF(pos[i], dec)}`);
      }
    });
    g.drawFastHLine(ox + 5, oy + 25, 220, UI_EDGE);
  } else {
    // 4-6 axes don't fit one Font2 row: two GLCD lines of three per system.
    rows.forEach(([lab, labCol, pos], r) => {
      const y = oy + 4 + r * 24;
      g.setFont(null); g.setTextColor(labCol);
      g.setCursor(ox + 5, y + 5); g.print(lab);
      for (let i = 0; i < numAx; i++) {
        g.setTextColor(i === hot ? COLOR_GREEN : COLOR_WHITE);
        g.setCursor(COL0 + (i % 3) * PITCH, y + (i < 3 ? 0 : 10));
        g.print(`${AXIS_NAMES[i]}:${fmtF(pos[i], dec)}`);
      }
    });
    g.drawFastHLine(ox + 5, oy + 25, 220, UI_EDGE);
  }
  g.setFont(null);
}
