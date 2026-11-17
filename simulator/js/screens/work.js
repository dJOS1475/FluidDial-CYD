/* screen_probing_work.cpp port (Work Area) */

function enterProbingWork() { releasePanelSprites(); }
function exitProbingWork() { releasePanelSprites(); }

function drawProbingWorkScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("WORK AREA");

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 43); display.print("COORDINATE SYSTEM");
  redrawWorkCoordButtons();

  // Both readouts in one framed panel split by a rule, like the MPos / WPos
  // panel on Jog & Homing.
  uiDrawPanel(5, WA_PANEL_Y, 230, WA_PANEL_H);
  display.drawFastHLine(10, WA_RULE_Y, 220, UI_EDGE);
  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(10, WA_MPOS_CAP_Y); display.print("MACHINE POS");
  display.setCursor(10, WA_WPOS_CAP_Y); display.print("WORK POS");
  updateWorkMachinePos();
  updateWorkAreaPos();

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 218); display.print("SET WORK ZERO");
  {
    const numAx = pendantMachine.numAxes;
    const totalBtns = numAx + 1;
    const btnW = (230 / totalBtns) | 0;
    for (let i = 0; i < numAx; i++)
      drawButton(5 + i * btnW, 230, btnW - 2, 38, AXIS_NAMES[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
    // Five or six axes leave only 36 / 30 px for ALL — too narrow for 9pt bold,
    // so it drops to bold Font2.
    if (numAx >= 5) uiDrawNarrowButton(5 + numAx * btnW, 230, btnW - 2, 38, "ALL", COLOR_DARK_GREEN, COLOR_WHITE);
    else drawButton(5 + numAx * btnW, 230, btnW - 2, 38, "ALL", COLOR_DARK_GREEN, COLOR_WHITE, 2);
  }
  drawButton(5, 277, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, 277, 112, 40, "Jog", COLOR_BLUE, COLOR_WHITE, 2);
}

// Panel geometry: y 98..212, caption + up to two value rows per half.
const WA_PANEL_Y = 98, WA_PANEL_H = 115, WA_RULE_Y = 155;
const WA_MPOS_CAP_Y = 102, WA_WPOS_CAP_Y = 159;

// One readout block inside the panel: 226 x 42 at (7, capY + 10), navy, values
// in up to three columns (three once there are more than 4 axes — see status.js).
function drawWorkPosBlock(pos, color, capY) {
  const nCols = pendantMachine.numAxes > 4 ? 3 : 2;
  const px = 7, py = capY + 10;
  display.fillRect(px, py, 226, 42, UI_PANEL_BG);
  display.setTextColor(color); display.setTextSize(2);
  for (let i = 0; i < pendantMachine.numAxes; i++) {
    display.setCursor(px + 3 + (i % nCols) * ((220 / nCols) | 0), py + 1 + ((i / nCols) | 0) * 20);
    display.print(AXIS_NAMES[i] + ":" + fmtF(pos[i], 1));
  }
}

function updateWorkMachinePos() {
  if (currentPendantScreen !== PSCREEN_PROBING_WORK) return;
  drawWorkPosBlock(pendantMachine.mpos, COLOR_ORANGE, WA_MPOS_CAP_Y);
}

function updateWorkAreaPos() {
  if (currentPendantScreen !== PSCREEN_PROBING_WORK) return;
  drawWorkPosBlock(pendantMachine.wpos, COLOR_CYAN, WA_WPOS_CAP_Y);
}

// Coordinate-system row — on the shared 5..235 grid (was pitch 56 / w 52, which
// left 5 px on the left but 15 px on the right).
const WCS_Y = 55, WCS_H = 38;

function redrawWorkCoordButtons() {
  const coordSystems = ["G54", "G55", "G56", "G57"];
  for (let i = 0; i < 4; i++) {
    const bg = i === pendantProbing.selectedCoordIndex ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(rowBtnX(4, i), WCS_Y, rowBtnWAt(4, i), WCS_H, coordSystems[i], bg, COLOR_WHITE, 2);
  }
}

function handleProbingWorkTouch(x, y) {
  const coords = ["G54", "G55", "G56", "G57"];
  for (let i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, rowBtnX(4, i), WCS_Y, rowBtnWAt(4, i), WCS_H)) {
      pendantProbing.selectedCoordIndex = i;
      pendantProbing.selectedCoordSystem = coords[i];
      redrawWorkCoordButtons();
      if (pendantConnected) send_line(coords[i]);
      return;
    }
  }
  {
    const axisLetters = ["X", "Y", "Z", "A"];
    const pNum = pendantProbing.selectedCoordIndex + 1;
    const numAx = pendantMachine.numAxes;
    const totalBtns = numAx + 1;
    const btnW = (230 / totalBtns) | 0;
    for (let i = 0; i < numAx; i++) {
      if (isTouchInBounds(x, y, 5 + i * btnW, 230, btnW - 2, 38)) {
        if (pendantConnected) send_line(`G10 L20 P${pNum} ${axisLetters[i]}0`);
        return;
      }
    }
    const allX = 5 + numAx * btnW;
    if (isTouchInBounds(x, y, allX, 230, btnW - 2, 38)) {
      if (pendantConnected) {
        let cmd = `G10 L20 P${pNum}`;
        const letters = [" X0", " Y0", " Z0", " A0"];
        for (let i = 0; i < numAx; i++) cmd += letters[i];
        send_line(cmd);
      }
      return;
    }
  }
  if (isTouchInBounds(x, y, 5, 277, 112, 40)) currentPendantScreen = PSCREEN_MAIN_MENU;
  else if (isTouchInBounds(x, y, 123, 277, 112, 40)) currentPendantScreen = PSCREEN_JOG_HOMING;
}
