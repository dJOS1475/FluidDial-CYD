/*
 * screen_probe_cfg.cpp  —  Probe Config screens (title: "Probe Config")
 *
 * CFG_3D  (3D Touch Probe):   Ball dia · Stylus length · Deflection · Pre-travel
 * CFG_PLATE (touch plate), field set chosen by the selected plate type:
 *   Z-Height Touch Plate → Thickness
 *   XYZ Touch Plate      → Thickness · Width · XY offset X · XY offset Y
 *
 * Both share:
 *   Setup → returns to the probe hub without saving
 *   Save  → writes NVS, returns to the hub
 * The selected probe type is shown in the panel, so the title stays generic.
 */

#include "pendant_shared.h"
#include "screen_probe.h"
#include "screen_probe_cfg.h"

// ══════════════════════════════════════════════════════════════════════════════
//  Probe-type illustrations
// ──────────────────────────────────────────────────────────────────────────────
// Simple schematic drawings so each config screen visually conveys its probe
// type.  Each fills the free area below the field panels with a bordered card.
// Only primitives shared with the JS simulator are used (rects/lines/circles —
// no triangles), so firmware and simulator stay pixel-identical.

static const int kGfxCX = 120;   // horizontal centre of all illustrations

// 3D touch probe: spindle body + stylus + ruby ball touching a work surface.
static void drawProbe3DGraphic() {
    const int cx = kGfxCX;
    display.fillRoundRect(5, 170, 230, 100, 4, PROBE_BG_PANEL);
    display.fillRect(cx - 7, 182, 14, 10, PROBE_C_DIMBLUE);            // collet/shank
    display.fillRoundRect(cx - 17, 192, 34, 22, 4, COLOR_GRAY_TEXT);  // probe body
    display.drawFastVLine(cx - 1, 214, 30, PROBE_C_LBLUE);            // stylus (3px)
    display.drawFastVLine(cx,     214, 30, PROBE_C_LBLUE);
    display.drawFastVLine(cx + 1, 214, 30, PROBE_C_LBLUE);
    display.fillCircle(cx, 248, 5, PROBE_C_RED);                      // ruby ball
    display.drawFastHLine(cx - 50, 256, 100, COLOR_GRAY_TEXT);        // work surface
    for (int i = 0; i < 7; i++)
        display.drawLine(cx - 46 + i * 14, 256, cx - 52 + i * 14, 262, PROBE_C_DIMBLUE);
}

// Z-height touch plate: tool descending onto a flat plate sitting on the stock.
static void drawPlateZGraphic() {
    const int cx = kGfxCX;
    display.fillRoundRect(5, 128, 230, 142, 4, PROBE_BG_PANEL);
    display.fillRect(cx - 45, 206, 90, 32, COLOR_BUTTON_GRAY);  // workpiece
    display.fillRect(cx - 55, 194, 110, 12, PROBE_C_BLUE);      // touch plate (top)
    display.fillRect(cx - 7, 164, 14, 30, COLOR_GRAY_TEXT);     // tool
}

// XYZ touch plate: L-shaped corner block wrapping the top-left corner of stock.
static void drawPlateXYZGraphic() {
    const int cx = kGfxCX;
    display.fillRoundRect(5, 170, 230, 100, 4, PROBE_BG_PANEL);
    display.fillRect(cx - 28, 213, 90, 43, COLOR_BUTTON_GRAY);  // workpiece block
    display.fillRect(cx - 40, 203, 78, 10, PROBE_C_BLUE);       // plate — top arm
    display.fillRect(cx - 40, 203, 10, 53, PROBE_C_BLUE);       // plate — left arm
    display.fillRect(cx - 10, 180, 12, 23, COLOR_GRAY_TEXT);    // tool
}

// ══════════════════════════════════════════════════════════════════════════════
//  SCR0a — 3D PROBE CONFIG
// ══════════════════════════════════════════════════════════════════════════════

// focusedField mapping for SCR0a:
//   0 = ball dia   1 = stylus len   2 = deflection   3 = pre-travel

void enterProbeCfg3D() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
    pendantProbeV2.focusedField   = 0;   // default focus on ball dia
    pendantProbeV2.confirmActive  = false;
    pendantProbeV2.dialAccelCount = 0;
}

void exitProbeCfg3D() {}

// Layout (240×320):
//   drawTitle      y=0   h=35
//   Hardware panel y=38  h=26   (static label: "3D Touch Probe")
//   Stylus panel   y=67  h=98   (4 KV-touch fields in 2×2, h=40 each)
//   sel bar        y=168 h=20
//   button pair    y=280 h=40

void drawProbeCfg3DScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("PROBE CONFIG");

    // ── Hardware panel (static info) ──────────────────────────────────────
    display.fillRoundRect(5, 38, 230, 26, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 41);
    display.print("PROBE HARDWARE");
    display.setTextSize(1);
    display.setTextColor(PROBE_C_DIMBLUE);
    display.setCursor(10, 53);
    display.print("3D Touch Probe");

    // ── Stylus panel (4 KV-touch fields) ─────────────────────────────────
    display.fillRoundRect(5, 67, 230, 98, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 70);
    display.print("STYLUS");

    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch( 7,  79, 112, 40, "Ball dia.",      pendantProbeV2.ballDia,    "mm", PROBE_C_BLUE,    fo==0, 3);
    probeDrawKVTouch(122,  79, 111, 40, "Stylus length", pendantProbeV2.stylusLen,  "mm", PROBE_C_DIMBLUE, fo==1, 3);
    probeDrawKVTouch( 7, 122, 112, 40, "Deflection",    pendantProbeV2.deflection, "mm", PROBE_C_BLUE,    fo==2, 3);
    probeDrawKVTouch(122, 122, 111, 40, "Pre-travel",   pendantProbeV2.preTravel,  "mm", PROBE_C_BLUE,    fo==3, 3);

    // ── Illustration ──────────────────────────────────────────────────────
    drawProbe3DGraphic();

    // ── Button pair ───────────────────────────────────────────────────────
    drawButton(  5, 280, 112, 40, "Back", PROBE_BTN_BLUE,   COLOR_WHITE, 2);
    drawButton(123, 280, 112, 40, "Save", COLOR_DARK_GREEN, COLOR_GREEN, 2);
}

void handleProbeCfg3DTouch(int x, int y) {
    // KV fields — tap to focus
    bool redraw = false;
    if (isTouchInBounds(x, y,  7,  79, 112, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 0) ? -1 : 0; redraw = true; }
    if (isTouchInBounds(x, y, 122,  79, 111, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 1) ? -1 : 1; redraw = true; }
    if (isTouchInBounds(x, y,  7, 122, 112, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 2) ? -1 : 2; redraw = true; }
    if (isTouchInBounds(x, y, 122, 122, 111, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 3) ? -1 : 3; redraw = true; }
    if (redraw) { drawProbeCfg3DScreen(); return; }

    // Setup — return to SCR0 without saving
    if (isTouchInBounds(x, y, 5, 280, 112, 40)) {
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }
    // Save — persist + return to SCR0
    if (isTouchInBounds(x, y, 122, 280, 113, 40)) {
        saveProbeSettings();
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  SCR0b — TOUCH PLATE CONFIG  (Z-Height plate OR XYZ plate)
// ══════════════════════════════════════════════════════════════════════════════

// Shared by both plate types.  Field set depends on the probe type:
//   Z-Height Touch Plate → Thickness only
//   XYZ Touch Plate      → Thickness, Width, XY offset X, XY offset Y
// focusedField: 0 = thickness  1 = width  2 = XY offset X  3 = XY offset Y

void enterProbeCfgPlate() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
    pendantProbeV2.focusedField   = 0;   // thickness — valid for both plate types
    pendantProbeV2.confirmActive  = false;
    pendantProbeV2.dialAccelCount = 0;
}

void exitProbeCfgPlate() {}

// Layout (240×320):
//   drawTitle      y=0   h=35
//   Type panel     y=38  h=26   (static label: plate type name)
//   Dimensions     y=67  h=55/98  (1 field for Z-plate, 4 for XYZ plate)
//   warn banner    y=...
//   button pair    y=280 h=40

void drawProbeCfgPlateScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("PROBE CONFIG");

    bool xyz = (pendantProbeV2.probeTypeIdx == PROBE_TYPE_XYZPLATE);

    // ── Type panel (static info — the probe type is shown here, so the title
    //    can stay generic "Probe Config") ──────────────────────────────────
    display.fillRoundRect(5, 38, 230, 26, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 41);
    display.print("PROBE TYPE");
    display.setTextColor(PROBE_C_DIMBLUE);
    display.setCursor(10, 53);
    display.print(xyz ? "XYZ Touch Plate" : "Z-Height Touch Plate");

    // ── Dimensions panel ──────────────────────────────────────────────────
    int panelH = xyz ? 98 : 55;
    display.fillRoundRect(5, 67, 230, panelH, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 70);
    display.print(xyz ? "PLATE DIMENSIONS" : "PLATE THICKNESS");

    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch(7, 79, 112, 40, "Thickness", pendantProbeV2.plateThick, "mm", PROBE_C_YELLOW, fo==0, 3);
    if (xyz) {
        probeDrawKVTouch(122,  79, 111, 40, "Width",       pendantProbeV2.plateWidth, "mm", PROBE_C_DIMBLUE, fo==1, 3);
        probeDrawKVTouch(  7, 122, 112, 40, "XY offset X", pendantProbeV2.plateOffX,  "mm", PROBE_C_BLUE,    fo==2, 3);
        probeDrawKVTouch(122, 122, 111, 40, "XY offset Y", pendantProbeV2.plateOffY,  "mm", PROBE_C_BLUE,    fo==3, 3);
    }

    // ── Illustration (replaces the old clip warning) ──────────────────────
    if (xyz) drawPlateXYZGraphic();
    else     drawPlateZGraphic();

    // ── Button pair ───────────────────────────────────────────────────────
    drawButton(  5, 280, 112, 40, "Back", PROBE_BTN_BLUE,   COLOR_WHITE, 2);
    drawButton(123, 280, 112, 40, "Save", COLOR_DARK_GREEN, COLOR_GREEN, 2);
}

void handleProbeCfgPlateTouch(int x, int y) {
    bool xyz = (pendantProbeV2.probeTypeIdx == PROBE_TYPE_XYZPLATE);

    // KV fields — only the ones drawn for this plate type are tappable.
    bool redraw = false;
    if (isTouchInBounds(x, y, 7, 79, 112, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 0) ? -1 : 0; redraw = true; }
    if (xyz) {
        if (isTouchInBounds(x, y, 122,  79, 111, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 1) ? -1 : 1; redraw = true; }
        if (isTouchInBounds(x, y,   7, 122, 112, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 2) ? -1 : 2; redraw = true; }
        if (isTouchInBounds(x, y, 122, 122, 111, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 3) ? -1 : 3; redraw = true; }
    }
    if (redraw) { drawProbeCfgPlateScreen(); return; }

    // Setup — return to SCR0 without saving
    if (isTouchInBounds(x, y, 5, 280, 112, 40)) {
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }
    // Save — persist + return to SCR0
    if (isTouchInBounds(x, y, 122, 280, 113, 40)) {
        saveProbeSettings();
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }
}
