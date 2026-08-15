/* screen_connection.cpp port — transport select + link status (ESPNOW_SPEC §6a) */

const TP_Y = 40, TP_H = 62, TBTN_Y = 55, TBTN_H = 40;
// LINK panel extends down to just above the action row: the panel is the
// screen's actual content, so it gets the space rather than leaving a dead band.
const LK_Y = 108, LK_H = 116;
const CACT_Y = 232, CACT_H = 44;
const CNAV_Y = 280, CNAV_H = 38;

// Transport changes only take effect at boot, so the change arms a countdown and
// the pendant reboots itself.  0 = no restart pending.  The sim reloads the page
// where the firmware calls ESP.restart().
const RESTART_DELAY_MS = 5000;
let _restartAtMs = 0;

function restartSecsLeft() {
  if (!_restartAtMs) return 0;
  const remain = _restartAtMs - millis();
  return remain <= 0 ? 0 : Math.ceil(remain / 1000);
}

// Simulated NVS transport selection (firmware: get/set_transport_force) versus
// the transport actually RUNNING (firmware: comms_active_mode()).  They differ
// between a transport change and the reboot that applies it — the firmware only
// re-reads NVS in comms_init(), so the LINK panel keeps showing the live link
// during the countdown.  Keeping them separate here preserves that behaviour.
let simTransport       = "ESPNOW";   // stored choice ("UART" | "ESPNOW")
let simActiveTransport = "ESPNOW";   // what comms_init() picked at boot
function get_transport_force() { return simTransport; }
function set_transport_force(f) { simTransport = f; }
function comms_active_mode() { return simActiveTransport; }

function enterConnection() { releasePanelSprites(); _restartAtMs = 0; }
function exitConnection() { releasePanelSprites(); }

function macStr(mac, ch) {
  const h = mac.map(b => b.toString(16).toUpperCase().padStart(2, "0")).join(":");
  return ch === undefined ? h : `${h} ch${ch}`;
}

// Draws the whole panel relative to (ox, base), where base is where the panel's
// top edge lands in the current target.  Anything outside the target is clipped,
// so the same code renders either the full panel or a horizontal band of it.
function drawLinkBody(g, ox, base) {
  g.fillRoundRect(ox, base, 230, LK_H, 4, PROBE_BG_PANEL);
  g.setTextSize(1); g.setTextColor(PROBE_C_LBLUE);
  g.setCursor(ox + 5, base + 3); g.print("LINK");

  if (comms_active_mode() === "UART") {
    g.setTextSize(2); g.setTextColor(PROBE_C_GREEN);
    g.setCursor(ox + 7, base + 18); g.print("Wired");
    g.setTextSize(1); g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(ox + 7, base + 44); g.print("Baud");
    g.setCursor(ox + 115, base + 44); g.print("Port");
    g.setTextColor(PROBE_C_BLUE);
    g.setCursor(ox + 7, base + 56); g.print(pendantMachine.baudRate);
    g.setCursor(ox + 115, base + 56); g.print(pendantMachine.port);
    g.setTextColor(PROBE_C_DIMBLUE);
    g.setCursor(ox + 7, base + 74); g.print("FluidNC over the RJ12 cable");
    return;
  }

  // The radio itself never started — no amount of waiting will fix it, so do
  // not show "Reconnecting" and imply otherwise.
  if (!espnow_radio_ready()) {
    g.setTextSize(2); g.setTextColor(PROBE_C_RED);
    g.setCursor(ox + 7, base + 18); g.print("Radio down");
    g.setTextSize(1); g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(ox + 7, base + 46); g.print("ESP-NOW failed to start.");
    g.setCursor(ox + 7, base + 58); g.print("Power-cycle the pendant; if it");
    g.setCursor(ox + 7, base + 70); g.print("persists, switch to UART.");
    return;
  }

  const paired = espnow_has_saved_pairing();
  const up = espnow_is_connected();
  g.setTextSize(2);
  g.setTextColor(up ? PROBE_C_GREEN : (paired ? PROBE_C_YELLOW : PROBE_C_RED));
  g.setCursor(ox + 7, base + 18);
  g.print(up ? "Connected" : (paired ? "Reconnecting" : "Not paired"));

  g.setTextSize(1);
  if (!paired) {
    g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(ox + 7, base + 46); g.print("Tap Pair New to add a machine");
    return;
  }
  const info = {};
  const idx = espnow_active_profile_index();
  const got = idx >= 0 && espnow_get_profile(idx, info);
  g.setTextColor(COLOR_GRAY_TEXT);
  g.setCursor(ox + 7, base + 44); g.print("Machine");
  g.setTextColor(PROBE_C_BLUE);
  g.setCursor(ox + 7, base + 56); g.print(got ? info.hostname : "(unnamed)");
  if (got) {
    g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(ox + 7, base + 74); g.print(macStr(info.mac, info.channel));
  }
  const bars = up ? espnow_signal_bars() : 0;
  for (let i = 0; i < 4; i++) {
    const bh = 6 + i * 5, bx = ox + 181 + i * 11, by = base + 58 - bh;
    g.fillRoundRect(bx, by, 8, bh, 1, i < bars ? PROBE_C_GREEN : PROBE_BG_SCREEN);
  }
  if (up) {
    g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(ox + 183, base + 62); g.print(espnow_rssi() + "dB");
  }

  // Receive-path counters, in the space reclaimed by moving the action row down.
  // LOST is packets the radio or RX queue never handed over; DROP is whole
  // messages abandoned in fragment reassembly — invisible to LOST, because those
  // packets DID arrive.  Both read 0 on a healthy link.
  {
    const lost = espnow_rx_dropped() + espnow_rx_pkt_dropped();
    const drop = espnow_frag_aborted();

    g.drawFastHLine(ox + 7, base + 86, 206, PROBE_BG_SCREEN);
    g.setTextColor(COLOR_GRAY_TEXT);
    g.setCursor(ox + 7,   base + 92); g.print("RX BYTES");
    g.setCursor(ox + 105, base + 92); g.print("LOST");
    g.setCursor(ox + 163, base + 92); g.print("DROP");

    g.setTextColor(PROBE_C_BLUE);
    g.setCursor(ox + 7, base + 102); g.print(String(espnow_rx_bytes()));
    g.setTextColor(lost ? PROBE_C_RED : PROBE_C_GREEN);
    g.setCursor(ox + 105, base + 102); g.print(String(lost));
    g.setTextColor(drop ? PROBE_C_RED : PROBE_C_GREEN);
    g.setCursor(ox + 163, base + 102); g.print(String(drop));
  }
}


// The panel is 116 px tall — more than the shared scratch can be relied on to
// hold once the radio has taken its heap, and a panel that exceeds the scratch
// falls back to drawing straight to the display, which is what flickers.  So
// render it as two bands that each fit comfortably.  Each band is still pushed
// atomically, so there is no wipe-then-draw step visible in either.
const LK_BAND = 58;

function drawLinkBand(bandY, bandH) {
  const P = panel(230, bandH, 5, LK_Y + bandY);
  drawLinkBody(P.g, P.ox, P.oy - bandY);
}

function drawLinkPanel() {
  drawLinkBand(0, LK_BAND);
  drawLinkBand(LK_BAND, LK_H - LK_BAND);
}

function drawTransportPanel() {
  display.fillRoundRect(5, TP_Y, 230, TP_H, 4, PROBE_BG_PANEL);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, TP_Y + 3); display.print("TRANSPORT");
  const sel = get_transport_force();
  drawButton(rowBtnX(2,0), TBTN_Y, rowBtnWAt(2,0), TBTN_H, "UART",
             sel === "UART" ? COLOR_ORANGE : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  drawButton(rowBtnX(2,1), TBTN_Y, rowBtnWAt(2,1), TBTN_H, "ESP-NOW",
             sel === "ESPNOW" ? COLOR_ORANGE : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
}

function drawActionRow() {
  display.fillRect(5, CACT_Y, 230, CACT_H, COLOR_BACKGROUND);
  if (_restartAtMs) {
    display.fillRoundRect(5, CACT_Y, 230, CACT_H, 4, PROBE_WARN_BG);
    display.drawRoundRect(5, CACT_Y, 230, CACT_H, 4, PROBE_WARN_BDR);
    display.setTextSize(1); display.setTextColor(COLOR_ORANGE);
    display.setCursor(14, CACT_Y + 8); display.print("Transport changed,");
    display.setTextSize(2);
    display.setCursor(14, CACT_Y + 21); display.print("rebooting in " + restartSecsLeft());
    return;
  }
  if (get_transport_force() !== "ESPNOW") return;
  drawButton(rowBtnX(2,0), CACT_Y, rowBtnWAt(2,0), CACT_H, "Pair New", PROBE_BTN_TEAL, COLOR_WHITE, 2);
  drawButton(rowBtnX(2,1), CACT_Y, rowBtnWAt(2,1), CACT_H, "Machines", COLOR_INDIGO, COLOR_WHITE, 2);
}

function drawConnectionScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("CONNECTION");
  drawTransportPanel();
  drawLinkPanel();
  drawActionRow();
  drawButton(5, CNAV_Y, 230, CNAV_H, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

let _lastShownSecs = -1;
function updateConnectionDisplay() {
  if (currentPendantScreen !== PSCREEN_CONNECTION) return;
  if (_restartAtMs) {
    if (millis() >= _restartAtMs) {
      _restartAtMs = 0;
      simRestart();          // firmware: ESP.restart()
      return;
    }
    const secs = restartSecsLeft();
    if (secs !== _lastShownSecs) { _lastShownSecs = secs; drawActionRow(); }
    return;
  }
  drawLinkPanel();
}

// Stands in for ESP.restart(): re-runs boot-time transport selection, including
// the first-run routing that opens pairing when nothing is paired yet.
function simRestart() {
  _lastShownSecs = -1;
  simActiveTransport = simTransport;   // comms_init() re-reads NVS at boot
  // Nothing paired lands on Connection (which shows "Not paired" + Pair New),
  // NOT on the pairing screen — auto-advertising from boot left the pendant
  // waiting with no obvious way out.
  if (simTransport === "ESPNOW" && !espnow_has_saved_pairing()) {
    navigateTo(PSCREEN_CONNECTION);
    drawConnectionScreen();
  } else {
    navigateTo(PSCREEN_MAIN_MENU);
  }
}

function handleConnectionTouch(x, y) {
  for (let i = 0; i < 2; i++) {
    if (isTouchInBounds(x, y, rowBtnX(2,i), TBTN_Y, rowBtnWAt(2,i), TBTN_H)) {
      const want = i === 0 ? "UART" : "ESPNOW";
      if (want === get_transport_force()) return;
      set_transport_force(want);
      // Re-selecting during the countdown is the escape hatch for a mis-tap.
      _restartAtMs = millis() + RESTART_DELAY_MS;
      _lastShownSecs = -1;
      drawTransportPanel(); drawLinkPanel(); drawActionRow();
      return;
    }
  }
  if (!_restartAtMs && get_transport_force() === "ESPNOW") {
    if (isTouchInBounds(x, y, rowBtnX(2,0), CACT_Y, rowBtnWAt(2,0), CACT_H)) {
      currentPendantScreen = PSCREEN_ESPNOW_PAIR; return;
    }
    if (isTouchInBounds(x, y, rowBtnX(2,1), CACT_Y, rowBtnWAt(2,1), CACT_H)) {
      currentPendantScreen = PSCREEN_ESPNOW_MACHINES; return;
    }
  }
  if (isTouchInBounds(x, y, 5, CNAV_Y, 230, CNAV_H)) currentPendantScreen = PSCREEN_MAIN_MENU;
}
