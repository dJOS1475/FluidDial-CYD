/* screen_spindle_control.cpp port */

function getSpindlePresets() {
  const maxRPM = pendantMachine.spindleMaxRPM > 0 ? pendantMachine.spindleMaxRPM : 24000;
  const minRPM = pendantMachine.spindleMinRPM;
  return [
    maxi(minRPM, ((maxRPM / 4 / 100) | 0) * 100),
    maxi(minRPM, ((maxRPM / 2 / 100) | 0) * 100),
    maxRPM,
  ];
}
function fmtRPM(rpm) {
  if (rpm >= 1000 && rpm % 1000 === 0) return rpm / 1000 + "k";
  return String(rpm);
}

// Dial toggle in the Probe screens' adjustable-field style: bordered box that
// highlights (yellow border + text) while dial mode is active.  Label only — the
// target RPM is shown in the readout panel above.
function drawSpindleDialButton() {
  const active = pendantSpindle.dialMode;
  display.fillRoundRect(179, 163, 56, 37, 2, active ? PROBE_SEL_BG : PROBE_BG_SCREEN);
  display.drawRoundRect(179, 163, 56, 37, 2, active ? PROBE_C_YELLOW : PROBE_C_TAPBDR);
  display.setTextSize(2);
  display.setTextColor(active ? PROBE_C_YELLOW : COLOR_TEAL_BRIGHT);
  const tw = display.textWidth("Dial");
  display.setCursor(179 + ((56 - tw) / 2 | 0), 163 + ((37 - 16) / 2 | 0));
  display.print("Dial");
}

function enterSpindleControl() {
  releasePanelSprites();
  requestSpindleConfig();
  if (pendantSpindle.targetRPM === 0) {
    const presets = getSpindlePresets();
    pendantSpindle.targetRPM = presets[pendantSpindle.selectedPreset];
  }
}
function exitSpindleControl() { releasePanelSprites(); }

function drawSpindleControlScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("SPINDLE CONTROL");
  display.fillRoundRect(5, 40, 230, 60, 5, COLOR_DARKER_BG);
  updateSpindleRPMDisplay();

  drawButton(5, 110, 112, 38, "Fwd", pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  drawButton(123, 110, 112, 38, "Rev", !pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 152);
  display.printf("Min: %d  Max: %d RPM", pendantMachine.spindleMinRPM, pendantMachine.spindleMaxRPM);

  const presets = getSpindlePresets();
  for (let i = 0; i < 3; i++) {
    const bg = (!pendantSpindle.dialMode && i === pendantSpindle.selectedPreset) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 58, 163, 56, 37, fmtRPM(presets[i]), bg, COLOR_WHITE, 2);
  }
  drawSpindleDialButton();

  drawButton(5, 218, 112, 40, "Start", COLOR_DARK_GREEN, COLOR_WHITE, 2);
  drawButton(123, 218, 112, 40, "Stop", COLOR_RED, COLOR_WHITE, 2);
  drawButton(5, 268, 230, 37, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

function updateSpindleRPMDisplay() {
  if (currentPendantScreen !== PSCREEN_SPINDLE_CONTROL) return;
  const P = panel(230, 60, 5, 40);
  const g = P.g, ox = P.ox, oy = P.oy;
  g.fillRect(ox, oy, 230, 60, COLOR_DARKER_BG);
  g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
  g.setCursor(ox + 5, oy + 5); g.print("RPM");
  g.setTextColor(COLOR_ORANGE); g.setTextSize(3);
  g.setCursor(ox + 5, oy + 22); g.print(pendantMachine.spindleRPM);
  g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
  g.setCursor(ox + 120, oy + 5); g.print("Target RPM");
  g.setTextColor(COLOR_DARK_GREEN); g.setTextSize(3);
  g.setCursor(ox + 120, oy + 22); g.print(pendantSpindle.targetRPM);
}

function redrawSpindleDirectionButtons() {
  if (currentPendantScreen !== PSCREEN_SPINDLE_CONTROL) return;
  drawButton(5, 110, 112, 38, "Fwd", pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  drawButton(123, 110, 112, 38, "Rev", !pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  updateSpindleRPMDisplay();
}

function redrawSpindlePresetButtons() {
  if (currentPendantScreen !== PSCREEN_SPINDLE_CONTROL) return;
  const presets = getSpindlePresets();
  for (let i = 0; i < 3; i++) {
    const bg = (!pendantSpindle.dialMode && i === pendantSpindle.selectedPreset) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 58, 163, 56, 37, fmtRPM(presets[i]), bg, COLOR_WHITE, 2);
  }
  drawSpindleDialButton();
  updateSpindleRPMDisplay();
}

function handleSpindleControlTouch(x, y) {
  if (isTouchInBounds(x, y, 5, 110, 112, 38)) {
    pendantSpindle.directionFwd = true; pendantMachine.spindleDir = "Fwd";
    redrawSpindleDirectionButtons(); return;
  } else if (isTouchInBounds(x, y, 123, 110, 112, 38)) {
    pendantSpindle.directionFwd = false; pendantMachine.spindleDir = "Rev";
    redrawSpindleDirectionButtons(); return;
  }
  const presets = getSpindlePresets();
  for (let i = 0; i < 3; i++) {
    if (isTouchInBounds(x, y, 5 + i * 58, 163, 56, 37)) {
      pendantSpindle.dialMode = false; pendantSpindle.selectedPreset = i;
      pendantSpindle.targetRPM = presets[i];
      redrawSpindlePresetButtons(); return;
    }
  }
  if (isTouchInBounds(x, y, 179, 163, 56, 37)) {
    pendantSpindle.dialMode = !pendantSpindle.dialMode;
    if (pendantSpindle.dialMode) {
      const maxRPM = pendantMachine.spindleMaxRPM > 0 ? pendantMachine.spindleMaxRPM : 24000;
      pendantSpindle.targetRPM = constrain(pendantSpindle.targetRPM, pendantMachine.spindleMinRPM, maxRPM);
    }
    redrawSpindlePresetButtons(); return;
  }
  if (isTouchInBounds(x, y, 5, 218, 112, 40)) {
    if (pendantConnected) send_line(`${pendantSpindle.directionFwd ? "M3" : "M4"} S${pendantSpindle.targetRPM}`);
    pendantMachine.spindleRunning = true;
    return;
  }
  if (isTouchInBounds(x, y, 123, 218, 112, 40)) {
    if (pendantConnected) send_line("M5");
    pendantMachine.spindleRunning = false;
    return;
  }
  if (y > 258) currentPendantScreen = PSCREEN_MAIN_MENU;
}
