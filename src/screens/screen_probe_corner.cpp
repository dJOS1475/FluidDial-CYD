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
    float rate    = pendantProbeV2.probeRate;
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
    int   aIdx    = pendantProbeV2.axesIdx;     // 0=XYZ  1=XY   2=Z

    // Direction signs: X probe direction (+1 or -1), Y probe direction
    // Bot-Left:  probe toward +X (+1), toward +Y (+1) — we approach from outside (left/below)
    // Bot-Right: probe toward -X (-1), toward +Y (+1)
    // Top-Left:  probe toward +X (+1), toward -Y (-1)
    // Top-Right: probe toward -X (-1), toward -Y (-1)
    int xDir = (cIdx == 0 || cIdx == 2) ? +1 : -1;
    int yDir = (cIdx == 0 || cIdx == 1) ? +1 : -1;

    char buf[80];
    bool doXY = (aIdx == 0 || aIdx == 1);
    bool doZ  = (aIdx == 0 || aIdx == 2);

    // ── Z probe (surface, via plate) ─────────────────────────────────────
    if (doZ) {
        send_line("G91 G21");
        snprintf(buf, sizeof(buf), "G38.2 Z-%.3f F%.0f", maxZ, rate);
        send_line(buf);
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
        // Probe toward X wall
        snprintf(buf, sizeof(buf), "G38.2 X%.3f F%.0f", xDir * (over + 20.0f), rate);
        send_line(buf);
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
        snprintf(buf, sizeof(buf), "G38.2 Y%.3f F%.0f", yDir * (over + 20.0f), rate);
        send_line(buf);
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
static const char* axesLabels[]   = { "X+Y+Z", "X+Y", "Z" };

static void drawCyclePair() {
    // Panel
    display.fillRoundRect(5, 69, 230, 46, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 72);
    display.print("TAP TO CYCLE");

    // CORNER button (orange active)
    display.fillRoundRect(7, 81, 110, 30, 4, PROBE_BG_PANEL);
    display.drawRoundRect(7, 81, 110, 30, 4, PROBE_C_YELLOW);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(12, 84);
    display.print("CORNER");
    display.setTextSize(1);
    display.setTextColor(PROBE_C_YELLOW);
    display.setCursor(12, 95);
    display.print(cornerLabels[pendantProbeV2.cornerIdx]);

    // AXES button (green active)
    display.fillRoundRect(120, 81, 113, 30, 4, PROBE_BG_PANEL);
    display.drawRoundRect(120, 81, 113, 30, 4, COLOR_GREEN);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(125, 84);
    display.print("AXES");
    display.setTextSize(1);
    display.setTextColor(PROBE_C_GREEN);
    display.setCursor(125, 95);
    display.print(axesLabels[pendantProbeV2.axesIdx]);
}

static void drawCornerSpecificPanel() {
    display.fillRoundRect(5, 118, 230, 80, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 121);
    display.print("CORNER - SPECIFIC");

    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch( 7, 130, 112, 30, "Probe depth", pendantProbeV2.cornerDepth, "mm", PROBE_C_RED,  fo==0, 3);
    probeDrawKVTouch(122, 130, 111, 30, "Overshoot",  pendantProbeV2.cornerOver,  "mm", PROBE_C_BLUE, fo==1, 3);
    probeDrawKVTouch( 7, 163, 112, 30, "XY retract",  pendantProbeV2.cornerRetXY, "mm", PROBE_C_BLUE, fo==2, 3);

    // Static "Sets" cell
    display.fillRoundRect(122, 163, 111, 30, 4, PROBE_BG_SCREEN);
    display.drawRoundRect(122, 163, 111, 30, 4, PROBE_C_DIMBLUE);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(127, 166);
    display.print("Sets");
    display.setTextColor(PROBE_C_BLUE);
    const char* axLbl = (pendantProbeV2.axesIdx == 0) ? "XYZ0"
                       : (pendantProbeV2.axesIdx == 1) ? "XY0" : "Z0";
    char setsBuf[20];
    snprintf(setsBuf, sizeof(setsBuf), "%s %s", pendantProbing.selectedCoordSystem.c_str(), axLbl);
    display.setCursor(127, 178);
    display.print(setsBuf);
}

// Layout:
//   drawTitle    y=0   h=35
//   Pos panel    y=38  h=28
//   Cycle panel  y=69  h=46
//   Specific     y=118 h=80  (4 KV touch + sets cell)
//   Warn         y=205 h=14
//   Back         y=239 h=38
//   Btn pair     y=280 h=38

void drawProbeCornerScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("XYZ CORNER");
    probeDrawPosPanel(38);
    drawCyclePair();
    drawCornerSpecificPanel();

    // (Redundant "selected field" bar removed — the focused KV field already
    //  shows its value live, matching the config screens.)
    probeDrawWarn(205, "! Position probe above corner edge");

    // Back — same location/style as the Z Surface screen
    drawButton(5, 239, 230, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);

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

    // CORNER cycle button
    if (isTouchInBounds(x, y, 7, 81, 110, 30)) {
        pendantProbeV2.cornerIdx = (pendantProbeV2.cornerIdx + 1) % 4;
        drawCyclePair();
        drawCornerSpecificPanel();
        return;
    }
    // AXES cycle button
    if (isTouchInBounds(x, y, 120, 81, 113, 30)) {
        pendantProbeV2.axesIdx = (pendantProbeV2.axesIdx + 1) % 3;
        drawCyclePair();
        drawCornerSpecificPanel();
        return;
    }

    // KV fields
    bool redraw = false;
    if (isTouchInBounds(x, y,  7, 130, 112, 30)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField==0)?-1:0; redraw=true; }
    if (isTouchInBounds(x, y, 122, 130, 111, 30)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField==1)?-1:1; redraw=true; }
    if (isTouchInBounds(x, y,  7, 163, 112, 30))  { pendantProbeV2.focusedField = (pendantProbeV2.focusedField==2)?-1:2; redraw=true; }
    if (redraw) { drawProbeCornerScreen(); return; }

    // Back → config hub
    if (isTouchInBounds(x, y, 5, 239, 230, 38)) {
        pendantProbeV2.returnScreen  = PSCREEN_PROBE_CORNER;
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }

    // Main Menu
    if (isTouchInBounds(x, y, 5, 280, 112, 38)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
        return;
    }
    // Probe
    if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
        if (!pendantConnected) { probeDrawWarn(205, "! Not connected", true); return; }
        pendantProbeV2.confirmActive = true;
        drawProbeCornerScreen();
        return;
    }
}
