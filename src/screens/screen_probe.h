#pragma once

// ── Probe colour palette — uses the same dark/orange/gray theme as the rest of the UI.
// All PROBE_* constants alias the shared COLOR_* macros from cnc_pendant_config.h.
#define PROBE_BG_SCREEN   COLOR_BACKGROUND    // 0x0000 — pure black
#define PROBE_BG_PANEL    COLOR_DARKER_BG     // 0x2104 — dark gray panels
#define PROBE_C_YELLOW    COLOR_ORANGE        // 0xFD20 — primary value / highlight colour
#define PROBE_C_GREEN     COLOR_GREEN         // 0x07E0
#define PROBE_C_RED       COLOR_RED           // 0xF800
#define PROBE_C_BLUE      COLOR_CYAN          // 0x07FF — secondary values
#define PROBE_C_LBLUE     COLOR_GRAY_TEXT     // 0x7BEF — section labels
#define PROBE_C_DIMBLUE   0x4208              // dim gray — unit suffixes, hint text
#define PROBE_C_TAPBDR    COLOR_GRAY_TEXT     // 0x7BEF — border of every TAPPABLE field
                                              // (read-only panels are borderless; a
                                              //  yellow border means focused/active)
#define PROBE_BTN_GREEN   COLOR_DARK_GREEN    // 0x0360 — PROBE action button
#define PROBE_BTN_YELLOW  COLOR_ORANGE        // 0xFD20
#define PROBE_BTN_BLUE    COLOR_BLUE          // 0x1C9F — Main Menu / nav buttons
#define PROBE_BTN_NAVY    COLOR_BUTTON_GRAY   // 0x31A6
#define PROBE_BTN_TEAL    COLOR_TEAL_BRIGHT   // 0x05B6 — Configure button (distinct from blue nav)
#define PROBE_SEL_BG      COLOR_DARKER_BG     // 0x2104 — focused field background
#define PROBE_WARN_BG     0x2880              // amber warning bg (functional colour — keep)
#define PROBE_WARN_BDR    0x6800              // amber warning border
#define PROBE_WARNR_BG    0x2800              // red warning bg
#define PROBE_WARNR_BDR   0x6808              // red warning border
#define PROBE_AMBER       COLOR_ORANGE        // 0xFD20 — sequence badge active

// ── Probe type model (pendantProbeV2.probeTypeIdx) ──────────────────────────
//   0 = Z-Height Touch Plate  — Z only.            Routine: Z Surface
//   1 = XYZ Touch Plate       — Z + XY edges.      Routines: Z Surface, XYZ Corner
//   2 = 3D Touch Probe        — full ball probe.   Routines: all four
// Only the 3D probe applies a ball-radius edge/Z offset; both plate types use
// the plate thickness for the Z offset.
enum {
    PROBE_TYPE_ZPLATE   = 0,
    PROBE_TYPE_XYZPLATE = 1,
    PROBE_TYPE_3D       = 2,
    PROBE_TYPE_COUNT    = 3,
};
inline bool probeIs3D()    { return pendantProbeV2.probeTypeIdx == PROBE_TYPE_3D; }

// Effective 3D-probe tip offset: ball radius minus stylus deflection.  The
// stylus flexes past first contact before the probe triggers, so the reported
// position is `deflection` beyond the true surface — subtracting it corrects
// the zero.  Deflection defaults to 0 (off); calibrate it for extra accuracy.
// (Centre-finding is unaffected: deflection is radially symmetric and cancels.)
inline float probeTipOffset3D() {
    float o = pendantProbeV2.ballDia / 2.0f - pendantProbeV2.deflection;
    return o > 0.0f ? o : 0.0f;
}
inline bool probeIsPlate() { return pendantProbeV2.probeTypeIdx != PROBE_TYPE_3D; }
// Number of probe routines a given type exposes (Z, +Corner, +Bore/Boss).
inline int  probeRoutineCount() {
    return pendantProbeV2.probeTypeIdx == PROBE_TYPE_3D       ? 4
         : pendantProbeV2.probeTypeIdx == PROBE_TYPE_XYZPLATE ? 2
                                                              : 1;
}

// SCR0 — probe hub
void enterProbe();
void exitProbe();
void drawProbeScreen();
void updateProbeScreen();
void handleProbeTouch(int x, int y);

// NVS persistence — shared across all probe screens
void saveProbeSettings();
void loadProbeSettings();

// Shared drawing helpers used by all probe screens
void probeDrawPosPanel(int y, int h = 28);
void probeDrawSettingsLink(int y);
void probeDrawSelBar(int y, const char* fieldName, float value, const char* unit, int decimals = 3);
void probeDrawSelBarInt(int y, const char* fieldName, int value);
void probeDrawKVTouch(int x, int y, int w, int h, const char* label, float value, const char* unit,
                      uint16_t valColor, bool focused, int decimals = 3);
void probeDrawKVTouchInt(int x, int y, int w, int h, const char* label, int value,
                         uint16_t valColor, bool focused);
void probeDrawWarn(int y, const char* msg, bool isRed = false, int h = 14);
void probeDrawConfirmOverlay(const char* routineName);

// Work-area button (shared across all routine screens): shows the WCS the probe
// will zero (G54..G57) in the Z-Surface "Sets" style; tap cycles it.
void probeDrawWorkAreaButton(int x, int y, int w, int h);
void probeCycleWorkArea();

// Sequence-step badge (numbered circle + label) used by the routine screens.
void drawSeqStep(int x, int y, int num, const char* txt, bool active);

// Make the selected work coordinate system (G54..G57) active on the controller,
// so a probe's G10 L20 zero takes effect immediately in the system on screen
// (no manual re-zero).  Every routine calls this before it probes.
void probeActivateWcs();

// Dial acceleration helper — returns the adjusted step multiplier
float probeDialStep(int delta, float baseStep);

// Crash-safe two-pass approach along one axis: fast seek to contact, back off a
// little, then slow re-probe.  Ends AT the fine trigger (no trailing retract) so
// the caller can set a WCS axis there, or save the trigger param.  Stays in the
// caller's G91 frame; seekDist is signed (e.g. "Z", -maxZ for a downward probe).
void probeSeekFine(const char* axis, float seekDist, float seekF, float fineF);
