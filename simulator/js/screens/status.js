/* screen_status.cpp port */

function enterStatus() { releasePanelSprites(); }
function exitStatus() { releasePanelSprites(); }

function drawStatusScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("STATUS");
  updateStatusMachineStatus();
  updateStatusCurrentFile();
  updateStatusAxisPositions();
  updateStatusFeedSpindle();
  drawButton(5, 280, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, 280, 112, 40, "FluidNC", COLOR_BLUE, COLOR_WHITE, 2);
}

function updateStatusMachineStatus() {
  if (currentPendantScreen !== PSCREEN_STATUS) return;
  const statusStr = pendantMachine.status;
  const fileStr = pendantMachine.currentFile;
  const pct = pendantMachine.jobPercent;
  const jobRunning = fileStr.length > 0;
  const P = panel(230, 50, 5, 40);
  const g = P.g, ox = P.ox, oy = P.oy;
  g.fillRect(ox, oy, 230, 50, COLOR_DARKER_BG);

  if (!pendantSynced || statusStr === "N/C" || statusStr.length === 0) {
    const phase = pendantConnected ? "Syncing" : "Connecting";
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    let lw = g.textWidth("MACHINE STATUS");
    g.setCursor(ox + 115 - (lw / 2 | 0), oy + 5); g.print("MACHINE STATUS");
    g.setTextColor(COLOR_ORANGE); g.setTextSize(3);
    let cw = g.textWidth(phase);
    g.setCursor(ox + 115 - (cw / 2 | 0), oy + 22); g.print(phase);
  } else if (statusStr.startsWith("Alarm")) {
    const desc = alarmDescription(statusStr);
    g.setTextColor(TFT_RED); g.setTextSize(1);
    let dw = g.textWidth(desc);
    g.setCursor(ox + 115 - (dw / 2 | 0), oy + 5); g.print(desc);
    g.setTextSize(3);
    let sw = g.textWidth("ALARM");
    g.setCursor(ox + 115 - (sw / 2 | 0), oy + 22); g.print("ALARM");
  } else if (jobRunning) {
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    g.setCursor(ox + 5, oy + 5); g.print("MACHINE STATUS");
    g.setTextColor(COLOR_CYAN); g.setTextSize(3);
    let sw = g.textWidth(statusStr);
    let cx = 56 - (sw / 2 | 0); if (cx < 0) cx = 0;
    g.setCursor(ox + cx, oy + 22); g.print(statusStr);
    g.drawLine(ox + 115, oy + 2, ox + 115, oy + 47, COLOR_BUTTON_GRAY);
    const pctStr = pct + "%";
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    let lw = g.textWidth("PROGRESS");
    g.setCursor(ox + 174 - (lw / 2 | 0), oy + 5); g.print("PROGRESS");
    g.setTextColor(COLOR_GREEN); g.setTextSize(3);
    let pw = g.textWidth(pctStr);
    g.setCursor(ox + 174 - (pw / 2 | 0), oy + 22); g.print(pctStr);
  } else {
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    let lw = g.textWidth("MACHINE STATUS");
    g.setCursor(ox + 115 - (lw / 2 | 0), oy + 5); g.print("MACHINE STATUS");
    g.setTextColor(COLOR_CYAN); g.setTextSize(3);
    let sw = g.textWidth(statusStr);
    g.setCursor(ox + 115 - (sw / 2 | 0), oy + 22); g.print(statusStr);
  }
}

function updateStatusCurrentFile() {
  if (currentPendantScreen !== PSCREEN_STATUS) return;
  const fileStr = pendantMachine.currentFile;
  const P = panel(230, 40, 5, 95);
  const g = P.g, ox = P.ox, oy = P.oy;
  g.fillRoundRect(ox, oy, 230, 40, 5, COLOR_DARKER_BG);
  if (pendantSdCard.loadedFile.length > 0 && fileStr.length === 0) {
    g.setTextColor(COLOR_GREEN); g.setTextSize(1);
    g.setCursor(ox + 5, oy + 5); g.print("READY - press green to run");
    g.setCursor(ox + 5, oy + 20); g.print(pendantSdCard.loadedFile);
  } else {
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    g.setCursor(ox + 5, oy + 5); g.print("CURRENT FILE");
    g.setTextColor(COLOR_CYAN);
    g.setCursor(ox + 5, oy + 20); g.print(fileStr);
  }
}

function updateStatusAxisPositions() {
  if (currentPendantScreen !== PSCREEN_STATUS) return;
  // MACHINE coordinates (workX/Y/Z/A = MPos); posX/Y/Z are work coords.
  const pos = [pendantMachine.workX, pendantMachine.workY, pendantMachine.workZ, pendantMachine.workA];
  const numAxes = pendantMachine.numAxes;
  const P = panel(230, 65, 5, 140);
  const g = P.g, ox = P.ox, oy = P.oy;
  g.fillRoundRect(ox, oy, 230, 65, 5, COLOR_DARKER_BG);
  g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
  g.setCursor(ox + 5, oy + 5); g.print("MACHINE POSITION");
  const axisNames = ["X", "Y", "Z", "A"];
  g.setTextColor(COLOR_ORANGE); g.setTextSize(2);
  for (let i = 0; i < numAxes; i++) {
    g.setCursor(ox + (i % 2 ? 125 : 5), oy + 20 + ((i / 2) | 0) * 23);
    g.print(axisNames[i] + ":" + fmtF(pos[i], 1));
  }
}

function updateStatusFeedSpindle() {
  if (currentPendantScreen !== PSCREEN_STATUS) return;
  const feedRate = pendantMachine.feedRate;
  const spindleRPM = pendantMachine.spindleRPM;
  const spindleDir = pendantMachine.spindleDir;
  const P = panel(230, 65, 5, 210);
  const g = P.g, ox = P.ox, oy = P.oy;
  g.fillRect(ox, oy, 230, 65, COLOR_BACKGROUND);

  g.fillRoundRect(ox + 0, oy, 112, 65, 5, COLOR_DARKER_BG);
  g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
  g.setCursor(ox + 5, oy + 3); g.print("FEED");
  g.setTextColor(COLOR_ORANGE); g.setTextSize(2);
  g.setCursor(ox + 5, oy + 25); g.print(feedRate);
  g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
  let mmW = g.textWidth("mm/min");
  g.setCursor(ox + 112 - 5 - mmW, oy + 50); g.print("mm/min");

  g.fillRoundRect(ox + 118, oy, 112, 65, 5, COLOR_DARKER_BG);
  g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
  g.setCursor(ox + 123, oy + 3); g.print("SPINDLE");
  let dirW = g.textWidth(spindleDir);
  g.setCursor(ox + 230 - 5 - dirW, oy + 3); g.print(spindleDir);
  g.setTextColor(COLOR_GREEN); g.setTextSize(2);
  g.setCursor(ox + 123, oy + 25); g.print(spindleRPM);
  g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
  let rpmW = g.textWidth("RPM");
  g.setCursor(ox + 230 - 5 - rpmW, oy + 50); g.print("RPM");
}

function handleStatusTouch(x, y) {
  if (isTouchInBounds(x, y, 5, 280, 112, 40)) currentPendantScreen = PSCREEN_MAIN_MENU;
  else if (isTouchInBounds(x, y, 123, 280, 112, 40)) currentPendantScreen = PSCREEN_FLUIDNC;
}
