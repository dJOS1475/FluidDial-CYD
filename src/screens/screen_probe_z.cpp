/*
 * screen_probe_z.cpp  —  SCR1: Z Surface Probe
 *
 * Probes Z only, using the selected probe type (3D probe or touch plate).
 * G-code generated internally:
 *   G91 G21
 *   G38.2 Z-{maxZTravel} F{probeRate}
 *   G90
 *   G10 L20 P{n} Z{plateThick}   ; for touch plate
 *   G10 L20 P{n} Z{ballDia/2}    ; for 3D probe (centre offset)
 *   G91
 *   G0 Z{retractDist}
 *   G90
 *
 * focusedField: 0 = Max Z travel   1 = Retract dist
 */

#include "pendant_shared.h"
#include "screen_probe.h"
#include "screen_probe_z.h"

void enterProbeZ() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
    pendantProbeV2.focusedField   = 0;
    pendantProbeV2.confirmActive  = false;
    pendantProbeV2.dialAccelCount = 0;
}

void exitProbeZ() {}

// ── G-code execution ─────────────────────────────────────────────────────────

static void runProbeZ() {
    if (!pendantConnected) return;

    // Coordinate system P number: G54=1, G55=2, G56=3, G57=4
    int pNum = pendantProbing.selectedCoordIndex + 1;

    // Z offset at trigger: ball radius (3D probe) or plate thickness (either plate)
    float zOffset = probeIs3D() ? pendantProbeV2.ballDia / 2.0f
                                : pendantProbeV2.plateThick;

    char buf[64];
    send_line("G91 G21");
    snprintf(buf, sizeof(buf), "G38.2 Z-%.3f F%.0f",
             pendantProbeV2.maxZTravel, pendantProbeV2.probeRate);
    send_line(buf);
    send_line("G90");
    snprintf(buf, sizeof(buf), "G10 L20 P%d Z%.3f", pNum, zOffset);
    send_line(buf);
    send_line("G91");
    snprintf(buf, sizeof(buf), "G0 Z%.3f", pendantProbeV2.retractDist);
    send_line(buf);
    send_line("G90");
}

// ── Layout ───────────────────────────────────────────────────────────────────
//   drawTitle           y=0   h=35
//   Pos panel           y=38  h=38
//   Param buttons       y=79  h=38  (Max Z travel | Retract dist, equal width)
//   Sets row            y=120 h=38
//   Warn banner         y=198 h=38
//   Settings link       y=239 h=38
//   Btn pair            y=280 h=38

// Shared helper — draws one of the two param buttons (tap-to-focus, dial-adjustable).
// Value displayed in mm or inch equiv; steps are always whole-mm via the dial handler.
static void drawZParamButton(int x, int y, int w, int h,
                              const char* label, float valueMm, bool focused) {
    bool inInch = pendantMachine.inInches;
    uint16_t bg  = focused ? PROBE_SEL_BG   : PROBE_BG_SCREEN;
    uint16_t bdr = focused ? PROBE_C_YELLOW : PROBE_C_DIMBLUE;
    uint16_t vc  = focused ? PROBE_C_YELLOW : PROBE_C_RED;
    display.fillRoundRect(x, y, w, h, 8, bg);
    display.drawRoundRect(x, y, w, h, 8, bdr);

    // Label (small, vertically centred in top half)
    display.setTextSize(1);
    display.setTextColor(focused ? COLOR_WHITE : PROBE_C_LBLUE);
    int16_t lw = display.textWidth(label);
    display.setCursor(x + (w - lw) / 2, y + 5);
    display.print(label);

    // Value (large, bottom)
    char valBuf[16];
    if (inInch)
        snprintf(valBuf, sizeof(valBuf), "%.3f in", valueMm / 25.4f);
    else
        snprintf(valBuf, sizeof(valBuf), "%.0f mm", valueMm);
    display.setTextSize(2);
    display.setTextColor(vc);
    int16_t vw = display.textWidth(valBuf);
    display.setCursor(x + (w - vw) / 2, y + 17);
    display.print(valBuf);
}

// Sets — static info row showing which WCS axis will be zeroed
static void drawSetsRow() {
    display.fillRoundRect(5, 120, 230, 38, 8, PROBE_BG_SCREEN);
    display.drawRoundRect(5, 120, 230, 38, 8, PROBE_C_DIMBLUE);

    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    int16_t lw = display.textWidth("Sets");
    display.setCursor(120 - lw / 2, 125);
    display.print("Sets");

    char setsBuf[16];
    snprintf(setsBuf, sizeof(setsBuf), "%s Z0", pendantProbing.selectedCoordSystem.c_str());
    display.setTextSize(2);
    display.setTextColor(PROBE_C_BLUE);
    int16_t vw = display.textWidth(setsBuf);
    display.setCursor(120 - vw / 2, 137);
    display.print(setsBuf);
}

void drawProbeZScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("Z SURFACE");
    int fo = pendantProbeV2.focusedField;
    probeDrawPosPanel(38);   // compact, consistent with the other routine screens
    drawZParamButton(  5, 79, 112, 38, "Max Z travel", pendantProbeV2.maxZTravel,  fo==0);
    drawZParamButton(122, 79, 113, 38, "Retract dist", pendantProbeV2.retractDist, fo==1);
    drawSetsRow();

    // Warning is type-aware: a 3D probe has no plate clip.
    probeDrawWarn(198, probeIs3D() ? "! Verify probe is connected"
                                   : "! Verify plate clip is connected", false, 38);

    drawButton(5, 239, 230, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);

    // Bottom buttons: Main Menu | Probe
    drawButton(  5, 280, 112, 38, "Main Menu", PROBE_BTN_BLUE,  COLOR_WHITE, 2);
    drawButton(123, 280, 112, 38, "Probe",     PROBE_BTN_GREEN, COLOR_WHITE, 2);

    if (pendantProbeV2.confirmActive)
        probeDrawConfirmOverlay("Z SURFACE");
}

void updateProbeZScreen() {
    if (currentPendantScreen != PSCREEN_PROBE_Z) return;
    probeDrawPosPanel(38);
}

void handleProbeZTouch(int x, int y) {
    // Confirm overlay
    if (pendantProbeV2.confirmActive) {
        if (isTouchInBounds(x, y, 28, 175, 78, 32)) {
            // CANCEL
            pendantProbeV2.confirmActive = false;
            drawProbeZScreen();
        } else if (isTouchInBounds(x, y, 114, 175, 98, 32)) {
            // CONFIRM — run probe
            pendantProbeV2.confirmActive = false;
            runProbeZ();
            currentPendantScreen = PSCREEN_STATUS;
        }
        return;
    }

    // Max Z travel — tap to focus/unfocus
    if (isTouchInBounds(x, y, 5, 79, 112, 38)) {
        pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 0) ? -1 : 0;
        drawProbeZScreen();
        return;
    }
    // Retract dist — tap to focus/unfocus
    if (isTouchInBounds(x, y, 122, 79, 113, 38)) {
        pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 1) ? -1 : 1;
        drawProbeZScreen();
        return;
    }

    // Settings link → SCR0
    if (isTouchInBounds(x, y, 5, 239, 230, 38)) {
        pendantProbeV2.returnScreen   = PSCREEN_PROBE_Z;
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }

    // Main Menu
    if (isTouchInBounds(x, y, 5, 280, 112, 38)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
        return;
    }

    // Probe button → confirm overlay
    if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
        if (!pendantConnected) {
            probeDrawWarn(198, "! Not connected", true, 38);
            return;
        }
        pendantProbeV2.confirmActive = true;
        drawProbeZScreen();
        return;
    }
}
