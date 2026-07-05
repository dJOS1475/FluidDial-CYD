/*
 * sim.js — wires the ported screens into a working device: screen routing and
 * touch/encoder dispatch (ported from CNC_Pendant_UI.cpp), the canvas, the
 * three hardware buttons, the encoder wheel, a 100 ms refresh tick, and a
 * control panel that drives the simulated machine/comms state.
 */

// ===== Screen registry =====
const SCREENS = {
  [PSCREEN_MAIN_MENU]:     { enter: enterMainMenu,     exit: exitMainMenu,     draw: drawMainMenu,            handle: handleMainMenuTouch,     update: [updateMainMenuDisplay] },
  [PSCREEN_STATUS]:        { enter: enterStatus,       exit: exitStatus,       draw: drawStatusScreen,        handle: handleStatusTouch,       update: [updateStatusMachineStatus, updateStatusCurrentFile, updateStatusAxisPositions, updateStatusFeedSpindle] },
  [PSCREEN_JOG_HOMING]:    { enter: enterJogHoming,    exit: exitJogHoming,    draw: drawJogHomingScreen,     handle: handleJogHomingTouch,    update: [updateJogAxisDisplay] },
  [PSCREEN_PROBING_WORK]:  { enter: enterProbingWork,  exit: exitProbingWork,  draw: drawProbingWorkScreen,   handle: handleProbingWorkTouch,  update: [updateWorkMachinePos, updateWorkAreaPos] },
  [PSCREEN_PROBE]:         { enter: enterProbe,        exit: exitProbe,        draw: drawProbeScreen,         handle: handleProbeTouch,        update: [] },
  [PSCREEN_PROBE_CFG_3D]:  { enter: enterProbeCfg3D,   exit: exitProbeCfg3D,   draw: drawProbeCfg3DScreen,    handle: handleProbeCfg3DTouch,   update: [updateProbeCfg3DScreen] },
  [PSCREEN_PROBE_CFG_PLATE]:{ enter: enterProbeCfgPlate,exit: exitProbeCfgPlate,draw: drawProbeCfgPlateScreen, handle: handleProbeCfgPlateTouch,update: [] },
  [PSCREEN_PROBE_Z]:       { enter: enterProbeZ,       exit: exitProbeZ,       draw: drawProbeZScreen,        handle: handleProbeZTouch,       update: [updateProbeZScreen] },
  [PSCREEN_PROBE_CORNER]:  { enter: enterProbeCorner,  exit: exitProbeCorner,  draw: drawProbeCornerScreen,   handle: handleProbeCornerTouch,  update: [updateProbeCornerScreen] },
  [PSCREEN_PROBE_BORE]:    { enter: enterProbeBore,    exit: exitProbeBore,    draw: drawProbeBoreScreen,     handle: handleProbeBoreTouch,    update: [updateProbeBoreScreen] },
  [PSCREEN_PROBE_BOSS]:    { enter: enterProbeBoss,    exit: exitProbeBoss,    draw: drawProbeBossScreen,     handle: handleProbeBossTouch,    update: [updateProbeBossScreen] },
  [PSCREEN_FEEDS_SPEEDS]:  { enter: enterFeedsSpeeds,  exit: exitFeedsSpeeds,  draw: drawFeedsSpeedsScreen,   handle: handleFeedsSpeedsTouch,  update: [updateFeedsSpeedsTopDisplay, updateFeedOverrideDisplay, updateSpindleOverrideDisplay] },
  [PSCREEN_SPINDLE_CONTROL]:{ enter: enterSpindleControl,exit: exitSpindleControl,draw: drawSpindleControlScreen,handle: handleSpindleControlTouch,update: [updateSpindleRPMDisplay] },
  [PSCREEN_MACROS]:        { enter: enterMacros,       exit: exitMacros,       draw: drawMacrosScreen,        handle: handleMacrosTouch,       update: [updateMacrosFileList] },
  [PSCREEN_SD_CARD]:       { enter: enterSDCard,       exit: exitSDCard,       draw: drawSDCardScreen,        handle: handleSDCardTouch,       update: [updateSDCardFileList] },
  [PSCREEN_FLUIDNC]:       { enter: enterFluidNC,      exit: exitFluidNC,      draw: drawFluidNCScreen,       handle: handleFluidNCTouch,      update: [updateFluidNCDisplay] },
  [PSCREEN_WIFI_SETUP]:    { enter: enterWiFiSetup,    exit: exitWiFiSetup,    draw: drawWiFiSetupScreen,     handle: handleWiFiSetupTouch,    update: [updateWiFiSetupDisplay] },
  [PSCREEN_SLEEP]:         { enter: enterSleep,        exit: exitSleep,        draw: drawSleepScreen,         handle: handleSleepTouch,        update: [] },
};

const SCREEN_LABELS = {
  [PSCREEN_MAIN_MENU]: "Main Menu", [PSCREEN_STATUS]: "Status", [PSCREEN_JOG_HOMING]: "Jog & Homing",
  [PSCREEN_PROBING_WORK]: "Work Area", [PSCREEN_PROBE]: "Probe Hub", [PSCREEN_PROBE_CFG_3D]: "Probe Config (3D)",
  [PSCREEN_PROBE_CFG_PLATE]: "Probe Config (Plate)", [PSCREEN_PROBE_Z]: "Probe: Z Surface",
  [PSCREEN_PROBE_CORNER]: "Probe: XYZ Corner", [PSCREEN_PROBE_BORE]: "Probe: Bore", [PSCREEN_PROBE_BOSS]: "Probe: Boss",
  [PSCREEN_FEEDS_SPEEDS]: "Feeds & Speeds", [PSCREEN_SPINDLE_CONTROL]: "Spindle Control",
  [PSCREEN_MACROS]: "Macros", [PSCREEN_SD_CARD]: "SD Card", [PSCREEN_FLUIDNC]: "FluidNC Info", [PSCREEN_WIFI_SETUP]: "WiFi Setup",
};

let display;
let canvas, octx, overlay;

function drawCurrentPendantScreen() {
  SCREENS[currentPendantScreen].draw();
}

function navigateTo(next) {
  if (next === currentPendantScreen) return;
  SCREENS[currentPendantScreen].exit();
  currentPendantScreen = next;
  SCREENS[next].enter();
  drawCurrentPendantScreen();
  const sel = document.getElementById("ctl-screen");
  if (sel) sel.value = next;
}

// ===== Touch dispatch (ported handlePendantTouch) =====
let _lastNavMs = 0;
function handlePendantTouch(x, y) {
  if (millis() - _lastNavMs < 350) return;
  const before = currentPendantScreen;
  SCREENS[currentPendantScreen].handle(x, y);
  if (currentPendantScreen !== before) {
    const dest = currentPendantScreen;
    currentPendantScreen = before;
    navigateTo(dest);
    _lastNavMs = millis();
  }
}

// Persistent state for the jog soft-limit clamp's predicted position (mirrors
// the function-static predMm/lastAxis/lastTickMs in CNC_Pendant_UI.cpp).
const _jogClamp = { predMm: [NaN, NaN, NaN], lastTickMs: 0, lastAxis: -1 };

// Continuous-jog (MPG-style) dial-stop tracking (mirrors CNC_Pendant_UI.cpp).
const JOG_CONTINUOUS_MS = 100, JOG_STOP_MS = 150;
const _jogMpg = { lastTickMs: 0, continuous: false, rapidCount: 0, timer: null };

// ===== Encoder delta (ported handleEncoderDelta) =====
function handleEncoderDelta(delta) {
  if (currentPendantScreen === PSCREEN_SPINDLE_CONTROL && pendantSpindle.dialMode) {
    const maxRPM = pendantMachine.spindleMaxRPM > 0 ? pendantMachine.spindleMaxRPM : 24000;
    const rpmStep = maxRPM <= 10000 ? 100 : 1000;
    pendantSpindle.targetRPM = constrain(pendantSpindle.targetRPM + delta * rpmStep, pendantMachine.spindleMinRPM, maxRPM);
    updateSpindleRPMDisplay();
    return;
  } else if (currentPendantScreen === PSCREEN_JOG_HOMING) {
    if (!pendantConnected) return;
    if (pending_nowait_sends >= 6) return;
    if (pendantJog.speedDialMode) {
      const maxIn = constrain((pendantJog.maxFeedRate / 25.4) | 0, 40, 400);
      if (pendantMachine.inInches) pendantJog.jogSpeedIn = constrain(pendantJog.jogSpeedIn + delta * 20, 40, maxIn);
      else pendantJog.jogSpeedMm = constrain(pendantJog.jogSpeedMm + delta * 500, 1000, pendantJog.maxFeedRate);
      redrawJogSpeedButton();
      updateJogAxisDisplay();
      return;
    }
    if (pendantJog.selectedAxis < 0) return;
    let distance = delta * pendantJog.increment;

    // Continuous-jog (MPG) detection + dial-stop watchdog (mirrors CNC_Pendant_UI.cpp):
    // rapid successive ticks are a spin; when the dial stops for JOG_STOP_MS, send a
    // real-time JogCancel so motion halts at once instead of coasting. A single
    // deliberate detent stays below the threshold and completes fully.
    {
      const now = (typeof millis === "function") ? millis() : Date.now();
      const gap = now - _jogMpg.lastTickMs; _jogMpg.lastTickMs = now;
      if (gap < JOG_CONTINUOUS_MS) { if (++_jogMpg.rapidCount >= 2) _jogMpg.continuous = true; }
      else { _jogMpg.rapidCount = 1; _jogMpg.continuous = false; }
      if (_jogMpg.timer) clearTimeout(_jogMpg.timer);
      _jogMpg.timer = setTimeout(() => {
        _jogMpg.timer = null;
        if (_jogMpg.continuous) {
          fnc_realtime(JogCancel);
          _jogMpg.continuous = false; _jogMpg.rapidCount = 0;
          _jogClamp.predMm = [NaN, NaN, NaN];   // flushed queue → resync prediction
        }
      }, JOG_STOP_MS);
    }

    // Soft-limit clamp (absolute) — mirrors CNC_Pendant_UI.cpp: keep the resulting
    // MACHINE position inside the homed envelope so cumulative G91 jogs can't walk
    // into a hard stop. Home = MPos 0; $23 bit clear → homes +, envelope
    // [-maxTravel, 0]; bit set → homes −, envelope [0, +maxTravel]. Engages only
    // for X/Y/Z once travel + $23 are known and the machine isn't in Alarm.
    // Clamps against a PREDICTED position (leads MPos by queued-but-unexecuted
    // jogs) re-seeded from the real MPos when a burst ends and the machine idles.
    {
      const axis = pendantJog.selectedAxis;
      if (axis >= 0 && axis <= 2 && pendantJog.maxTravel[axis] > 0 &&
          pendantJog.homingDirMask >= 0 && !pendantMachine.status.startsWith("Alarm")) {
        const homesNeg = (pendantJog.homingDirMask >> axis) & 1;
        const travelMm = pendantJog.maxTravel[axis];
        const loMm = homesNeg ? 0 : -travelMm;
        const hiMm = homesNeg ? travelMm : 0;
        const MARGIN_MM = 0.5;
        const mposDisp = [pendantMachine.workX, pendantMachine.workY, pendantMachine.workZ][axis];
        const mposMm = pendantMachine.inInches ? mposDisp * 25.4 : mposDisp;
        let distMm = pendantMachine.inInches ? distance * 25.4 : distance;

        const now = (typeof millis === "function") ? millis() : Date.now();
        const interval = now - _jogClamp.lastTickMs; _jogClamp.lastTickMs = now;
        const newBurst = interval > 400 || axis !== _jogClamp.lastAxis;
        if (Number.isNaN(_jogClamp.predMm[axis]) ||
            (newBurst && pendantMachine.status.startsWith("Idle"))) {
          _jogClamp.predMm[axis] = mposMm;
        }
        _jogClamp.lastAxis = axis;

        if (distMm > 0) { let room = (hiMm - MARGIN_MM) - _jogClamp.predMm[axis]; if (room < 0) room = 0; if (distMm > room) distMm = room; }
        else if (distMm < 0) { let room = (loMm + MARGIN_MM) - _jogClamp.predMm[axis]; if (room > 0) room = 0; if (distMm < room) distMm = room; }
        distance = pendantMachine.inInches ? distMm / 25.4 : distMm;
        if (Math.abs(distance) < 1e-4) return;   // fully blocked at the limit
        _jogClamp.predMm[axis] += distMm;         // commit the queued distance
      }
    }

    const an = ["X", "Y", "Z", "A"];
    const g21 = pendantMachine.inInches ? "G20" : "G21";
    send_line_nowait(`$J=G91 ${g21} ${an[pendantJog.selectedAxis]}${fmtF(distance, 3)} F${pendantJog.jogSpeedMm}`);
    // sim convenience: reflect the jog in both the DRO (work) and the machine
    // position so the Work Area screen and the soft-limit clamp stay consistent.
    const axisKey = ["posX", "posY", "posZ", "posA"][pendantJog.selectedAxis];
    const machKey = ["workX", "workY", "workZ", "workA"][pendantJog.selectedAxis];
    pendantMachine[axisKey] += distance;
    pendantMachine[machKey] += distance;
    syncControlsFromState();
    updateJogAxisDisplay();
    return;
  } else if (
    currentPendantScreen === PSCREEN_PROBE || currentPendantScreen === PSCREEN_PROBE_CFG_3D ||
    currentPendantScreen === PSCREEN_PROBE_CFG_PLATE || currentPendantScreen === PSCREEN_PROBE_Z ||
    currentPendantScreen === PSCREEN_PROBE_CORNER || currentPendantScreen === PSCREEN_PROBE_BORE ||
    currentPendantScreen === PSCREEN_PROBE_BOSS
  ) {
    const fo = pendantProbeV2.focusedField;
    if (fo < 0) return;
    const p = pendantProbeV2;
    if (currentPendantScreen === PSCREEN_PROBE) {
      const step = probeDialStep(delta, fo <= 1 ? 10.0 : 0.1);
      if (fo === 0) p.probeRate = constrain(p.probeRate + delta * step, 10, 3000);
      if (fo === 1) p.seekRate = constrain(p.seekRate + delta * step, 10, 3000);
      if (fo === 2) p.retractDist = constrain(p.retractDist + delta * step, 0.1, 50);
      if (fo === 3) p.maxZTravel = constrain(p.maxZTravel + delta * step, 1, 200);
      drawProbeScreen();
    } else if (currentPendantScreen === PSCREEN_PROBE_CFG_3D) {
      if (p.calState !== 0) return;   // dial inert while a cal overlay is up
      const step = probeDialStep(delta, fo === 1 ? 0.001 : 0.1);
      if (fo === 0) p.ballDia = constrain(p.ballDia + delta * step, 0.1, 20);
      if (fo === 1) p.deflection = constrain(p.deflection + delta * step, -1, 1);   // signed
      if (fo === 2) p.calGaugeWidth = constrain(p.calGaugeWidth + delta * step, 1, 300);
      drawProbeCfg3DScreen();
    } else if (currentPendantScreen === PSCREEN_PROBE_CFG_PLATE) {
      const step = probeDialStep(delta, 0.1);
      if (fo === 0) p.plateThick = constrain(p.plateThick + delta * step, 0.1, 50);
      if (fo === 1) p.plateWidth = constrain(p.plateWidth + delta * step, 1, 200);
      if (fo === 2) p.plateOffX = constrain(p.plateOffX + delta * step, -50, 50);
      if (fo === 3) p.plateOffY = constrain(p.plateOffY + delta * step, -50, 50);
      drawProbeCfgPlateScreen();
    } else if (currentPendantScreen === PSCREEN_PROBE_Z) {
      const base = pendantMachine.inInches ? 1 / 25.4 : 1;
      const step = probeDialStep(delta, base);
      if (fo === 0) p.maxZTravel = constrain(p.maxZTravel + delta * step, 1, 200);
      if (fo === 1) p.retractDist = constrain(p.retractDist + delta * step, 1, 50);
      drawProbeZScreen();
    } else if (currentPendantScreen === PSCREEN_PROBE_CORNER) {
      const step = probeDialStep(delta, 0.1);
      if (fo === 0) p.cornerDepth = constrain(p.cornerDepth + delta * step, 0.1, 50);
      if (fo === 1) p.cornerOver = constrain(p.cornerOver + delta * step, 0.1, 20);
      if (fo === 2) p.cornerRetXY = constrain(p.cornerRetXY + delta * step, 0.1, 20);
      drawProbeCornerScreen();
    } else if (currentPendantScreen === PSCREEN_PROBE_BORE) {
      const step = probeDialStep(delta, 0.1);
      if (fo === 0) p.boreDia = constrain(p.boreDia + delta * step, 0.1, 500);
      if (fo === 1) p.boreOffset = constrain(p.boreOffset + delta * step, 0.1, 50);
      drawProbeBoreScreen();
    } else if (currentPendantScreen === PSCREEN_PROBE_BOSS) {
      const step = probeDialStep(delta, 0.1);
      if (p.bossRect) {
        // 0=X size (bossDia) 1=Y size (bossSizeY) 2=bossDepth 3=bossClear
        if (fo === 0) p.bossDia = constrain(p.bossDia + delta * step, 0.1, 500);
        if (fo === 1) p.bossSizeY = constrain(p.bossSizeY + delta * step, 0.1, 500);
        if (fo === 2) p.bossDepth = constrain(p.bossDepth + delta * step, 0.1, 100);
        if (fo === 3) p.bossClear = constrain(p.bossClear + delta * step, 0.1, 50);
      } else {
        // 0=bossDia 1=bossDepth 2=bossClear
        if (fo === 0) p.bossDia = constrain(p.bossDia + delta * step, 0.1, 500);
        if (fo === 1) p.bossDepth = constrain(p.bossDepth + delta * step, 0.1, 100);
        if (fo === 2) p.bossClear = constrain(p.bossClear + delta * step, 0.1, 50);
      }
      drawProbeBossScreen();
    }
    return;
  } else if (currentPendantScreen === PSCREEN_FEEDS_SPEEDS) {
    if (!pendantConnected) return;
    if (pendantFeeds.dialMode === 1) {
      // 10% per detent, applied by the paced stepper (no burst).
      const base = _feedOvr.target >= 0 ? _feedOvr.target : pendantMachine.feedOverride;
      overrideSetFeedTarget(base + delta * 10);
      updateFeedOverrideDisplay();
    } else if (pendantFeeds.dialMode === 2) {
      const base = _spindleOvr.target >= 0 ? _spindleOvr.target : pendantMachine.spindleOverride;
      overrideSetSpindleTarget(base + delta * 10);
      updateSpindleOverrideDisplay();
    }
    return;
  } else if (currentPendantScreen === PSCREEN_FLUIDNC) {
    const newRot = pendantMachine.rotation === 2 ? 0 : 2;
    pendantMachine.rotation = newRot;
    pendantMachine.displayRotation = newRot === 2 ? "Normal" : "Upside Down";
    pendantMachine.rotationDirty = true;
    drawCurrentPendantScreen();
  }
}

// ===== Hardware buttons (red / yellow / green) =====
// Mapping mirrors the physical pendant's E-Stop / Pause / Cycle-Start roles.
// These are sim approximations (the firmware's button handler lives in
// ardmain.cpp) — they emit the standard GRBL realtime bytes and log them.
function pressButton(which) {
  if (which === "red") {
    logLine("BTN red  -> soft reset (0x18)");
    pendantMachine.spindleRunning = false;
  } else if (which === "yellow") {
    logLine("BTN yellow -> feed hold '!'");
  } else if (which === "green") {
    logLine("BTN green -> cycle start '~'");
    if (pendantSdCard.loadedFile.length > 0) {
      send_line("$SD/Run=" + pendantSdCard.loadedFile);
      pendantSdCard.loadedFile = "";
      navigateTo(PSCREEN_STATUS);
    }
  }
}

// ===== Tap marker overlay (does not touch the device framebuffer) =====
function flashTap(x, y) {
  const octxo = overlay.getContext("2d");
  const scale = overlay.width / 240;
  octxo.clearRect(0, 0, overlay.width, overlay.height);
  octxo.strokeStyle = "rgba(255,255,255,0.85)";
  octxo.lineWidth = 2;
  octxo.beginPath();
  octxo.arc(x * scale, y * scale, 10 * scale, 0, Math.PI * 2);
  octxo.stroke();
  setTimeout(() => octxo.clearRect(0, 0, overlay.width, overlay.height), 130);
}

// ===== Screen sleep (PSCREEN_SLEEP) — mirrors CNC_Pendant_UI.cpp =====
// Hidden, button-less screen that blanks the display after idle while the CNC is
// Idle.  Being the active screen, only handleSleepTouch() is reachable, so a wake
// touch can never hit a control.  (No real backlight in the sim — we just paint
// black; on hardware it's setBrightness(0).)
let SLEEP_TIMEOUT_MS = 15 * 60 * 1000;   // 15 min; `let` so a demo can shorten it
let sleepReturnScreen = PSCREEN_MAIN_MENU;
let lastActivityMs = 0;

function enterSleep() {}                                   // (hardware: setBrightness(0))
function exitSleep() {}                                    // (hardware: restore brightness)
function drawSleepScreen() { display.fillScreen(COLOR_BACKGROUND); }
function handleSleepTouch(/*x, y*/) {                      // any touch wakes; sends nothing
  lastActivityMs = millis();
  currentPendantScreen = sleepReturnScreen;   // wrapper (handlePendantTouch) runs exit/enter/draw
}
// Demo helper: blank immediately (so you don't wait out the 15-min timer).
function forceSleepNow() {
  if (currentPendantScreen === PSCREEN_SLEEP || currentPendantScreen === PSCREEN_WIFI_SETUP) return;
  sleepReturnScreen = currentPendantScreen;
  navigateTo(PSCREEN_SLEEP);
}
function manageScreenSleep() {
  // WiFi (battery) pendants only — wired pendants power down with the controller.
  // Eligible to blank when Idle OR while "Connecting" (not connected); any
  // connected-but-busy state keeps it awake and resets the idle clock.
  if (comms_active_mode() !== COMMS_MODE_WIFI) return;
  const sleepEligible = !pendantConnected || pendantMachine.status.startsWith("Idle");
  if (!sleepEligible) lastActivityMs = millis();
  if (currentPendantScreen === PSCREEN_SLEEP) {
    if (pendantConnected && !pendantMachine.status.startsWith("Idle")) navigateTo(sleepReturnScreen);
  } else if (currentPendantScreen !== PSCREEN_WIFI_SETUP
             && sleepEligible
             && millis() - lastActivityMs >= SLEEP_TIMEOUT_MS) {
    sleepReturnScreen = currentPendantScreen;
    navigateTo(PSCREEN_SLEEP);
  }
}

// ===== Refresh tick (firmware updates panels ~every 100 ms) =====
function tick() {
  manageScreenSleep();
  if (currentPendantScreen === PSCREEN_SLEEP) return;   // nothing to update while blank
  const u = SCREENS[currentPendantScreen].update;
  for (const fn of u) fn();
}

// ===== Log panel =====
function renderLog() {
  const el = document.getElementById("log");
  if (!el) return;
  el.textContent = simLog.slice(-100).join("\n");
  el.scrollTop = el.scrollHeight;
}

// ===== Boot =====
window.addEventListener("DOMContentLoaded", () => {
  canvas = document.getElementById("screen");
  overlay = document.getElementById("overlay");
  octx = canvas.getContext("2d");
  display = new LGFX(octx, 240, 320);

  loadProbeSettings();
  try {
    const jp = JSON.parse(localStorage.getItem("sim.jog") || "{}");
    if (jp.fineIncrements !== undefined) pendantJog.fineIncrements = jp.fineIncrements;
    if (jp.selectedIncrement !== undefined) pendantJog.selectedIncrement = jp.selectedIncrement;
  } catch (e) {}

  restoreSession();

  setupCanvasInput();
  setupButtons();
  setupEncoder();
  buildControls();

  SCREENS[currentPendantScreen].enter();
  drawCurrentPendantScreen();
  setInterval(tick, 100);
  setInterval(saveSession, 1000);
  window.addEventListener("beforeunload", saveSession);
  logLine("Simulator ready — FluidDial-CYD UI");
});

// ===== Session persistence (survives a live-reload so you stay put) =====
function saveSession() {
  try {
    sessionStorage.setItem("sim.session", JSON.stringify({
      screen: currentPendantScreen,
      commsMode: _commsMode,
      connected: pendantConnected,
      synced: pendantSynced,
      machine: pendantMachine,
      probing: pendantProbing,
      jog: pendantJog,
      spindle: pendantSpindle,
      feeds: pendantFeeds,
      probeV2: pendantProbeV2,
    }));
  } catch (e) {}
}

function restoreSession() {
  let s;
  try {
    s = JSON.parse(sessionStorage.getItem("sim.session") || "null");
  } catch (e) {}
  if (!s) return;
  // Firmware-identity fields are owned by the code (state.js / sync.py), not the
  // saved session — otherwise a stale snapshot would shadow a version bump and
  // the sim would keep showing the old value after the code changed.
  const CODE_OWNED = ["fluidDialVersion", "fluidNCVersion"];
  const codeVals = {};
  for (const k of CODE_OWNED) codeVals[k] = pendantMachine[k];
  _commsMode = s.commsMode;
  pendantConnected = s.connected;
  pendantSynced = s.synced;
  Object.assign(pendantMachine, s.machine);
  for (const k of CODE_OWNED) pendantMachine[k] = codeVals[k];
  Object.assign(pendantProbing, s.probing);
  Object.assign(pendantJog, s.jog);
  Object.assign(pendantSpindle, s.spindle);
  Object.assign(pendantFeeds, s.feeds);
  Object.assign(pendantProbeV2, s.probeV2);
  if (SCREENS[s.screen]) currentPendantScreen = s.screen;
}

function setupCanvasInput() {
  canvas.addEventListener("pointerdown", (e) => {
    const rect = canvas.getBoundingClientRect();
    const x = ((e.clientX - rect.left) * 240) / rect.width;
    const y = ((e.clientY - rect.top) * 320) / rect.height;
    flashTap(x, y);
    lastActivityMs = millis();     // any touch counts as activity
    handlePendantTouch(x | 0, y | 0);
  });
}

function setupButtons() {
  document.getElementById("btn-red").addEventListener("click", () => pressButton("red"));
  document.getElementById("btn-yellow").addEventListener("click", () => pressButton("yellow"));
  document.getElementById("btn-green").addEventListener("click", () => pressButton("green"));
}

// Encoder: drag the knob to rotate (15° per detent), scroll wheel, or +/- keys.
function setupEncoder() {
  const knob = document.getElementById("knob");
  const knobInner = document.getElementById("knob-mark");
  let angle = 0, accum = 0, dragging = false, lastA = 0;
  const DEG_PER_DETENT = 15;

  function angleAt(e) {
    const r = knob.getBoundingClientRect();
    const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    return (Math.atan2(e.clientY - cy, e.clientX - cx) * 180) / Math.PI;
  }
  knob.addEventListener("pointerdown", (e) => { dragging = true; lastA = angleAt(e); knob.setPointerCapture(e.pointerId); });
  knob.addEventListener("pointermove", (e) => {
    if (!dragging) return;
    let a = angleAt(e), d = a - lastA;
    if (d > 180) d -= 360; if (d < -180) d += 360;
    lastA = a; angle += d; accum += d;
    knobInner.style.transform = `rotate(${angle}deg)`;
    while (accum >= DEG_PER_DETENT) { accum -= DEG_PER_DETENT; handleEncoderDelta(1); }
    while (accum <= -DEG_PER_DETENT) { accum += DEG_PER_DETENT; handleEncoderDelta(-1); }
  });
  const stop = (e) => { dragging = false; };
  knob.addEventListener("pointerup", stop);
  knob.addEventListener("pointercancel", stop);
  knob.addEventListener("wheel", (e) => { e.preventDefault(); handleEncoderDelta(e.deltaY < 0 ? 1 : -1); }, { passive: false });

  document.getElementById("enc-minus").addEventListener("click", () => handleEncoderDelta(-1));
  document.getElementById("enc-plus").addEventListener("click", () => handleEncoderDelta(1));
  window.addEventListener("keydown", (e) => {
    if (e.target.tagName === "INPUT" || e.target.tagName === "SELECT") return;
    if (e.key === "ArrowLeft" || e.key === "ArrowDown") handleEncoderDelta(-1);
    if (e.key === "ArrowRight" || e.key === "ArrowUp") handleEncoderDelta(1);
  });
}
