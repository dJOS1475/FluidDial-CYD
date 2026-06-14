/* screen_probing_work.cpp port (Work Area) */

function enterProbingWork() { releasePanelSprites(); }
function exitProbingWork() { releasePanelSprites(); }

function drawProbingWorkScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("WORK AREA");

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 43); display.print("COORDINATE SYSTEM");
  redrawWorkCoordButtons();

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 100); display.print("MACHINE POS");
  updateWorkMachinePos();

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 158); display.print("WORK POS");
  updateWorkAreaPos();

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 218); display.print("SET WORK ZERO");
  {
    const axisLabels = ["X", "Y", "Z", "A"];
    const numAx = pendantMachine.numAxes;
    const totalBtns = numAx + 1;
    const btnW = (230 / totalBtns) | 0;
    for (let i = 0; i < numAx; i++)
      drawButton(5 + i * btnW, 230, btnW - 2, 38, axisLabels[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
    drawButton(5 + numAx * btnW, 230, btnW - 2, 38, "ALL", COLOR_DARK_GREEN, COLOR_WHITE, 2);
  }
  drawButton(5, 277, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, 277, 112, 40, "Jog", COLOR_BLUE, COLOR_WHITE, 2);
}

function updateWorkMachinePos() {
  if (currentPendantScreen !== PSCREEN_PROBING_WORK) return;
  const pos = [pendantMachine.workX, pendantMachine.workY, pendantMachine.workZ, pendantMachine.workA];
  const axisNames = ["X", "Y", "Z", "A"];
  display.fillRect(5, 108, 230, 45, COLOR_BACKGROUND);
  display.setTextColor(COLOR_ORANGE); display.setTextSize(2);
  for (let i = 0; i < pendantMachine.numAxes; i++) {
    display.setCursor(5 + (i % 2 ? 120 : 0), 108 + 5 + ((i / 2) | 0) * 20);
    display.print(axisNames[i] + ":" + fmtF(pos[i], 1));
  }
}

function updateWorkAreaPos() {
  if (currentPendantScreen !== PSCREEN_PROBING_WORK) return;
  const pos = [pendantMachine.posX, pendantMachine.posY, pendantMachine.posZ, pendantMachine.posA];
  const axisNames = ["X", "Y", "Z", "A"];
  display.fillRect(5, 166, 230, 45, COLOR_BACKGROUND);
  display.setTextColor(COLOR_CYAN); display.setTextSize(2);
  for (let i = 0; i < pendantMachine.numAxes; i++) {
    display.setCursor(5 + (i % 2 ? 120 : 0), 166 + 5 + ((i / 2) | 0) * 20);
    display.print(axisNames[i] + ":" + fmtF(pos[i], 1));
  }
}

function redrawWorkCoordButtons() {
  const coordSystems = ["G54", "G55", "G56", "G57"];
  for (let i = 0; i < 4; i++) {
    const bg = i === pendantProbing.selectedCoordIndex ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 56, 55, 52, 38, coordSystems[i], bg, COLOR_WHITE, 2);
  }
}

function handleProbingWorkTouch(x, y) {
  const coords = ["G54", "G55", "G56", "G57"];
  for (let i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, 5 + i * 56, 55, 52, 38)) {
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
