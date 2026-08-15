/* Simulated PeerLink state — stands in for src/PeerLink.cpp, which is real
   radio code with no browser equivalent.  Lets the pairing / machines screens
   be driven through every state from the control panel. */

const simEspNow = {
  profiles: [
    { name: "Shapeoko", mac: [0xA4,0xCF,0x12,0x9B,0x00,0x1E], ch: 6 },
    { name: "MPCNC",    mac: [0x3C,0x61,0x05,0x44,0x2A,0xB7], ch: 1 },
  ],
  activeIdx: 0,
  connected: true,
  rssi: -58,
  pairing: "idle",      // idle | waiting | working | ok | fail
  radioReady: true,     // false = espnow_init() failed (heap exhaustion)
  rxBytes: 48213,       // receive-path counters shown on the Connection screen
  rxLost: 0,
  fragAborted: 0,
};

function espnow_has_saved_pairing() { return simEspNow.profiles.length > 0; }
function espnow_is_connected()      { return simEspNow.connected && espnow_has_saved_pairing(); }
function espnow_is_reconnecting()   { return simEspNow.pairing === "working"; }
function espnow_pairing_complete()  { return simEspNow.pairing === "ok"; }
function espnow_rssi()              { return simEspNow.rssi; }
function espnow_signal_bars() {
  const r = simEspNow.rssi;
  return r >= -55 ? 4 : r >= -65 ? 3 : r >= -75 ? 2 : r >= -85 ? 1 : 0;
}
function espnow_radio_ready()       { return simEspNow.radioReady; }
function espnow_rx_bytes()          { return simEspNow.rxBytes; }
function espnow_rx_dropped()        { return simEspNow.rxLost; }
function espnow_rx_pkt_dropped()    { return 0; }
function espnow_frag_aborted()      { return simEspNow.fragAborted; }
function espnow_profile_count()        { return simEspNow.profiles.length; }
function espnow_active_profile_index() { return simEspNow.profiles.length ? simEspNow.activeIdx : -1; }
function espnow_get_profile(i, out) {
  const p = simEspNow.profiles[i];
  if (!p) return false;
  out.hostname = p.name; out.mac = p.mac; out.channel = p.ch;
  return true;
}
function espnow_select_profile(i) {
  if (!simEspNow.profiles[i]) return false;
  simEspNow.activeIdx = i; return true;
}
function espnow_remove_profile(i) {
  if (!simEspNow.profiles[i]) return false;
  simEspNow.profiles.splice(i, 1);
  if (simEspNow.activeIdx >= simEspNow.profiles.length) simEspNow.activeIdx = simEspNow.profiles.length - 1;
  return true;
}
function espnow_start_pairing()  { simEspNow.pairing = "waiting"; }
function espnow_cancel_pairing() { simEspNow.pairing = "idle"; }
