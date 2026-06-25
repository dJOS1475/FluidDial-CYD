/*
 * screen_probe_corner.cpp  —  SCR2: XYZ Corner Probe
 *
 * Probes the X, Y, and/or Z axes of a workpiece corner.
 * User pre-positions probe above the corner edge.
 *
 * Corner options (cornerIdx): 0=Bot-Left  1=Bot-Right  2=Top-Left  3=Top-Right
 * Axes options  (axesIdx):    0=X+Y+Z     1=X+Y        2=Z
 *
 * focusedField: 0=cornerDepth  1=cornerOver  2=cornerRetXY
 *
 * G-code generated internally using G91 (relative) mode throughout.
 * Ball radius / plate-edge compensation applied to G10 L20.
 */

#include "pendant_shared.h"
#include "screen_probe.h"
#include "screen_probe_corner.h"

void enterProbeCorner() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
    pendantProbeV2.focusedField   = 0;
    pendantProbeV2.confirmActive  = false;
    pendantProbeV2.dialAccelCount = 0;
}

void exitProbeCorner() {}

// ── G-code execution ─────────────────────────────────────────────────────────

static void runProbeCorner() {
    if (!pendantConnected) return;

    int   pNum    = pendantProbing.selectedCoordIndex + 1;
    float rate    = pendantProbeV2.probeRate;   // fine (slow) re-probe feed
    float seekF   = pendantProbeV2.seekRate;    // fast seek feed
    float depth   = pendantProbeV2.cornerDepth;    // positive mm below surface
    float over    = pendantProbeV2.cornerOver;     // overshoot approach distance
    float retXY   = pendantProbeV2.cornerRetXY;
    float retZ    = pendantProbeV2.retractDist;
    float maxZ    = pendantProbeV2.maxZTravel;
    bool  is3D    = probeIs3D();
    float platZ   = is3D ? (pendantProbeV2.ballDia / 2.0f) : pendantProbeV2.plateThick;
    // For XY compensation: ball radius shifts edge; use half ball dia
    float edgeOfs = is3D ? (pendantProbeV2.ballDia / 2.0f) : 0.0f;

    int   cIdx    = pendantProbeV2.cornerIdx;   // 0=BotL 1=BotR 2=TopL 3=TopR

    // Direction signs: X probe direction (+1 or -1), Y probe direction
    // Bot-Left:  probe toward +X (+1), toward +Y (+1) — we approach from outside (left/below)
    // Bot-Right: probe toward -X (-1), toward +Y (+1)
    // Top-Left:  probe toward +X (+1), toward -Y (-1)
    // Top-Right: probe toward -X (-1), toward -Y (-1)
    int xDir = (cIdx == 0 || cIdx == 2) ? +1 : -1;
    int yDir = (cIdx == 0 || cIdx == 1) ? +1 : -1;

    char buf[80];
    bool doXY = true;   // corner probe always does X/Y/Z
    bool doZ  = true;

    // ── Z probe (surface, via plate) ─────────────────────────────────────
    if (doZ) {
        send_line("G91 G21");
        probeSeekFine("Z", -maxZ, seekF, rate);   // two-pass surface touch
        send_line("G90");
        snprintf(buf, sizeof(buf), "G10 L20 P%d Z%.3f", pNum, platZ);
        send_line(buf);
        send_line("G91");
        snprintf(buf, sizeof(buf), "G0 Z%.3f F1000", retZ);
        send_line(buf);
    }

    // ── XY probe ─────────────────────────────────────────────────────────
    if (doXY) {
        // Lower to XY probe depth (relative from current Z after retract)
        // Net movement: down by (depth + retZ) to reach depth below surface
        // We've already retracted retZ, so go down (depth + retZ) more if Z was probed,
        // or just depth if Z was not probed.
        float dropZ = doZ ? (depth + retZ) : depth;
        snprintf(buf, sizeof(buf), "G91");
        send_line(buf);
        snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", dropZ);
        send_line(buf);

        // ── X probe ──────────────────────────────────────────────────────
        // Move overshoot in opposite direction to X wall (away from workpiece)
        snprintf(buf, sizeof(buf), "G0 X%.3f F1000", -xDir * over);
        send_line(buf);
        // Two-pass probe toward X wall
        probeSeekFine("X", xDir * (over + 20.0f), seekF, rate);
        // Set X WCS: when probing toward +X, edge is +edgeOfs past probe trigger
        //   G10 L20 gives: current machine pos = WCS value supplied
        //   We want WCS X=0 at the workpiece edge:
        //     probing +X: edge is edgeOfs ahead of trigger → supply -edgeOfs
        //     probing -X: edge is edgeOfs behind trigger   → supply +edgeOfs
        send_line("G90");
        float xWCS = xDir > 0 ? -edgeOfs : +edgeOfs;
        snprintf(buf, sizeof(buf), "G10 L20 P%d X%.3f", pNum, xWCS);
        send_line(buf);
        // Retract from X wall
        send_line("G91");
        snprintf(buf, sizeof(buf), "G0 X%.3f F1000", -xDir * retXY);
        send_line(buf);

        // ── Y probe ──────────────────────────────────────────────────────
        // Move overshoot in opposite direction to Y wall
        snprintf(buf, sizeof(buf), "G0 Y%.3f F1000", -yDir * over);
        send_line(buf);
        // Two-pass probe toward Y wall
        probeSeekFine("Y", yDir * (over + 20.0f), seekF, rate);
        send_line("G90");
        float yWCS = yDir > 0 ? -edgeOfs : +edgeOfs;
        snprintf(buf, sizeof(buf), "G10 L20 P%d Y%.3f", pNum, yWCS);
        send_line(buf);
        send_line("G91");
        snprintf(buf, sizeof(buf), "G0 Y%.3f F1000", -yDir * retXY);
        send_line(buf);

        // Raise back to safe Z
        snprintf(buf, sizeof(buf), "G0 Z%.3f F500", depth);
        send_line(buf);
        send_line("G90");
    }
}

// ── Draw helpers ─────────────────────────────────────────────────────────────

static const char* cornerLabels[] = { "Bot-Left", "Bot-Right", "Top-Left", "Top-Right" };

// Top-down diagram of corner probing: a workpiece corner with arrows probing the
// X and Y edges, and a dot marking the found corner.
static void drawCornerDiagram() {
    display.fillRect(20, 182, 38, 22, PROBE_C_DIMBLUE);   // workpiece (corner top-right)
    // Probe body/stem + stylus descending onto the corner
    display.fillRoundRect(53, 141, 10, 12, 2, PROBE_C_LBLUE);
    display.drawLine(58, 152, 58, 180, PROBE_C_YELLOW);
    display.fillCircle(58, 182, 2, PROBE_C_YELLOW);       // probe ball at the corner
    // X probe → toward the right edge
    display.drawLine(80, 192, 61, 192, PROBE_C_GREEN);
    display.drawLine(61, 192, 65, 189, PROBE_C_GREEN);
    display.drawLine(61, 192, 65, 195, PROBE_C_GREEN);
    // Y probe → toward the top edge
    display.drawLine(40, 164, 40, 177, PROBE_C_GREEN);
    display.drawLine(40, 177, 37, 173, PROBE_C_GREEN);
    display.drawLine(40, 177, 43, 173, PROBE_C_GREEN);
}

// Layout (boss style):
//   drawTitle    y=0   h=35
//   Pos panel    y=38  h=28
//   Combined     y=70  h=146  (left: sequence+diagram / right: settings)
//   Warn         y=220 h=14
//   Back         y=239 h=38
//   Btn pair     y=280 h=38

void drawProbeCornerScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("XYZ CORNER");
    probeDrawPosPanel(38);

    display.fillRoundRect(5, 70, 230, 146, 4, PROBE_BG_PANEL);

    // Left column: sequence + diagram
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 73);
    display.print("SEQUENCE");
    drawSeqStep( 8, 87,  1, "Touch top->Z0", true);
    drawSeqStep( 8, 105, 2, "Probe X & Y",   false);
    drawSeqStep( 8, 123, 3, "Set X0 Y0 Z0",  false);
    drawCornerDiagram();

    // Right column: settings
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(122, 73);
    display.print("SETTINGS");

    // Corner selector (tap to cycle) — top of the right column
    display.fillRoundRect(122, 84, 111, 27, 4, PROBE_BG_PANEL);
    display.drawRoundRect(122, 84, 111, 27, 4, PROBE_C_YELLOW);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(127, 87);
    display.print("CORNER");
    display.setTextColor(PROBE_C_YELLOW);
    display.setCursor(127, 98);
    display.print(cornerLabels[pendantProbeV2.cornerIdx]);

    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch(122, 113, 111, 27, "Probe depth", pendantProbeV2.cornerDepth, "mm", PROBE_C_RED,  fo==0, 3);
    probeDrawKVTouch(122, 142, 111, 27, "Overshoot",   pendantProbeV2.cornerOver,  "mm", PROBE_C_BLUE, fo==1, 3);
    probeDrawKVTouch(122, 171, 111, 27, "XY retract",  pendantProbeV2.cornerRetXY, "mm", PROBE_C_BLUE, fo==2, 3);

    // Result line — what the probe will set
    {
        const char* s = "Sets X0 Y0 Z0";
        display.setTextSize(1);
        display.setTextColor(PROBE_C_GREEN);
        display.setCursor(177 - display.textWidth(s) / 2, 203);
        display.print(s);
    }

    probeDrawWarn(220, "! Position probe above corner edge");

    // Bottom-nav row: Back (left) | work-area selector (right)
    drawButton(5, 239, 112, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
    probeDrawWorkAreaButton(123, 239, 112, 38);

    drawButton(  5, 280, 112, 38, "Main Menu", PROBE_BTN_BLUE,  COLOR_WHITE, 2);
    drawButton(123, 280, 112, 38, "Probe",     PROBE_BTN_GREEN, COLOR_WHITE, 2);

    if (pendantProbeV2.confirmActive)
        probeDrawConfirmOverlay("XYZ CORNER");
}

void updateProbeCornerScreen() {
    if (currentPendantScreen != PSCREEN_PROBE_CORNER) return;
    probeDrawPosPanel(38);
}

void handleProbeCornerTouch(int x, int y) {
    if (pendantProbeV2.confirmActive) {
        if (isTouchInBounds(x, y, 28, 175, 78, 32)) {
            pendantProbeV2.confirmActive = false;
            drawProbeCornerScreen();
        } else if (isTouchInBounds(x, y, 114, 175, 98, 32)) {
            pendantProbeV2.confirmActive = false;
            runProbeCorner();
            currentPendantScreen = PSCREEN_STATUS;
        }
        return;
    }

    // CORNER cycle button (top of right column)
    if (isTouchInBounds(x, y, 122, 84, 111, 27)) {
        pendantProbeV2.cornerIdx = (pendantProbeV2.cornerIdx + 1) % 4;
        drawProbeCornerScreen();
        return;
    }

    // KV fields (right column)
    bool redraw = false;
    if (isTouchInBounds(x, y, 122, 113, 111, 27)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField==0)?-1:0; redraw=true; }
    if (isTouchInBounds(x, y, 122, 142, 111, 27)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField==1)?-1:1; redraw=true; }
    if (isTouchInBounds(x, y, 122, 171, 111, 27)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField==2)?-1:2; redraw=true; }
    if (redraw) { drawProbeCornerScreen(); return; }

    // Back → config hub
    if (isTouchInBounds(x, y, 5, 239, 112, 38)) {
        pendantProbeV2.returnScreen  = PSCREEN_PROBE_CORNER;
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }
    // Work-area selector → cycle G54..G57
    if (isTouchInBounds(x, y, 123, 239, 112, 38)) {
        probeCycleWorkArea();
        drawProbeCornerScreen();
        return;
    }

    // Main Menu
    if (isTouchInBounds(x, y, 5, 280, 112, 38)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
        return;
    }
    // Probe
    if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
        if (!pendantConnected) { probeDrawWarn(220, "! Not connected", true); return; }
        pendantProbeV2.confirmActive = true;
        drawProbeCornerScreen();
        return;
    }
}
