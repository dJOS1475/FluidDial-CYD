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

// ===== Screen State Enum =====
enum PendantScreen {
    PSCREEN_MAIN_MENU,
    PSCREEN_STATUS,
    PSCREEN_JOG_HOMING,
    PSCREEN_PROBING_WORK,
    PSCREEN_PROBING,
    PSCREEN_FEEDS_SPEEDS,
    PSCREEN_SPINDLE_CONTROL,
    PSCREEN_MACROS,
    PSCREEN_SD_CARD,
    PSCREEN_FLUIDNC
};

// ===== Hardware Event (Core 0 → Core 1) =====
struct HwEvent {
    enum Type : uint8_t {
        BUTTON_RED,
        BUTTON_YELLOW,
        BUTTON_GREEN,
        ENCODER_DELTA,
        STATE_UPDATE
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
    String spindleDir    = "Fwd";
    bool   spindleRunning = false;
    int    feedOverride    = 100;
    int    spindleOverride = 100;
    int    spindleMaxRPM   = 24000;
    int    spindleMinRPM   = 0;
    String fluidDialVersion = "v1.5.3";
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
    String displayRotation  = "Normal";
    int    rotation         = 2;
};

struct JogState {
    int          selectedAxis      = 0;     // 0=X, 1=Y, 2=Z, 3=A; -1 = speed dial mode active
    float        increment         = 0.01f;
    int          selectedIncrement = 1;     // index within the active increment set
    bool         fineIncrements    = true;  // true=fine set, false=coarse; triple-tap rightmost button
    bool         speedDialMode     = false; // true = encoder adjusts jog speed, not axis
    int          jogSpeedMm        = 5000;  // mm/min cap, step 100 (used to limit $J feed rate)
    int          jogSpeedIn        = 200;   // ipm cap,    step  10
    int          maxFeedRate       = 10000; // mm/min cap, updated from $110 on entry
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
};

struct MacroState {
    String content[10];       // macro0..macro9 content from config.yaml (empty = not set)
    int    indices[10] = {};  // original macro number for each visible entry
    int    count       = 0;   // number of non-empty macros
    int    scrollOffset= 0;
    int    selected    = -1;  // display index of selected macro
    bool   loading     = true;
    bool   pendingRun  = false;
};

struct SpindleState {
    int  selectedPreset = 1;   // 0=25%, 1=50%, 2=100% of max
    bool directionFwd   = true;
    bool dialMode       = false; // true = encoder adjusts RPM in 1000 RPM steps
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

struct ProbeState {
    int    selectedProbeType = -1;
    float  feedRate          = 100.0f;
    float  maxTravel         = 25.0f;
    float  toolDia           = 6.0f;
    String status            = "Ready";
    float  lastZ             = 0.0f;
};

// ===== Extern State Variables (defined in CNC_Pendant_UI.cpp) =====
extern MachineState  pendantMachine;
extern JogState      pendantJog;
extern SDCardState   pendantSdCard;
extern MacroState    pendantMacros;
extern SpindleState  pendantSpindle;
extern FeedsState    pendantFeeds;
extern ProbingState  pendantProbing;
extern ProbeState    pendantProbe;

extern PendantScreen currentPendantScreen;
extern PendantScreen previousPendantScreen;

// ===== Extern Sprites (defined in CNC_Pendant_UI.cpp, reused across screens) =====
extern LGFX_Sprite spriteAxisDisplay;
extern LGFX_Sprite spriteValueDisplay;
extern LGFX_Sprite spriteStatusBar;
extern LGFX_Sprite spriteFileDisplay;
extern bool        spritesInitialized;

// ===== FreeRTOS Sync Objects (defined in CNC_Pendant_UI.cpp) =====
extern SemaphoreHandle_t stateMutex;
extern QueueHandle_t     hwEventQueue;

// ===== Connection State (set by Core 0 callbacks, read by Core 1) =====
// Use this instead of fnc_is_connected() on Core 1 — fnc_is_connected() sends
// UART bytes which block when no controller is attached.
extern volatile bool pendantConnected;

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
