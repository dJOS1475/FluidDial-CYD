/* screen_jog_homing.cpp port */

function currentIncrements() {
  const s = { labels: [], values: [] };
  if (pendantMachine.inInches) {
    if (pendantJog.fineIncrements) {
      s.labels = [".0001", ".001", ".010", ".100"];
      s.values = [0.0001, 0.001, 0.01, 0.1];
    } else {
      s.labels = [".05", ".5", "2.0", "4.0"];
      s.values = [0.05, 0.5, 2.0, 4.0];
    }
  } else {
    if (pendantJog.fineIncrements) {
      s.labels = ["0.01", "0.1", "1", "10"];
      s.values = [0.01, 0.1, 1.0, 10.0];
    } else {
      s.labels = ["1", "10", "50", "100"];
      s.values = [1.0, 10.0, 50.0, 100.0];
    }
  }
  return s;
}

const SPD_X = 80, SPD_W = 80, SPD_Y = 277, SPD_H = 40;
let _incTapCount = 0, _incTapMs = 0;

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
  _incTapCount = 0; _incTapMs = 0;
  if (pendantJog.speedDialMode || pendantJog.selectedAxis < 0) {
    pendantJog.speedDialMode = false;
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
    const modeStr = pendantJog.fineIncrements ? " - fine" : " - coarse";
    display.print("JOG INCREMENT (" + unitStr + ")" + modeStr);
  }
}

function drawJogHomingScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("JOG & HOMING");
  display.fillRoundRect(5, 40, 230, 55, 5, COLOR_DARKER_BG);
  updateJogAxisDisplay();

  const axisNames = ["X", "Y", "Z", "A"];
  const numAx = pendantMachine.numAxes;
  const btnW = (230 / numAx) | 0;

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 103); display.print("HOME");
  {
    const HW = 57;
    const homeNames = ["X", "Y", "Z", numAx < 4 ? "ALL" : "A"];
    const numHome = numAx < 4 ? numAx + 1 : 4;
    for (let i = 0; i < numHome; i++) {
      const sz = (i === numAx && numAx < 4) ? 2 : 3;
      drawButton(5 + i * HW, 115, HW - 4, 38, homeNames[i], COLOR_DARK_GREEN, COLOR_WHITE, sz);
    }
  }

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 161); display.print("JOG AXIS");
  for (let i = 0; i < numAx; i++) {
    const bg = (!pendantJog.speedDialMode && i === pendantJog.selectedAxis) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * btnW, 173, btnW - 4, 38, axisNames[i], bg, COLOR_WHITE, 3);
  }

  redrawJogIncrementLabel();

  const incs = currentIncrements();
  for (let i = 0; i < 4; i++) {
    const bg = i === pendantJog.selectedIncrement ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 56, 231, 52, 38, incs.labels[i], bg, COLOR_WHITE, 2);
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
  const btnW = (230 / numAx) | 0;
  for (let i = 0; i < numAx; i++) {
    const bg = (!pendantJog.speedDialMode && i === pendantJog.selectedAxis) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * btnW, 173, btnW - 4, 38, axisNames[i], bg, COLOR_WHITE, 3);
  }
  updateJogAxisDisplay();
}

function redrawJogIncrementButtons() {
  if (currentPendantScreen !== PSCREEN_JOG_HOMING) return;
  const incs = currentIncrements();
  for (let i = 0; i < 4; i++) {
    const bg = i === pendantJog.selectedIncrement ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 56, 231, 52, 38, incs.labels[i], bg, COLOR_WHITE, 2);
  }
}

function handleJogHomingTouch(x, y) {
  const numAx = pendantMachine.numAxes;
  const btnW = (230 / numAx) | 0;
  {
    const HW = 57;
    const homeNames = ["X", "Y", "Z", numAx < 4 ? "ALL" : "A"];
    const numHome = numAx < 4 ? numAx + 1 : 4;
    for (let i = 0; i < numHome; i++) {
      if (isTouchInBounds(x, y, 5 + i * HW, 115, HW - 4, 38)) {
        if (!pendantConnected) return;
        if (i === numAx) { send_line("$H"); pendantJog.homingAxis = 0; }
        else { const an = ["X", "Y", "Z", "A"]; send_line("$H" + an[i]); pendantJog.homingAxis = i; }
        updateJogAxisDisplay();
        return;
      }
    }
  }
  for (let i = 0; i < numAx; i++) {
    if (isTouchInBounds(x, y, 5 + i * btnW, 173, btnW - 4, 38)) {
      pendantJog.speedDialMode = false;
      pendantJog.selectedAxis = i;
      pendantJog.homingAxis = -1;
      redrawJogAxisButtons();
      redrawJogSpeedButton();
      redrawJogIncrementLabel();   // A<->X/Y/Z changes the unit (deg vs mm/in)
      return;
    }
  }
  for (let i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, 5 + i * 56, 231, 52, 38)) {
      if (i === 3) {
        const now = millis();
        if (now - _incTapMs < 600) _incTapCount++; else _incTapCount = 1;
        _incTapMs = now;
        if (_incTapCount >= 3) {
          _incTapCount = 0;
          pendantJog.fineIncrements = !pendantJog.fineIncrements;
          pendantJog.increment = currentIncrements().values[pendantJog.selectedIncrement];
          saveJogPrefs();
          drawJogHomingScreen();
          return;
        }
      } else _incTapCount = 0;
      pendantJog.selectedIncrement = i;
      pendantJog.increment = currentIncrements().values[i];
      saveJogPrefs();
      redrawJogIncrementButtons();
      return;
    }
  }
  if (isTouchInBounds(x, y, SPD_X, SPD_Y, SPD_W, SPD_H)) {
    pendantJog.speedDialMode = true;
    pendantJog.selectedAxis = -1;
    redrawJogAxisButtons();
    redrawJogSpeedButton();
    updateJogAxisDisplay();
    return;
  }
  if (isTouchInBounds(x, y, 5, SPD_Y, 73, SPD_H)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
  if (isTouchInBounds(x, y, 162, SPD_Y, 73, SPD_H)) { currentPendantScreen = PSCREEN_PROBING_WORK; return; }
}
