/* screen_jog_homing.cpp port */

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

const SPD_X = 80, SPD_W = 80, SPD_Y = 277, SPD_H = 40;

// Button rows use the shared rowBtnX()/rowBtnWAt() grid from helpers.js, so HOME,
// JOG AXIS and the increment row all span 5..235 and align with each other.
const ROW_H = 38;
const HOME_Y = 115, AXIS_Y = 173;

// Increment row: 4 slots on the shared grid — rowBtnW(4) === 56, pitch 58.
const INC_Y = 231, INC_H = ROW_H;
const INC_W = 56;
function incX(i) { return rowBtnX(4, i); }

// Axis to restore when the coarse dial box hands the encoder back to jogging.
let _incDialPrevAxis = 0;

// Slot 3 is the coarse dial box — adjustable-field styling so it reads as
// something the encoder drives, yellow while incDialMode is live.
function drawIncDialButton() {
  const selected = pendantJog.selectedIncrement === 3;
  const active = pendantJog.incDialMode;
  const incs = currentIncrements();
  const bg = active ? PROBE_SEL_BG : (selected ? COLOR_ORANGE : COLOR_BUTTON_GRAY);
  const bdr = active ? PROBE_C_YELLOW : PROBE_C_TAPBDR;
  const x = incX(3);
  display.fillRoundRect(x, INC_Y, INC_W, INC_H, 5, bg);
  display.drawRoundRect(x, INC_Y, INC_W, INC_H, 5, bdr);
  display.setTextSize(1);
  display.setTextColor(active ? PROBE_C_YELLOW : COLOR_WHITE);
  const lw = display.textWidth("DIAL");
  display.setCursor(x + ((INC_W - lw) / 2 | 0), INC_Y + 5);
  display.print("DIAL");
  const v = incs.labels[3];
  display.setTextSize(2);
  display.setTextColor(active ? PROBE_C_YELLOW : COLOR_WHITE);
  const vw = display.textWidth(v);
  display.setCursor(x + ((INC_W - vw) / 2 | 0), INC_Y + 17);
  display.print(v);
}

function redrawJogSpeedButton() {
  // Adjustable-field style, matching the tap-to-edit buttons on the Probe
  // screens: a bordered box with a small label on top and the value below; the
  // border + value highlight (yellow) while speed-dial mode is active.
  const active = pendantJog.speedDialMode;
  const spd = pendantMachine.inInches ? pendantJog.jogSpeedIn : pendantJog.jogSpeedMm;
  const unit = pendantMachine.inInches ? "ipm" : "mm/m";
  probeDrawKVTouch(SPD_X, SPD_Y, SPD_W, SPD_H, "SPEED", spd, unit, COLOR_GREEN, active, 0);
}

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

// Redraws just the "JOG INCREMENT (unit) - mode" label row — refreshes on axis
// change (A → "deg", X/Y/Z → "mm"/"in") without a full-screen redraw.
function redrawJogIncrementLabel() {
  display.fillRect(5, 219, 230, 9, COLOR_BACKGROUND);
  display.setTextSize(1);
  display.setCursor(5, 219);
  if (pendantMachine.status.startsWith("Alarm")) {
    display.setTextColor(TFT_RED);
    display.print("JOG INCREMENT  *** " + pendantMachine.status + " ***");
  } else {
    display.setTextColor(COLOR_GRAY_TEXT);
    const unitStr = pendantJog.selectedAxis === 3 ? "deg" : (pendantMachine.inInches ? "in" : "mm");
    display.print("JOG INCREMENT (" + unitStr + ")");
  }
}

function drawJogHomingScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("JOG & HOMING");
  display.fillRoundRect(5, 40, 230, 55, 5, COLOR_DARKER_BG);
  updateJogAxisDisplay();

  const axisNames = ["X", "Y", "Z", "A"];
  const numAx = pendantMachine.numAxes;
  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 103); display.print("HOME");
  {
    const homeNames = ["X", "Y", "Z", numAx < 4 ? "ALL" : "A"];
    const numHome = numAx < 4 ? numAx + 1 : 4;
    for (let i = 0; i < numHome; i++) {
      const sz = (i === numAx && numAx < 4) ? 2 : 3;
      drawButton(rowBtnX(numHome, i), HOME_Y, rowBtnWAt(numHome, i), ROW_H,
                 homeNames[i], COLOR_DARK_GREEN, COLOR_WHITE, sz);
    }
  }

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 161); display.print("JOG AXIS");
  for (let i = 0; i < numAx; i++) {
    const bg = (!pendantJog.speedDialMode && i === pendantJog.selectedAxis) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(rowBtnX(numAx, i), AXIS_Y, rowBtnWAt(numAx, i), ROW_H, axisNames[i], bg, COLOR_WHITE, 3);
  }

  redrawJogIncrementLabel();

  {
    const incs = currentIncrements();
    for (let i = 0; i < 3; i++) {
      const bg = i === pendantJog.selectedIncrement ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
      drawButton(incX(i), INC_Y, INC_W, INC_H, incs.labels[i], bg, COLOR_WHITE, 2);
    }
    drawIncDialButton();   // slot 3 — coarse dial box
  }

  drawButton(5, SPD_Y, 73, SPD_H, "Main Menu", COLOR_BLUE, COLOR_WHITE, 1);
  redrawJogSpeedButton();
  drawButton(162, SPD_Y, 73, SPD_H, "Work Area", COLOR_BLUE, COLOR_WHITE, 1);
}

function updateJogAxisDisplay() {
  if (currentPendantScreen !== PSCREEN_JOG_HOMING) return;
  // MACHINE coordinates (workX/Y/Z/A = MPos); posX/Y/Z are work coords.
  const pos = [pendantMachine.workX, pendantMachine.workY, pendantMachine.workZ, pendantMachine.workA];
  const P = panel(230, 55, 5, 40);
  const g = P.g, ox = P.ox, oy = P.oy;
  g.fillRect(ox, oy, 230, 55, COLOR_DARKER_BG);

  if (pendantJog.speedDialMode) {
    g.setTextColor(COLOR_GREEN); g.setTextSize(1);
    let lw = g.textWidth("JOG SPEED");
    g.setCursor(ox + 115 - (lw / 2 | 0), oy + 5); g.print("JOG SPEED");
    const speedStr = pendantMachine.inInches
      ? "F:" + pendantJog.jogSpeedIn + " ipm"
      : "F:" + pendantJog.jogSpeedMm + " mm/m";
    g.setTextSize(2);
    let sw = g.textWidth(speedStr);
    g.setCursor(ox + 115 - (sw / 2 | 0), oy + 20); g.print(speedStr);
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    let hw = g.textWidth("Select an axis to jog");
    g.setCursor(ox + 115 - (hw / 2 | 0), oy + 42); g.print("Select an axis to jog");
  } else if (pendantJog.incDialMode) {
    // Coarse dial mode — mirrors the speed panel so the encoder's job is never
    // ambiguous: it's stepping the increment, not moving the machine.
    const incs = currentIncrements();
    const unit = pendantJog.selectedAxis === 3 ? "deg" : (pendantMachine.inInches ? "in" : "mm");
    g.setTextColor(PROBE_C_YELLOW); g.setTextSize(1);
    let lw = g.textWidth("JOG INCREMENT");
    g.setCursor(ox + 115 - (lw / 2 | 0), oy + 5); g.print("JOG INCREMENT");
    const incStr = incs.labels[3] + " " + unit;
    g.setTextSize(2);
    let sw = g.textWidth(incStr);
    g.setCursor(ox + 115 - (sw / 2 | 0), oy + 20); g.print(incStr);
    // Name the toggle explicitly — the second tap is the quick way back to jogging.
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    const hint = "Tap DIAL again to jog";
    let hw2 = g.textWidth(hint);
    g.setCursor(ox + 115 - (hw2 / 2 | 0), oy + 42); g.print(hint);
  } else {
    const axisNames = ["X", "Y", "Z", "A"];
    const inAlarm = pendantMachine.status.startsWith("Alarm");
    const dispAxis = pendantJog.homingAxis >= 0 ? pendantJog.homingAxis : pendantJog.selectedAxis;
    const decPlaces = pendantMachine.inInches ? 4 : 2;
    // A axis (rotary) → no "mm"/"in"; a degree symbol is drawn after the value.
    const isAAxis = dispAxis === 3;
    const val = fmtF(pos[dispAxis], decPlaces);
    let mainLine;
    if (inAlarm) mainLine = `${axisNames[dispAxis]} ${val} ${pendantMachine.status}`;
    else if (isAAxis) mainLine = `${axisNames[dispAxis]} ${val}`;
    else mainLine = `${axisNames[dispAxis]} ${val} ${pendantMachine.inInches ? "in" : "mm"}`;
    g.setTextColor(inAlarm ? TFT_RED : COLOR_GREEN); g.setTextSize(3);
    g.setCursor(ox + 5, oy + 5); g.print(mainLine);
    if (isAAxis && !inAlarm) {
      const iconX = ox + 5 + g.textWidth(mainLine) + 6;
      g.drawCircle(iconX, oy + 8, 3, COLOR_GREEN);
      g.drawCircle(iconX, oy + 8, 2, COLOR_GREEN);
    }

    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    const numAx = pendantMachine.numAxes;
    const colSpacing = numAx > 1 ? (230 / (numAx - 1)) | 0 : 230;
    let col = 5;
    for (let i = 0; i < numAx; i++) {
      if (i === dispAxis) continue;
      g.setCursor(ox + col, oy + 38);
      g.print(`${axisNames[i]}:${fmtF(pos[i], 2)}`);
      col += colSpacing;
    }
  }
}

function redrawJogAxisButtons() {
  if (currentPendantScreen !== PSCREEN_JOG_HOMING) return;
  const axisNames = ["X", "Y", "Z", "A"];
  const numAx = pendantMachine.numAxes;
  for (let i = 0; i < numAx; i++) {
    const bg = (!pendantJog.speedDialMode && i === pendantJog.selectedAxis) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(rowBtnX(numAx, i), AXIS_Y, rowBtnWAt(numAx, i), ROW_H, axisNames[i], bg, COLOR_WHITE, 3);
  }
  updateJogAxisDisplay();
}

function redrawJogIncrementButtons() {
  if (currentPendantScreen !== PSCREEN_JOG_HOMING) return;
  const incs = currentIncrements();
  for (let i = 0; i < 3; i++) {
    const bg = i === pendantJog.selectedIncrement ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(incX(i), INC_Y, INC_W, INC_H, incs.labels[i], bg, COLOR_WHITE, 2);
  }
  drawIncDialButton();   // slot 3 — coarse dial box
}

function handleJogHomingTouch(x, y) {
  const numAx = pendantMachine.numAxes;
  {
    const homeNames = ["X", "Y", "Z", numAx < 4 ? "ALL" : "A"];
    const numHome = numAx < 4 ? numAx + 1 : 4;
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
  if (isTouchInBounds(x, y, 5, SPD_Y, 73, SPD_H)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 162, SPD_Y, 73, SPD_H)) { currentPendantScreen = PSCREEN_PROBING_WORK; return; }
}
