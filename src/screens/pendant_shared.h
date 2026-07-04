/*
 * Shared types, state structs, and extern declarations for CNC Pendant screens.
 * Include this in every screen_*.cpp file.
 */

#pragma once

#include "../cnc_pendant_config.h"
#include "../System.h"
#include "../FluidNCModel.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <esp_system.h>

// Captured at boot in setup() (ardmain.cpp).  Useful for diagnostics on
// production builds where serial output is unavailable (UART0 is hijacked
// for FluidNC).  ESP_RST_POWERON / ESP_RST_SW are healthy; ESP_RST_PANIC,
// ESP_RST_TASK_WDT, ESP_RST_BROWNOUT etc. indicate a real fault.
extern esp_reset_reason_t lastResetReason;
const char* resetReasonName(esp_reset_reason_t r);

// Snapshot of the previous boot's last-reached boot-stage marker (from RTC
// memory).  0 = fresh power-on (or stage never set), otherwise the stage
// number tells us how far the previous boot got before the panic/wdt fired.
//
// Core 0 (pendant_hw_task) stages — these dominate during setup and steady
// state, written from the hardware task on Core 0:
//   1=setup    2=init_system  3=setup_pendant  4=hw_task    5=comms_init
//   6=WiFi.begin  7=WL_CONNECTED  8=tcp_begin  9=tcp_begin done
//   10=first status sent  11=first byte received
//   100..107 = sub-steps inside the main loop iteration
extern uint32_t capturedBootStage;

// Core 1 (Arduino loop task) stages — written by loop_pendant() at every
// sub-step.  Diagnoses crashes where the Arduino loop task is the one
// blocking on something (rather than pendant_hw_task on Core 0).
//   0   never entered loop_pendant
//   1   loop_pendant entered (very top)
//   2   action() callback done
//   3   about to process hwEventQueue
//   4   inside CONNECTED handler (requestControllerConfig)
//   5   inside STATE_UPDATE handler (sprite redraw)
//   6   inside GREEN button handler (SD run)
//   7   inside POWER_OFF handler
//   8   queue dispatch finished
//   9   periodic sprite refresh done
//   10  loop_pendant about to return
//   200 requestControllerConfig start
//   208 requestControllerConfig finished
extern uint32_t capturedCore1Stage;

// Iteration counters from the previous boot.  If these are 0 (or very low),
// the relevant loop never actually started running — meaning the crash
// happened in setup or right at task entry.  If they're high (thousands+),
// the loops were iterating fine — the WDT must have fired because IDLE was
// starved by a higher-priority task (most likely the WiFi driver), not
// because our code hung.  These are decisive for distinguishing
// "code is hanging" from "code is fine but starved of CPU".
extern uint32_t capturedCore0Iters;
extern uint32_t capturedCore1Iters;

// NVS-backed checkpoint of the PREVIOUS boot's last known state.  Unlike the
// RTC-backed values above, these survive a power-cycle — they're the only
// diagnostic we have if the pendant freezes hard enough to require a manual
// power-off (which wipes RTC).  Updated by pendant_hw_task every 30 seconds.
extern uint32_t nvsPrevIter0;
extern uint32_t nvsPrevIter1;
extern uint32_t nvsPrevMinHeap;
extern uint32_t nvsPrevNowHeap;

// ===== Screen State Enum =====
enum PendantScreen {
    PSCREEN_MAIN_MENU,
    PSCREEN_STATUS,
    PSCREEN_JOG_HOMING,
    PSCREEN_PROBING_WORK,
    // ── v2.0.0 probe screens ──────────────────────────────────────────────
    PSCREEN_PROBE,            // SCR0  — probe hub (type select, shared settings, routine buttons)
    PSCREEN_PROBE_CFG_3D,     // SCR0a — 3D touch-probe config (ball dia, stylus, etc.)
    PSCREEN_PROBE_CFG_PLATE,  // SCR0b — touch-plate config (thickness, width, offsets)
    PSCREEN_PROBE_Z,          // SCR1  — Z surface probe
    PSCREEN_PROBE_CORNER,     // SCR2  — XYZ corner probe
    PSCREEN_PROBE_BORE,       // SCR3a — bore (inside circle) centre probe
    PSCREEN_PROBE_BOSS,       // SCR3b — boss (outside circle) centre probe
    // ─────────────────────────────────────────────────────────────────────
    PSCREEN_FEEDS_SPEEDS,
    PSCREEN_SPINDLE_CONTROL,
    PSCREEN_MACROS,
    PSCREEN_SD_CARD,
    PSCREEN_FLUIDNC,
    PSCREEN_WIFI_SETUP,
    PSCREEN_SLEEP            // hidden — display-blank after idle; touch-to-wake (not a menu item)
};

// ===== Hardware Event (Core 0 → Core 1) =====
struct HwEvent {
    enum Type : uint8_t {
        BUTTON_RED,
        BUTTON_YELLOW,
        BUTTON_GREEN,
        ENCODER_DELTA,
        STATE_UPDATE,
        CONNECTED,      // edge: pendant transitioned to connected — fetch static config
        POWER_OFF       // red button held 5 s — Core 1 draws shutdown screen then sleeps
    } type;
    int32_t value;
};

// ===== Machine State =====
struct MachineState {
    String status        = "N/C";
    String currentFile   = "";
    int    jobPercent    = 0;     // 0-100, valid only while currentFile is non-empty
    float  posX          = 0.0f;
    float  posY          = 0.0f;
    float  posZ          = 0.0f;
    float  posA          = 0.0f;
    float  workX         = 0.0f;
    float  workY         = 0.0f;
    float  workZ         = 0.0f;
    float  workA         = 0.0f;
    int    feedRate      = 0;
    int    spindleRPM    = 0;

    // Core 0 writes these inside stateMutex (via onDROChange); Core 1 reads inside stateMutex.
    // Core 1 also writes them directly in touch handlers — safe because int is 32-bit atomic
    // on Xtensa LX6, so no torn read is possible. The value self-corrects within ~200 ms when
    // the next DRO update arrives from Core 0.
    int    feedOverride    = 100;
    int    spindleOverride = 100;

    // Core 1 owned — Core 0 never reads or writes these.
    // No mutex required; reads and writes happen only on the UI core.
    String spindleDir     = "Fwd";
    bool   spindleRunning = false;
    String displayRotation  = "Normal";
    int    rotation         = 2;

    int    spindleMaxRPM   = 24000;
    int    spindleMinRPM   = 0;
    String fluidDialVersion = FIRMWARE_VERSION;
    String fluidNCVersion   = "v3.7.16";
    String baudRate         = "1000000";
    String port             = "UART0";
    String connectionStatus = "N/C";
    int    freeHeap         = 0;
    int    numAxes          = 3;
    bool   inInches         = false;
    String workCoordSystem  = "G54";
    String ipAddress        = "---";
    String wifiSSID         = "---";
    // Set true when display rotation is changed via the FluidNC screen encoder.
    // exitFluidNC() observes this flag and writes the new value to NVS once on
    // exit instead of every encoder detent (avoids hammering flash).
    bool   rotationDirty    = false;

    // Battery state — written by Core 0 (pendant_hw_task) every 5 s under stateMutex;
    // read by Core 1 (drawBatteryIcon) without mutex (int/bool are atomic on Xtensa LX6).
    // batteryPercent = -1 means hardware not present or not yet sampled.
    int    batteryPercent   = -1;   // 0–100, or -1 if unavailable
    int    batteryMv        = 0;    // smoothed millivolts (used by FluidNC info screen)
    bool   batteryCharging  = false; // true when IP5306 reports charge-in-progress

    // Cached WiFi state — sampled by pendant_hw_task on Core 0 every ~500 ms.
    // The UI on Core 1 (drawWiFiIcon, screen_wifi_setup) reads these instead
    // of calling WiFi.RSSI() / wifi_in_ap_mode() directly, eliminating the
    // cross-core WiFi.h access that was a suspected crash source.  Int / bool
    // fields are 32-bit atomic on Xtensa LX6 so the read on Core 1 cannot be
    // torn.  -1 means "not yet sampled".
    int    wifiSignalBars   = -1;    // 0..4 bars, or -1 if not sampled / UART mode
    bool   wifiInApMode     = false; // true while captive portal is active
};

struct JogState {
    int          selectedAxis      = 0;     // 0=X, 1=Y, 2=Z, 3=A; -1 = speed dial mode active
    // Transient axis to show in the big DRO WHILE homing (the axis currently
    // being homed).  -1 = not homing → DRO follows selectedAxis (the user's
    // jog-button choice).  Set when a home is started / tracked during the
    // Homing state, cleared when homing ends, so after homing the DRO returns
    // to the jog-selected axis without disturbing selectedAxis.
    int          homingAxis        = -1;
    float        increment         = 0.01f;
    int          selectedIncrement = 1;     // index within the active increment set
    bool         fineIncrements    = true;  // true=fine set, false=coarse; triple-tap rightmost button
    bool         speedDialMode     = false; // true = encoder adjusts jog speed, not axis
    int          jogSpeedMm        = 5000;  // mm/min cap, step 100 (used to limit $J feed rate)
    int          jogSpeedIn        = 200;   // ipm cap,    step  10
    int          maxFeedRate       = 10000; // mm/min cap, updated from $110 on entry
    // Per-axis max travel in mm, updated from $130/$131/$132/$133 on entry.
    // 0 = unknown (not yet reported by controller) → fall back to hard-coded jog cap.
    // Used to clamp per-tick jog distance so a fast wheel turn at a coarse increment
    // can't request a move larger than half the axis travel range.
    int          maxTravel[4]      = { 0, 0, 0, 0 };
    // Homing direction invert mask from $23 (grbl/FluidNC).  Bit per axis:
    // clear = axis homes toward + (switch at the max end, MPos 0 there, travel
    // runs negative → envelope [-maxTravel, 0]); set = homes toward − (switch at
    // the min end → envelope [0, +maxTravel]).  -1 = not yet reported.  Combined
    // with the live machine position it lets the jog handler clamp each move so
    // the ABSOLUTE machine position can never leave the homed travel envelope.
    int          homingDirMask     = -1;
};

// Save jog preferences (fineIncrements + selectedIncrement) to NVS — defined in CNC_Pendant_UI.cpp
extern void saveJogPrefs();

struct SDCardState {
    int    selectedFile  = 0;
    int    scrollOffset  = 0;
    String files[20];
    int    fileCount     = 0;
    bool   loading       = false;
    bool   pendingRun    = false;  // true = file selected, awaiting Load/Run confirmation
    String loadedFile    = "";     // set by Load; green button sends run command
    bool   loadFailed    = false;  // request didn't complete in time → show retry hint
    unsigned long loadStartMs = 0; // millis() when the current request was issued (UI deadline)
};

struct MacroState {
    String content[20];       // display name for each macro (from preferences.json / macrocfg.json)
    String filename[20];      // run path: /sd/..., /localfs/..., or cmd:...
    int    count       = 0;   // number of macros loaded
    int    scrollOffset= 0;
    int    selected    = -1;  // display index of selected macro
    bool   loading     = true;
    bool   pendingRun  = false;
    bool   cacheValid  = false; // true after first successful load; skip re-fetch on re-entry
    bool   loadFailed  = false; // fetch finished/aborted with no macros → show retry hint
    unsigned long loadStartMs = 0;  // millis() when the current fetch began (UI deadline)
};

struct SpindleState {
    int  selectedPreset = 1;     // 0=25%, 1=50%, 2=100% of max
    bool directionFwd   = true;
    bool dialMode       = false; // true = encoder adjusts RPM in 1000 RPM steps
    int  targetRPM      = 0;     // RPM selected by user via preset or dial (sent on Start)
};

struct FeedsState {
    int selectedFeedOverride    = 2;  // 0=50%, 1=75%, 2=100%, 3=125%, 4=150%
    int selectedSpindleOverride = 2;
    int dialMode                = 0;  // 0=none, 1=feed dial active, 2=spindle dial active
};

struct ProbingState {
    String selectedCoordSystem = "G54";
    int    selectedCoordIndex  = 0;
};

// ===== v2.0.0 Probe State =====
// All settings and runtime state for the new 7-screen probe system.
// Persisted to NVS under the "probe" namespace (via saveProbeSettings /
// loadProbeSettings declared in screen_probe.h).
struct ProbeV2State {
    // ── Probe type (SCR0) — see screen_probe.h PROBE_TYPE_* ───────────────
    // 0 = Z-Height Touch Plate   1 = XYZ Touch Plate   2 = 3D Touch Probe
    int  probeTypeIdx  = 0;

    // ── SCR0 shared settings ──────────────────────────────────────────────
    float probeRate    = 150.0f;   // mm/min — fine probe feed rate
    float seekRate     = 600.0f;   // mm/min — fast approach rate (unused in basic Z, reserved)
    float retractDist  = 5.0f;     // mm — lift after probe trigger
    float maxZTravel   = 40.0f;    // mm — max Z travel for probe move (safety limit)

    // ── SCR0a: 3D touch-probe hardware ───────────────────────────────────
    float ballDia      = 2.0f;     // mm — stylus ball diameter
    float deflection   = 0.0f;     // mm — stylus flex before trigger; subtracted
                                   // from the ball radius (0 = off, optional cal)

    // ── SCR0b: touch-plate ────────────────────────────────────────────────
    float plateThick   = 10.0f;    // mm — plate thickness (Z offset correction)
    float plateWidth   = 50.0f;    // mm — plate width (unused in basic Z, reserved)
    float plateOffX    = 0.0f;     // mm — XY offset X
    float plateOffY    = 0.0f;     // mm — XY offset Y

    // ── SCR2: corner probe ────────────────────────────────────────────────
    // cornerIdx: 0=Bot-L  1=Bot-R  2=Top-L  3=Top-R
    // axesIdx:   0=X+Y+Z  1=X+Y   2=Z
    int   cornerIdx    = 0;
    int   axesIdx      = 0;
    float cornerDepth  = 5.0f;     // mm below surface for XY probing (positive = down)
    float cornerOver   = 2.0f;     // mm — overshoot distance (probe travel past expected edge)
    float cornerRetXY  = 3.0f;     // mm — XY retract after each wall touch

    // ── SCR3a: bore probe ─────────────────────────────────────────────────
    float boreDia      = 60.0f;    // mm — nominal bore diameter
    float boreDepth    = 8.0f;     // mm — probe depth below current Z (positive value)
    float boreOffset   = 5.0f;     // mm — clearance from nominal wall before probing
    int   borePasses   = 2;        // probe passes (1–4)

    // ── SCR3b: boss probe ─────────────────────────────────────────────────
    float bossDia      = 60.0f;    // mm — nominal boss diameter
    float bossDepth    = 5.0f;     // mm — probe depth below boss top (positive value)
    float bossClear    = 5.0f;     // mm — clearance outside boss before probing
    int   bossPasses   = 2;        // probe passes (1–4)

    // ── UI runtime state (not persisted) ─────────────────────────────────
    // focusedField: screen-relative index of the field the dial currently adjusts.
    // -1 = no field focused.
    int  focusedField   = -1;
    // confirmActive: true while the CONFIRM overlay is shown before running a probe.
    bool confirmActive  = false;
    // returnScreen: the screen to go back to when ⚙ Shared Settings is tapped from
    // a routine screen (Z, Corner, Bore, Boss).  Set before navigating to SCR0.
    PendantScreen returnScreen = PSCREEN_PROBE;
    // Dial acceleration state — shared across all probe screens.
    int  dialAccelCount = 0;
    unsigned long dialLastMs = 0;
};

// ===== Extern State Variables (defined in CNC_Pendant_UI.cpp) =====
extern MachineState  pendantMachine;
extern JogState      pendantJog;
extern SDCardState   pendantSdCard;
extern MacroState    pendantMacros;
extern SpindleState  pendantSpindle;
extern FeedsState    pendantFeeds;
extern ProbingState  pendantProbing;
extern ProbeV2State  pendantProbeV2;

extern PendantScreen currentPendantScreen;

// ===== Extern Sprites (defined in CNC_Pendant_UI.cpp, reused across screens) =====
extern LGFX_Sprite spriteAxisDisplay;
extern LGFX_Sprite spriteValueDisplay;
extern LGFX_Sprite spriteStatusBar;
extern LGFX_Sprite spriteFileDisplay;

// Allocate a persistent 8-bit panel sprite (depth set BEFORE createSprite()).
// Now used ONLY for the large, BLACK-filled list sprites (macros / SD file
// lists, 230×200) and the black-filled probing-work panels: black (0x0000) is
// identical in 8- and 16-bit, so there's no rgb332 colour tint, and 8-bit halves
// the RAM of a buffer that would be ~92 KB at 16-bit (and wouldn't fit on WiFi).
// The near-neutral grey panels use the 16-bit beginPanelSprite() scratch instead
// (8-bit crushed their blue → greenish).  If minHeap > 0 and free heap is below
// it, or the allocation fails, returns false and leaves the sprite empty —
// callers then fall back to direct drawing (flicker but accurate, never blank).
bool allocPanelSprite(LGFX_Sprite& s, int w, int h, uint32_t minHeap = 0);

// Release all four shared panel sprites.  Call at the top of every enter*()
// so a screen only needs to allocate the ones it uses, and can't inherit
// another screen's buffers.
void releasePanelSprites();

// Shared-scratch panel sprite: every panel on a screen draws into ONE 16-bit
// scratch buffer that grows to the largest panel and is then reused with no
// per-frame alloc/free churn.  endPanelSprite() pushes only the panel's w×h
// region (via a destination clip rect).  Released by releasePanelSprites().
//   int ox, oy;
//   LovyanGFX* g = beginPanelSprite(230, 65, ox, oy, 5, 140);
//   ... draw via g at (ox+.., oy+..) ...
//   endPanelSprite(230, 65, 5, 140);   // same w,h,px,py
// On allocation failure g is &display and (ox,oy)=(px,py) so drawing lands at
// the correct on-screen spot (direct-draw fallback — never blank).
LovyanGFX* beginPanelSprite(int w, int h, int& ox, int& oy, int px, int py);
void       endPanelSprite(int w, int h, int px, int py);

// ===== FreeRTOS Sync Objects (defined in CNC_Pendant_UI.cpp) =====
extern SemaphoreHandle_t stateMutex;
extern QueueHandle_t     hwEventQueue;

// ===== Connection State (set by Core 0 callbacks, read by Core 1) =====
// Use this instead of fnc_is_connected() on Core 1 — fnc_is_connected() sends
// UART bytes which block when no controller is attached.
extern volatile bool pendantConnected;

// True once the link is up AND the controller has reported live state (and the
// post-connect config queries have had a moment to reply).  False from power-up
// until that first sync completes, and again after any disconnect — drives the
// "Connecting" indicator.  Transport-agnostic (works for WiFi and wired).
extern volatile bool pendantSynced;

// ===== Helper Functions (defined in CNC_Pendant_UI.cpp) =====
bool   isTouchInBounds(int tx, int ty, int x, int y, int w, int h);
void   drawRoundRect(int x, int y, int w, int h, int r, uint16_t color);
void   drawButton(int x, int y, int w, int h, String text, uint16_t bgColor, uint16_t textColor, int textSize = 2);
void   drawMultiLineButton(int x, int y, int w, int h, String line1, String line2, uint16_t bgColor, uint16_t textColor, int textSize = 1);
void   drawTitle(String title);
void   drawInfoBox(int x, int y, int w, int h, String label, String value, uint16_t valueColor = COLOR_ORANGE);
void   drawCurrentPendantScreen();
void   navigateTo(PendantScreen next);

// ===== Alarm Description Helper =====
// Returns a short human-readable description for FluidNC/GRBL alarm codes.
// Status strings from FluidNC look like "Alarm:1", "Alarm:2", etc.
inline String alarmDescription(const String& status) {
    if (!status.startsWith("Alarm:")) return "";
    int code = status.substring(6).toInt();
    switch (code) {
        case 1:  return "Hard limit triggered";
        case 2:  return "Soft limit exceeded";
        case 3:  return "Abort during cycle";
        case 4:  return "Probe fail - no contact";
        case 5:  return "Probe fail - contact lost";
        case 6:  return "Homing fail - reset";
        case 7:  return "Homing fail - door open";
        case 8:  return "Homing fail - pull off";
        case 9:  return "Homing fail - no limit";
        case 10: return "Homing fail - on limit";
        default: return "Check controller";
    }
}

// ===== Screen Lifecycle (declared in each screen header, called by coordinator) =====
// Each screen exposes: enterXxx(), exitXxx(), drawXxx(), handleXxxTouch(int,int)
// Plus zero or more updateXxx() sprite-refresh functions called every 100ms.
