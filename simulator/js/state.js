/*
 * state.js — the pendant's runtime state, mirrored from the structs in
 * src/screens/pendant_shared.h.  These are plain globals (like the firmware's
 * extern objects) so every screen port and the control panel share them.
 */

// ===== Screen enum (order matches PendantScreen in pendant_shared.h) =====
const PSCREEN_MAIN_MENU     = "MAIN_MENU";
const PSCREEN_STATUS        = "STATUS";
const PSCREEN_JOG_HOMING    = "JOG_HOMING";
const PSCREEN_PROBING_WORK  = "PROBING_WORK";
const PSCREEN_PROBE         = "PROBE";
const PSCREEN_PROBE_CFG_3D  = "PROBE_CFG_3D";
const PSCREEN_PROBE_CFG_PLATE = "PROBE_CFG_PLATE";
const PSCREEN_PROBE_Z       = "PROBE_Z";
const PSCREEN_PROBE_CORNER  = "PROBE_CORNER";
const PSCREEN_PROBE_BORE    = "PROBE_BORE";
const PSCREEN_PROBE_BOSS    = "PROBE_BOSS";
const PSCREEN_FEEDS_SPEEDS  = "FEEDS_SPEEDS";
const PSCREEN_SPINDLE_CONTROL = "SPINDLE_CONTROL";
const PSCREEN_MACROS        = "MACROS";
const PSCREEN_SD_CARD       = "SD_CARD";
const PSCREEN_FLUIDNC       = "FLUIDNC";
const PSCREEN_WIFI_SETUP    = "WIFI_SETUP";
const PSCREEN_SLEEP         = "SLEEP";   // hidden — display blank after idle; touch-to-wake

// ===== Machine state =====
const pendantMachine = {
  status: "N/C",
  currentFile: "",
  jobPercent: 0,
  posX: 0, posY: 0, posZ: 0, posA: 0,
  workX: 0, workY: 0, workZ: 0, workA: 0,
  feedRate: 0,
  spindleRPM: 0,
  feedOverride: 100,
  spindleOverride: 100,
  spindleDir: "Fwd",
  spindleRunning: false,
  displayRotation: "Normal",
  rotation: 2,
  spindleMaxRPM: 24000,
  spindleMinRPM: 0,
  fluidDialVersion: "v2.1.0",
  fluidNCVersion: "v3.7.16",
  baudRate: "1000000",
  port: "UART0",
  connectionStatus: "N/C",
  freeHeap: 148000,
  numAxes: 3,
  inInches: false,
  workCoordSystem: "G54",
  ipAddress: "---",
  wifiSSID: "---",
  rotationDirty: false,
  batteryPercent: -1,
  batteryMv: 0,
  batteryCharging: false,
  wifiSignalBars: -1,
  wifiInApMode: false,
};

const pendantJog = {
  selectedAxis: 0,
  homingAxis: -1,
  increment: 0.01,
  selectedIncrement: 1,
  fineIncrements: true,
  speedDialMode: false,
  jogSpeedMm: 5000,
  jogSpeedIn: 200,
  maxFeedRate: 10000,
  maxTravel: [0, 0, 0, 0],
};

const pendantSdCard = {
  selectedFile: 0,
  scrollOffset: 0,
  files: [],
  fileCount: 0,
  loading: false,
  pendingRun: false,
  loadedFile: "",
  loadFailed: false,
  loadStartMs: 0,
};

const pendantMacros = {
  content: [],
  filename: [],
  count: 0,
  scrollOffset: 0,
  selected: -1,
  loading: true,
  pendingRun: false,
  cacheValid: false,
  loadFailed: false,
  loadStartMs: 0,
};

const pendantSpindle = {
  selectedPreset: 1,
  directionFwd: true,
  dialMode: false,
  targetRPM: 0,
};

const pendantFeeds = {
  selectedFeedOverride: 2,
  selectedSpindleOverride: 2,
  dialMode: 0,
};

const pendantProbing = {
  selectedCoordSystem: "G54",
  selectedCoordIndex: 0,
};

const pendantProbeV2 = {
  probeTypeIdx: 0,
  probeRate: 150.0,
  seekRate: 500.0,
  retractDist: 5.0,
  maxZTravel: 40.0,
  ballDia: 2.0,
  stylusLen: 22.0,
  deflection: 0.01,
  preTravel: 0.005,
  plateThick: 10.0,
  plateWidth: 50.0,
  plateOffX: 0.0,
  plateOffY: 0.0,
  cornerIdx: 0,
  axesIdx: 0,
  cornerDepth: 5.0,
  cornerOver: 2.0,
  cornerRetXY: 3.0,
  boreDia: 60.0,
  boreDepth: 8.0,
  boreOffset: 5.0,
  borePasses: 2,
  bossDia: 60.0,
  bossDepth: 5.0,
  bossClear: 5.0,
  bossPasses: 2,
  focusedField: -1,
  confirmActive: false,
  returnScreen: PSCREEN_PROBE,
  dialAccelCount: 0,
  dialLastMs: 0,
};

// Connection flags — set by the control panel (Core 0 callbacks in firmware).
let pendantConnected = false;
let pendantSynced = false;

// Current screen.
let currentPendantScreen = PSCREEN_MAIN_MENU;

// ===== Shared sprite stubs =====
// The firmware double-buffers some panels into LGFX sprites.  In the sim we
// always take the screens' "direct draw" fallback path (which draws to the
// same absolute coordinates and is visually identical, with no flicker on a
// canvas).  These stubs make getBuffer() return null so that path is chosen.
const _spriteStub = {
  getBuffer: () => null,
  createSprite() {},
  deleteSprite() {},
  fillSprite() {},
  pushSprite() {},
};
const spriteAxisDisplay = _spriteStub;
const spriteValueDisplay = _spriteStub;
const spriteStatusBar = _spriteStub;
const spriteFileDisplay = _spriteStub;

function allocPanelSprite() {
  return false;
}
function releasePanelSprites() {}

// beginPanelSprite/endPanelSprite: take the direct-draw branch — return the
// display and place drawing at the on-screen (px,py).  Matches the firmware's
// allocation-failure fallback exactly.
// JS convenience wrapper used by the screen ports in place of the firmware's
// `LovyanGFX* g = beginPanelSprite(w,h,ox,oy,px,py)` (which uses C++ output
// params).  Always direct-draws onto `display` at (px,py).
function panel(w, h, px, py) {
  return { g: display, ox: px, oy: py };
}
