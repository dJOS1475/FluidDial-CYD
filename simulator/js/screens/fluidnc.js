/* screen_fluidnc.cpp port */

function enterFluidNC() { releasePanelSprites(); }
function exitFluidNC() {
  releasePanelSprites();
  if (pendantMachine.rotationDirty) {
    localStorage.setItem("sim.rotation", String(pendantMachine.rotation));
    pendantMachine.rotationDirty = false;
  }
}

function updateFluidNCDisplay() {
  const connected = pendantConnected;
  // Panel 1: version / network
  {
    const P = panel(230, 60, 5, 40);
    const g = P.g, ox = P.ox, oy = P.oy;
    g.fillRect(ox, oy, 230, 60, COLOR_BACKGROUND);
    g.fillRoundRect(ox, oy, 230, 60, 5, COLOR_DARKER_BG);
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    g.setCursor(ox + 5, oy + 5); g.print("FluidDial-CYD");
    g.setTextColor(COLOR_GREEN);
    g.setCursor(ox + 5, oy + 17); g.print(pendantMachine.fluidDialVersion);
    g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(ox + 5, oy + 35); g.print("FluidNC");
    // "---" until the controller has actually reported its version, so an unread
    // field can never be mistaken for a real one.
    g.setTextColor(pendantMachine.fluidNCVersion ? COLOR_GREEN : COLOR_GRAY_TEXT);
    g.setCursor(ox + 5, oy + 47); g.print(pendantMachine.fluidNCVersion || "---");
    g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(ox + 115, oy + 5); g.print("IP ADDRESS");
    g.setTextColor(pendantMachine.ipAddress.length ? COLOR_CYAN : COLOR_GRAY_TEXT);
    g.setCursor(ox + 115, oy + 17); g.print(pendantMachine.ipAddress.length ? pendantMachine.ipAddress : "---");
    g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(ox + 115, oy + 35); g.print("WIFI SSID");
    g.setTextColor(pendantMachine.wifiSSID.length ? COLOR_CYAN : COLOR_GRAY_TEXT);
    g.setCursor(ox + 115, oy + 47); g.print(pendantMachine.wifiSSID.length ? pendantMachine.wifiSSID : "---");
  }
  // Panel 2: resources
  {
    const P = panel(230, 70, 5, 186);
    const g = P.g, ox = P.ox, oy = P.oy;
    g.fillRect(ox, oy, 230, 70, COLOR_BACKGROUND);
    g.fillRoundRect(ox, oy, 230, 70, 5, COLOR_DARKER_BG);
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    g.setCursor(ox + 5, oy + 5); g.print("FREE HEAP");
    g.setTextColor(COLOR_ORANGE); g.setTextSize(2);
    g.setCursor(ox + 5, oy + 20); g.print(((ESP.getFreeHeap() / 1024) | 0) + " KB");
    g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
    g.setCursor(ox + 115, oy + 5); g.print("STATUS");
    g.setTextColor(connected ? COLOR_GREEN : COLOR_RED);
    g.setCursor(ox + 115, oy + 20); g.print(pendantMachine.connectionStatus);
    g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(ox + 5, oy + 48); g.print("ROTATION");
    g.setTextColor(COLOR_CYAN);
    g.setCursor(ox + 5, oy + 58); g.print(pendantMachine.displayRotation);
    g.setTextColor(COLOR_GRAY_TEXT);
    // Battery voltage + inferred charge state — charging is INFERRED from the
    // voltage trend, so the raw mV is shown to make a wrong icon diagnosable.
    g.setCursor(ox + 115, oy + 48); g.print("BATTERY");
    if (pendantMachine.batteryPercent < 0) {
      g.setTextColor(COLOR_GRAY_TEXT);
      g.setCursor(ox + 115, oy + 58); g.print("n/a");
    } else {
      g.setTextColor(pendantMachine.batteryCharging ? COLOR_YELLOW : COLOR_CYAN);
      g.setCursor(ox + 115, oy + 58);
      g.print(pendantMachine.batteryMv + "mV " + (pendantMachine.batteryCharging ? "CHG" : "bat"));
    }
  }
}

function drawFluidNCScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("FLUIDNC");

  display.fillRoundRect(5, 108, 230, 70, 5, COLOR_DARKER_BG);
  display.drawRoundRect(5, 108, 230, 70, 5, COLOR_CYAN);
  const hint = (comms_active_mode() === "UART") ? "UART >" : "ESP-NOW >";
  display.setTextColor(COLOR_CYAN); display.setTextSize(1);
  display.setCursor(240 - 5 - display.textWidth(hint), 113);
  display.print(hint);

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(10, 113); display.print("CONNECTION");
  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(10, 130); display.print("Baud:");
  display.setTextColor(COLOR_ORANGE); display.setTextSize(2);
  display.setCursor(100, 127); display.print(pendantMachine.baudRate);
  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(10, 148); display.print("Port:");
  display.setTextColor(COLOR_CYAN); display.setTextSize(1);
  display.setCursor(10, 160); display.print(pendantMachine.port);

  drawButton(5, 272, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, 272, 112, 40, "Status", COLOR_BLUE, COLOR_WHITE, 2);

  updateFluidNCDisplay();
}

function handleFluidNCTouch(x, y) {
  if (isTouchInBounds(x, y, 5, 272, 112, 40)) currentPendantScreen = PSCREEN_MAIN_MENU;
  else if (isTouchInBounds(x, y, 123, 272, 112, 40)) currentPendantScreen = PSCREEN_STATUS;
  else if (isTouchInBounds(x, y, 5, 108, 230, 70)) currentPendantScreen = PSCREEN_CONNECTION;
}
