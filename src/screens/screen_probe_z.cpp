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
    float zOffset = probeIs3D() ? probeTipOffset3D()
                                : pendantProbeV2.plateThick;

    char buf[64];
    probeActivateWcs();          // zero into the system shown on screen
    send_line("G91 G21");
    // Two-pass: fast seek down to the surface, then slow re-probe for precision.
    probeSeekFine("Z", -pendantProbeV2.maxZTravel,
                  pendantProbeV2.seekRate, pendantProbeV2.probeRate);
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
    uint16_t bdr = focused ? PROBE_C_YELLOW : PROBE_C_TAPBDR;
    uint16_t vc  = focused ? PROBE_C_YELLOW : PROBE_C_BLUE;
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

// Side-view diagram of a Z-surface probe: stylus descending onto the work
// surface, with a down arrow showing the probe direction.
static void drawZDiagram() {
    display.fillRect(22, 192, 76, 10, PROBE_C_DIMBLUE);   // work surface
    display.fillRoundRect(55, 150, 10, 12, 2, PROBE_C_LBLUE);  // probe body/stem
    display.drawLine(60, 161, 60, 189, PROBE_C_YELLOW);   // stylus
    display.fillCircle(60, 191, 2, PROBE_C_YELLOW);       // ball touching surface
    // Probe direction (down arrow beside the stylus)
    display.drawLine(42, 166, 42, 184, PROBE_C_GREEN);    // shaft
    display.drawLine(42, 184, 39, 180, PROBE_C_GREEN);    // head
    display.drawLine(42, 184, 45, 180, PROBE_C_GREEN);
}

// Redraws ONLY the Z-Surface settings fields (opaque boxes) — used by the full
// draw and the dial handler so a jog-dial edit doesn't do a full-screen redraw.
void updateProbeZFields() {
    if (currentPendantScreen != PSCREEN_PROBE_Z) return;
    int fo = pendantProbeV2.focusedField;
    drawZParamButton(122, 84,  111, 33, "Max Z travel", pendantProbeV2.maxZTravel,  fo==0);
    drawZParamButton(122, 120, 111, 33, "Retract dist", pendantProbeV2.retractDist, fo==1);
}

void drawProbeZScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("Z SURFACE");
    probeDrawPosPanel(38);

    // Combined panel: sequence + diagram (left) / settings (right) — boss style.
    display.fillRoundRect(5, 70, 230, 146, 4, PROBE_BG_PANEL);

    // Left column: sequence + diagram
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 73);
    display.print("SEQUENCE");
    drawSeqStep( 8, 87,  1, "Fast seek -Z",  true);
    drawSeqStep( 8, 105, 2, "Slow re-probe", false);
    drawSeqStep( 8, 123, 3, "Set Z0",        false);
    drawZDiagram();

    // Right column: settings
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(122, 73);
    display.print("SETTINGS");
    updateProbeZFields();

    // Result line — what the probe will set
    {
        const char* s = "Sets Z0";
        display.setTextSize(1);
        display.setTextColor(PROBE_C_GREEN);
        display.setCursor(177 - display.textWidth(s) / 2, 166);
        display.print(s);
    }

    // Warning — same height/placement as the Boss screen (type-aware).
    probeDrawWarn(220, probeIs3D() ? "! Verify probe is connected"
                                   : "! Verify plate clip is connected");

    // Bottom-nav row: Back (left) | work-area selector (right)
    drawButton(5, 239, 112, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
    probeDrawWorkAreaButton(123, 239, 112, 38);

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
    if (isTouchInBounds(x, y, 122, 84, 111, 33)) {
        pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 0) ? -1 : 0;
        drawProbeZScreen();
        return;
    }
    // Retract dist — tap to focus/unfocus
    if (isTouchInBounds(x, y, 122, 120, 111, 33)) {
        pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 1) ? -1 : 1;
        drawProbeZScreen();
        return;
    }

    // Back → config hub
    if (isTouchInBounds(x, y, 5, 239, 112, 38)) {
        pendantProbeV2.returnScreen   = PSCREEN_PROBE_Z;
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }
    // Work-area selector → cycle G54..G57
    if (isTouchInBounds(x, y, 123, 239, 112, 38)) {
        probeCycleWorkArea();
        drawProbeZScreen();
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
            probeDrawWarn(220, "! Not connected", true);
            return;
        }
        pendantProbeV2.confirmActive = true;
        drawProbeZScreen();
        return;
    }
}
