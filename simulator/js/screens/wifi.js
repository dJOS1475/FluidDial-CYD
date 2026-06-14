/* screen_wifi_setup.cpp port (USE_WIFI build) */

const PNL_MODE_X = 5, PNL_MODE_Y = 40, PNL_MODE_W = 230, PNL_MODE_H = 58;
const PNL_STAT_X = 5, PNL_STAT_Y = 106, PNL_STAT_W = 230, PNL_STAT_H = 116;
const BTN_ACT_X = 5, BTN_ACT_Y = 230, BTN_ACT_W = 230, BTN_ACT_H = 36;
const BTN_BACK_X = 5, BTN_BACK_Y = 272, BTN_BACK_W = 230, BTN_BACK_H = 40;

function drawModeBanner(uartMode) {
  display.fillRoundRect(PNL_MODE_X, PNL_MODE_Y, PNL_MODE_W, PNL_MODE_H, 5, COLOR_DARKER_BG);
  display.drawRoundRect(PNL_MODE_X, PNL_MODE_Y, PNL_MODE_W, PNL_MODE_H, 5, COLOR_CYAN);
  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(PNL_MODE_X + 5, PNL_MODE_Y + 5); display.print("TRANSPORT");
  const cue = "TAP";
  display.setTextColor(COLOR_CYAN);
  display.setCursor(PNL_MODE_X + PNL_MODE_W - 5 - display.textWidth(cue), PNL_MODE_Y + 5);
  display.print(cue);
  display.setTextColor(uartMode ? COLOR_ORANGE : COLOR_GREEN); display.setTextSize(2);
  display.setCursor(PNL_MODE_X + 5, PNL_MODE_Y + 18); display.print(uartMode ? "UART cable" : "WiFi");
  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(PNL_MODE_X + 5, PNL_MODE_Y + 44); display.print("Tap to switch transport");
}

let _minHeapEverSeen = 0xffffffff;
function sampleMinHeap() {
  const now = ESP.getFreeHeap();
  if (now < _minHeapEverSeen) _minHeapEverSeen = now;
}

function redrawStatusPanel() {
  sampleMinHeap();
  display.fillRoundRect(PNL_STAT_X, PNL_STAT_Y, PNL_STAT_W, PNL_STAT_H, 5, COLOR_DARKER_BG);
  const uartMode = comms_active_mode() === COMMS_MODE_UART;

  if (uartMode) {
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 8); display.print("Talking to FluidNC via");
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 21); display.print("the UART (RJ12) cable.");
    display.setTextColor(COLOR_GRAY_TEXT);
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 45); display.print("Baud:");
    display.setTextColor(COLOR_ORANGE);
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 58); display.print(pendantMachine.baudRate);
    display.setTextColor(COLOR_GRAY_TEXT);
    display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 45); display.print("Port:");
    display.setTextColor(COLOR_CYAN);
    display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 58); display.print(pendantMachine.port);
    display.setTextColor(COLOR_GRAY_TEXT);
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 82); display.print("Tap the banner above to");
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 95); display.print("change transport.  Last:");
    display.print(` ${resetReasonName()}/0:0`);
  } else {
    const apMode = pendantMachine.wifiInApMode;
    let bars = pendantMachine.wifiSignalBars; if (bars < 0) bars = 0;
    const cfg = wifi_active_config();
    const connected = websocket_is_connected();
    display.setTextSize(1);
    display.setTextColor(COLOR_GRAY_TEXT);
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 6); display.print("STATUS");
    display.setTextColor(connected ? COLOR_GREEN : COLOR_ORANGE);
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 18); display.print(wifi_status_str());

    if (apMode) {
      display.setTextColor(COLOR_GRAY_TEXT);
      display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 50); display.print("Connect phone to WiFi:");
      display.setTextColor(COLOR_CYAN);
      display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 63); display.print(wifi_ap_ssid());
      display.setTextColor(COLOR_GRAY_TEXT);
      display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 82); display.print("Then open browser:");
      display.setTextColor(COLOR_CYAN);
      display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 95); display.print("192.168.4.1");
    } else {
      let barStr = "";
      for (let i = 0; i < 4; i++) barStr += i < bars ? "|" : ".";
      display.setTextSize(1);
      display.setTextColor(COLOR_GRAY_TEXT);
      display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 50); display.print("NETWORK");
      display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 50); display.print("SIGNAL");
      display.setTextColor(COLOR_CYAN);
      display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 63); display.print(cfg.valid ? cfg.ssid : "---");
      display.setTextColor(bars > 0 ? COLOR_GREEN : COLOR_GRAY_TEXT);
      display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 63); display.print(barStr);
      display.setTextColor(COLOR_GRAY_TEXT);
      display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 85); display.print("FluidNC IP");
      display.setTextColor(COLOR_CYAN);
      display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 98); display.print(cfg.valid ? cfg.fluidnc_ip : "---");
      display.setTextColor(COLOR_GRAY_TEXT);
      display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 85); display.print("Reset c0/c1");
      display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 98); display.print(resetReasonName() + " 0:0");
    }
    const nowHeap = ESP.getFreeHeap();
    display.setTextColor(nowHeap < 30000 ? COLOR_RED : nowHeap < 60000 ? COLOR_ORANGE : COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 108);
    display.print(`Heap ${(nowHeap / 1024) | 0}K (min ${(_minHeapEverSeen / 1024) | 0}K)`);
  }
}

function enterWiFiSetup() {}
function exitWiFiSetup() {}

function drawWiFiSetupScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("WIFI SETUP");
  const uartMode = comms_active_mode() === COMMS_MODE_UART;
  const apMode = wifi_in_ap_mode();
  drawModeBanner(uartMode);
  redrawStatusPanel();
  if (!uartMode) {
    if (apMode) drawButton(BTN_ACT_X, BTN_ACT_Y, BTN_ACT_W, BTN_ACT_H, "Cancel AP Setup", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    else drawButton(BTN_ACT_X, BTN_ACT_Y, BTN_ACT_W, BTN_ACT_H, "Reconfigure WiFi", COLOR_ORANGE, COLOR_BACKGROUND, 2);
  }
  drawButton(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, "< Back", COLOR_BLUE, COLOR_WHITE, 2);
}

function updateWiFiSetupDisplay() {
  if (currentPendantScreen !== PSCREEN_WIFI_SETUP) return;
  redrawStatusPanel();
}

function cycleTransportOverride() {
  const next = get_transport_force() === TFORCE_WIFI ? TFORCE_UART : TFORCE_WIFI;
  set_transport_force(next);
  // In the sim we apply the change immediately (no reboot) and redraw.
  pendantMachine.port = next === TFORCE_WIFI ? "WiFi:81" : "UART0";
  drawWiFiSetupScreen();
  if (typeof syncControlsFromState === "function") syncControlsFromState();
}

function handleWiFiSetupTouch(x, y) {
  if (isTouchInBounds(x, y, BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H)) {
    currentPendantScreen = PSCREEN_FLUIDNC; return;
  }
  if (isTouchInBounds(x, y, PNL_MODE_X, PNL_MODE_Y, PNL_MODE_W, PNL_MODE_H)) {
    cycleTransportOverride(); return;
  }
  const uartMode = comms_active_mode() === COMMS_MODE_UART;
  const apMode = wifi_in_ap_mode();
  if (uartMode) return;
  if (isTouchInBounds(x, y, BTN_ACT_X, BTN_ACT_Y, BTN_ACT_W, BTN_ACT_H)) {
    if (apMode) { wifi_stop_ap(); currentPendantScreen = PSCREEN_FLUIDNC; return; }
    pendantMachine.wifiInApMode = true;
    logLine("WiFi credentials cleared — AP captive portal started (sim)");
    drawWiFiSetupScreen();
    return;
  }
}
