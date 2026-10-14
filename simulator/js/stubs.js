/*
 * stubs.js — stand-ins for the firmware platform / comms calls the screens
 * make.  None of them touch real hardware; the side-effecting ones (sending
 * G-code, requesting file lists) just log to the on-screen "Controller Log"
 * panel and, where it makes the UI behave realistically, mutate sim state.
 */

// ---- Arduino-ish helpers ----
function constrain(v, lo, hi) {
  return v < lo ? lo : v > hi ? hi : v;
}
function maxi(a, b) {
  return a > b ? a : b;
}
function mini(a, b) {
  return a < b ? a : b;
}
function millis() {
  return performance.now() | 0;
}
function delay_ms() {} // button-flash delays are handled by the tap ripple in sim.js
function dtostrf(v, _width, dec) {
  return Number(v).toFixed(dec);
}

// ---- command / event log (rendered by sim.js) ----
const simLog = [];
function logLine(s) {
  const t = new Date().toLocaleTimeString();
  simLog.push(`[${t}] ${s}`);
  if (simLog.length > 200) simLog.shift();
  if (typeof renderLog === "function") renderLog();
}

// ---- comms / motion ----
function send_line(cmd) {
  logLine("TX  " + cmd);
}
function send_line_nowait(cmd) {
  logLine("TX* " + cmd);
}
let pending_nowait_sends = 0;

// FluidNC realtime override byte names — we only need the identifiers; log them.
const FeedOvrReset = "FeedOvrReset",
  FeedOvrCoarsePlus = "FeedOvrCoarse+",
  FeedOvrCoarseMinus = "FeedOvrCoarse-",
  FeedOvrFinePlus = "FeedOvrFine+",
  FeedOvrFineMinus = "FeedOvrFine-",
  SpindleOvrReset = "SpindleOvrReset",
  SpindleOvrCoarsePlus = "SpindleOvrCoarse+",
  SpindleOvrCoarseMinus = "SpindleOvrCoarse-",
  SpindleOvrFinePlus = "SpindleOvrFine+",
  SpindleOvrFineMinus = "SpindleOvrFine-",
  JogCancel = "JogCancel";   // realtime 0x85 — flush + stop a jog
function fnc_realtime(b) {
  logLine("RT  " + b);
}

function requestSpindleConfig() {
  logLine("REQ $30/$31 (spindle config)");
}

// ---- SD / macro list requests ----
// The real pendant asks the controller and fills the list asynchronously.
// Here we fulfil the request from the sim's seeded lists after a short delay,
// so the "Loading..." state is actually visible.
function request_file_list(dirname) {
  const path = dirname || "/sd";
  const gen  = pendantSdCard.listGen || 0;   // tag the request
  logLine(`REQ $Files/ListGCode=${path}`);
  setTimeout(() => {
    // Drop a reply that we navigated away from while it was in flight —
    // descend-then-ascend would otherwise fill the wrong folder's list.
    if ((pendantSdCard.listGen || 0) !== gen) {
      logLine(`  (stale listing for ${path} dropped)`);
      return;
    }
    const names = [], dirs = [];
    if (simSdUseTree) {
      // Mirror the firmware copy-out: ".." first, then folders, then files.
      // (FluidNC's own comparator puts files FIRST and folders last, so the
      // pendant has to invert that order itself.)
      const entries = SIM_SD_TREE[path] || [];
      if (pendantSdCard.dirLevel > 0) { names.push(".."); dirs.push(true); }
      for (const e of entries) if (e.dir)  { names.push(e.name); dirs.push(true); }
      for (const e of entries) if (!e.dir) { names.push(e.name); dirs.push(false); }
      pendantSdCard.totalFiles = entries.length;
    } else {
      for (const f of simSdFiles) { names.push(f); dirs.push(false); }
      pendantSdCard.totalFiles = simSdTotal;   // pre-cap; drives the "+" marker
    }
    pendantSdCard.files = names;
    pendantSdCard.isDir = dirs;
    pendantSdCard.fileCount = names.length;
    pendantSdCard.curPath = path;
    pendantSdCard.loading = false;
    pendantSdCard.loadFailed = false;
    if (currentPendantScreen === PSCREEN_SD_CARD) sdRepaintPaged();
  }, 500);
}
let g_expecting_json = false;

function requestMacros() {
  logLine("REQ macro list");
  pendantMacros.loadStartMs = millis();
  setTimeout(() => {
    pendantMacros.content = simMacros.map((m) => m.name);
    pendantMacros.filename = simMacros.map((m) => m.cmd);
    pendantMacros.count = simMacros.length;
    pendantMacros.loading = false;
    pendantMacros.loadFailed = false;
    pendantMacros.cacheValid = true;
    if (currentPendantScreen === PSCREEN_MACROS) updateMacrosFileList();
  }, 500);
}

// ---- NVS persistence (localStorage-backed so settings survive a reload) ----
function saveJogPrefs() {
  localStorage.setItem(
    "sim.jog",
    JSON.stringify({
      coarseIdx: pendantJog.coarseIdx,
      selectedIncrement: pendantJog.selectedIncrement,
    })
  );
}
function saveProbeSettings() {
  localStorage.setItem("sim.probe", JSON.stringify(pendantProbeV2));
  logLine("Probe settings saved");
}
function loadProbeSettings() {
  try {
    const j = JSON.parse(localStorage.getItem("sim.probe") || "{}");
    Object.assign(pendantProbeV2, j);
  } catch (e) {}
}

// ---- ESP / diagnostics stubs (used by FluidNC + WiFi screens) ----
const ESP = {
  getFreeHeap: () => pendantMachine.freeHeap,
  restart: () => logLine("ESP.restart() — (sim: ignored)"),
};
const ESP_RST_POWERON = 1,
  ESP_RST_SW = 3;
let lastResetReason = ESP_RST_POWERON;
function resetReasonName() {
  return "POWERON";
}
let capturedBootStage = 0,
  capturedCore1Stage = 0,
  capturedCore0Iters = 0,
  capturedCore1Iters = 0;
let nvsPrevIter0 = 0,
  nvsPrevIter1 = 0,
  nvsPrevMinHeap = 0,
  nvsPrevNowHeap = 0;

// ---- comms transport mode (USE_WIFI build is what the sim models) ----
const COMMS_MODE_UART = 0,
  COMMS_MODE_WIFI = 1;
let _commsMode = COMMS_MODE_UART;
function comms_active_mode() {
  return _commsMode;
}
const TFORCE_UART = 0,
  TFORCE_WIFI = 1;
function get_transport_force() {
  return _commsMode === COMMS_MODE_WIFI ? TFORCE_WIFI : TFORCE_UART;
}
function set_transport_force(v) {
  _commsMode = v === TFORCE_WIFI ? COMMS_MODE_WIFI : COMMS_MODE_UART;
  logLine("Transport override -> " + (v === TFORCE_WIFI ? "WiFi" : "UART"));
}

// ---- WiFi helpers (WiFi-mode WiFi-setup screen) ----
function wifi_in_ap_mode() {
  return pendantMachine.wifiInApMode;
}
function wifi_status_str() {
  if (pendantMachine.wifiInApMode) return "AP / captive portal";
  return pendantConnected ? "Connected" : "Connecting...";
}
function wifi_last_error() {
  return "";
}
function wifi_ap_ssid() {
  return "FluidDial-Setup";
}
function websocket_is_connected() {
  return pendantConnected;
}
function wifi_active_config() {
  return {
    valid: pendantMachine.wifiSSID !== "---",
    ssid: pendantMachine.wifiSSID,
    fluidnc_ip: pendantMachine.ipAddress,
  };
}
function wifi_stop_ap() {
  pendantMachine.wifiInApMode = false;
  logLine("wifi_stop_ap()");
}

// ---- seed data for the list screens ----
// A handful of realistic names, then generated filler so long lists can be
// exercised from the bench "SD files" control.  simSdTotal is what the
// *controller* would have returned — simSdFiles is capped at SD_MAX_FILES the
// way the firmware's copy-out loop caps it, so the "+" marker can be shown.
const SIM_SD_SEED = [
  "bracket_v3.nc",
  "logo_engrave.gcode",
  "pocket_op1.nc",
  "facing_pass.nc",
  "drill_grid.gcode",
  "spoilboard_surface.nc",
  "test_circle.nc",
];
const SD_MAX_FILES = 100;

let simSdTotal = SIM_SD_SEED.length;   // pre-cap count from the controller
let simSdFiles = SIM_SD_SEED.slice();  // what the pendant actually holds

// A small tree, keyed by absolute path.  Entries are {name, dir}.  FluidNC
// returns leaf names only and marks directories with size -1; the pendant sorts
// folders to the top and synthesises the ".." row itself.
const SIM_SD_TREE = {
  "/sd": [
    { name: "jobs", dir: true }, { name: "fixtures", dir: true },
    { name: "bracket_v3.nc", dir: false }, { name: "facing_pass.nc", dir: false },
    { name: "logo_engrave.gcode", dir: false }, { name: "test_circle.nc", dir: false },
  ],
  "/sd/jobs": [
    { name: "weekend", dir: true },
    { name: "panel_front.nc", dir: false }, { name: "panel_rear.nc", dir: false },
    { name: "spoilboard_surface.nc", dir: false },
  ],
  "/sd/jobs/weekend": [
    { name: "parts", dir: true }, { name: "batch_01.nc", dir: false },
  ],
  "/sd/jobs/weekend/parts": [
    { name: "a_long_part_name_that_overflows.nc", dir: false },
    { name: "pocket_op1.nc", dir: false },
  ],
  "/sd/fixtures": [{ name: "vise_stop.nc", dir: false }],
  "/sd/jobs/weekend/parts/revision_b_final": [
    { name: "deep_part.nc", dir: false },
  ],
};
let simSdUseTree = false;   // bench toggle: flat generated list vs the tree above

// Build a list of `total` names and cap it the way the firmware does.
function simSetSdFileCount(total) {
  simSdUseTree = false;
  simSdTotal = total;
  const all = [];
  for (let i = 0; i < total; i++) {
    all.push(i < SIM_SD_SEED.length ? SIM_SD_SEED[i]
                                    : `part_${String(i + 1).padStart(3, "0")}_op1.nc`);
  }
  all.sort();                              // FluidNC's listing is sorted before copy-out
  simSdFiles = all.slice(0, SD_MAX_FILES);  // firmware caps here and drops the rest
}
const simMacros = [
  { name: "Go to Front", cmd: "cmd:G53 G0 Y0" },
  { name: "Park (rear)", cmd: "cmd:G53 G0 Z0;G53 G0 Y-10" },
  { name: "Spindle Warmup", cmd: "/sd/warmup.nc" },
  { name: "Tool Change Pos", cmd: "cmd:G53 G0 Z0;G53 G0 X0 Y0" },
  { name: "Probe Z Quick", cmd: "/localfs/probez.nc" },
];
