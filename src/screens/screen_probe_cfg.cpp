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
// Compact form so it fits the space left below the deflection-cal panel (y188+).
static void drawProbe3DGraphic() {
    const int cx = kGfxCX;
    display.fillRoundRect(5, 188, 230, 82, 4, PROBE_BG_PANEL);
    display.fillRect(cx - 7, 194, 14, 8, PROBE_C_DIMBLUE);            // collet/shank
    display.fillRoundRect(cx - 17, 202, 34, 18, 4, COLOR_GRAY_TEXT); // probe body
    display.drawFastVLine(cx - 1, 220, 24, PROBE_C_LBLUE);           // stylus (3px)
    display.drawFastVLine(cx,     220, 24, PROBE_C_LBLUE);
    display.drawFastVLine(cx + 1, 220, 24, PROBE_C_LBLUE);
    display.fillCircle(cx, 247, 5, PROBE_C_RED);                     // ruby ball
    display.drawFastHLine(cx - 50, 255, 100, COLOR_GRAY_TEXT);       // work surface
    for (int i = 0; i < 7; i++)
        display.drawLine(cx - 46 + i * 14, 255, cx - 52 + i * 14, 261, PROBE_C_DIMBLUE);
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
//   0 = ball dia   1 = deflection   2 = gauge width (deflection-cal reference)

void enterProbeCfg3D() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
    pendantProbeV2.focusedField   = 0;   // default focus on ball dia
    pendantProbeV2.confirmActive  = false;
    pendantProbeV2.dialAccelCount = 0;
    pendantProbeV2.calState       = 0;   // never resume a calibration across visits
    g_calCapture                  = false;
}

void exitProbeCfg3D() { g_calCapture = false; }

// ── Deflection-calibration overlays ──────────────────────────────────────────
// buttons: 0 = none (busy), 1 = single OK (dismiss), 2 = CANCEL + affirmative
static void drawCalOverlay(const char* l1, const char* l2, uint16_t l2col, int buttons,
                           const char* okLabel = "APPLY") {
    display.fillRoundRect(20, 100, 200, 120, 8, PROBE_BG_PANEL);
    display.drawRoundRect(20, 100, 200, 120, 8, PROBE_C_YELLOW);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_YELLOW);
    int16_t w1 = display.textWidth(l1);
    display.setCursor(120 - w1 / 2, 118);
    display.print(l1);
    display.setTextSize(2);
    display.setTextColor(l2col);
    int16_t w2 = display.textWidth(l2);
    display.setCursor(120 - w2 / 2, 140);   // centre the size-2 line
    display.print(l2);
    display.setTextSize(2);
    display.setTextColor(COLOR_WHITE);
    if (buttons == 2) {
        display.fillRoundRect(28,  175, 78, 32, 5, COLOR_BUTTON_GRAY);   // CANCEL
        display.fillRoundRect(114, 175, 98, 32, 5, PROBE_BTN_GREEN);     // affirmative
        display.setCursor(36, 183);  display.print("CANCEL");
        int16_t aw = display.textWidth(okLabel);
        display.setCursor(114 + (98 - aw) / 2, 183);  display.print(okLabel);
    } else if (buttons == 1) {
        display.fillRoundRect(71, 175, 98, 32, 5, PROBE_BTN_BLUE);       // OK / dismiss
        int16_t ow = display.textWidth("OK");
        display.setCursor(71 + (98 - ow) / 2, 183);  display.print("OK");
    }
}

// Layout (240×320):
//   drawTitle       y=0   h=35
//   Hardware panel  y=38  h=26
//   Stylus panel    y=67  h=55   (Ball dia · Deflection)
//   Cal panel       y=128 h=142  (Gauge width · Calibrate + hint)
//   button pair     y=280 h=40

void drawProbeCfg3DScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("PROBE CONFIG");

    // ── Hardware panel (static info) ──────────────────────────────────────
    display.fillRoundRect(5, 38, 230, 26, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 41);
    display.print("PROBE HARDWARE");
    display.setTextColor(PROBE_C_GREEN);
    display.setCursor(10, 53);
    display.print("3D Touch Probe");

    // ── Stylus panel (Ball dia · Deflection) ─────────────────────────────
    display.fillRoundRect(5, 67, 230, 55, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 70);
    display.print("STYLUS");

    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch( 7,  79, 112, 40, "Ball dia.",  pendantProbeV2.ballDia,    "mm", PROBE_C_BLUE, fo==0, 3);
    probeDrawKVTouch(122,  79, 111, 40, "Deflection", pendantProbeV2.deflection, "mm", PROBE_C_BLUE, fo==1, 3);

    // ── Deflection-calibration panel (field spaced like the Stylus panel) ─
    display.fillRoundRect(5, 128, 230, 55, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 131);
    display.print("DEFLECTION CAL");

    probeDrawKVTouch(7, 140, 112, 40, "Gauge width", pendantProbeV2.calGaugeWidth, "mm", PROBE_C_BLUE, fo==2, 1);
    drawButton(122, 140, 111, 40, "Calibrate", PROBE_BTN_BLUE, COLOR_WHITE, 2);

    // ── Illustration ──────────────────────────────────────────────────────
    drawProbe3DGraphic();

    // ── Button pair ───────────────────────────────────────────────────────
    drawButton(  5, 280, 112, 40, "Back", PROBE_BTN_BLUE,   COLOR_WHITE, 2);
    drawButton(123, 280, 112, 40, "Save", COLOR_DARK_GREEN, COLOR_GREEN, 2);

    // ── Calibration overlays ──────────────────────────────────────────────
    char vbuf[24];
    switch (pendantProbeV2.calState) {
        case 4:
            drawCalOverlay("START CALIBRATION?", "PROBE MOVES", PROBE_C_YELLOW, 2, "START");
            break;
        case 1:
            drawCalOverlay("CALIBRATING...", "PROBING", PROBE_C_YELLOW, 0);
            break;
        case 2:
            snprintf(vbuf, sizeof(vbuf), "%.3f mm", pendantProbeV2.calResult);
            drawCalOverlay("MEASURED DEFLECTION", vbuf, PROBE_C_GREEN, 2);
            break;
        case 3:
            drawCalOverlay("CALIBRATION FAILED", "CHECK SETUP", PROBE_C_RED, 1);
            break;
    }
}

// ── Deflection-calibration probe program ─────────────────────────────────────
// Side-effect-free: probes the gauge top (for a safe lateral depth) then the two
// X faces from OUTSIDE (two-pass each) — never touches WCS (no G10).  The two
// face positions come back via show_probe()/g_calProbeXe4; updateProbeCfg3DScreen
// reads them.  Start with the probe roughly centred over the gauge, above it.
static void runProbeCalibration() {
    if (!pendantConnected) return;

    float W     = pendantProbeV2.calGaugeWidth;
    float seekF = pendantProbeV2.seekRate;
    float fineF = pendantProbeV2.probeRate;
    float maxZ  = pendantProbeV2.maxZTravel;
    float retZ  = pendantProbeV2.retractDist;
    const float CLEAR = 10.0f;                 // move this far past a face before lowering
    const float DEPTH = 5.0f;                  // probe this far below the gauge top
    float toClear = W / 2.0f + CLEAR;          // centre → clear position (past a face)
    float plunge  = retZ + DEPTH;              // top+retZ → top-DEPTH

    // Arm capture (show_probe records each [PRB:] machine X into g_calProbeXe4).
    g_calCount = 0; g_calAllOk = true; g_calCapture = true;

    char buf[80];
    send_line("G21 G90");
    send_line("#<sx> = #5420");                // save start (work coords) to return between faces
    send_line("#<sy> = #5421");

    // ── Z: find the gauge top, then lift to a safe height above it ──
    send_line("G91");
    snprintf(buf, sizeof(buf), "G38.2 Z-%.3f F%.0f", maxZ, fineF); send_line(buf);   // PRB[0] (Z)
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", retZ);            send_line(buf);    // to top+retZ
    send_line("G90");

    // ── LEFT face → x1: clear left (safe Z), lower beside, two-pass probe +X ──
    send_line("G91");
    snprintf(buf, sizeof(buf), "G0 X-%.3f F1000", toClear);       send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", plunge);         send_line(buf);
    snprintf(buf, sizeof(buf), "G38.2 X%.3f F%.0f", CLEAR + 3.0f, seekF); send_line(buf); // PRB[1] fast
    send_line("G0 X-1.5 F1000");
    snprintf(buf, sizeof(buf), "G38.2 X2.5 F%.0f", fineF);        send_line(buf);    // PRB[2] slow = x1
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", plunge);          send_line(buf);    // lift to top+retZ
    send_line("G90 G0 X#<sx> Y#<sy> F1000");                                          // return to centre

    // ── RIGHT face → x2: clear right, lower, two-pass probe -X ──
    send_line("G91");
    snprintf(buf, sizeof(buf), "G0 X%.3f F1000", toClear);        send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", plunge);         send_line(buf);
    snprintf(buf, sizeof(buf), "G38.2 X-%.3f F%.0f", CLEAR + 3.0f, seekF); send_line(buf); // PRB[3] fast
    send_line("G0 X1.5 F1000");
    snprintf(buf, sizeof(buf), "G38.2 X-2.5 F%.0f", fineF);       send_line(buf);    // PRB[4] slow = x2
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", plunge);          send_line(buf);
    send_line("G90 G0 X#<sx> Y#<sy> F1000");
}

// Poll the calibration flow: fired from the periodic update while calState==1.
void updateProbeCfg3DScreen() {
    if (currentPendantScreen != PSCREEN_PROBE_CFG_3D) return;
    if (pendantProbeV2.calState != 1) return;

    if (!g_calAllOk) {                                      // a probe missed contact
        g_calCapture = false;
        pendantProbeV2.calState = 3;
        drawProbeCfg3DScreen();
        return;
    }
    if (millis() - pendantProbeV2.calStartMs > 90000UL) {   // safety timeout
        g_calCapture = false;
        pendantProbeV2.calState = 3;
        drawProbeCfg3DScreen();
        return;
    }
    if (g_calCount >= 5) {                                  // Z + 2 two-pass faces done
        g_calCapture = false;
        int c = g_calCount;
        float x1u = g_calProbeXe4[c - 3] / 10000.0f;        // left slow re-probe
        float x2u = g_calProbeXe4[c - 1] / 10000.0f;        // right slow re-probe
        float x1  = pendantMachine.inInches ? x1u * 25.4f : x1u;   // → mm
        float x2  = pendantMachine.inInches ? x2u * 25.4f : x2u;
        // Outer span exceeds (width + ballDia) by 2×deflection.  Signed: a probe
        // that under-reads yields a negative deflection (handled by probeTipOffset3D).
        float defl = ((x2 - x1) - pendantProbeV2.calGaugeWidth - pendantProbeV2.ballDia) / 2.0f;
        pendantProbeV2.calResult = constrain(defl, -1.0f, 1.0f);
        pendantProbeV2.calState  = 2;                       // → result/confirm overlay
        drawProbeCfg3DScreen();
    }
}

void handleProbeCfg3DTouch(int x, int y) {
    // ── Calibration overlay is modal ──────────────────────────────────────
    if (pendantProbeV2.calState == 4) {                    // confirm: CANCEL | START
        if (isTouchInBounds(x, y, 28, 175, 78, 32)) {      // CANCEL
            pendantProbeV2.calState = 0; drawProbeCfg3DScreen();
        } else if (isTouchInBounds(x, y, 114, 175, 98, 32)) {  // START
            pendantProbeV2.calState   = 1;
            pendantProbeV2.calStartMs = millis();
            drawProbeCfg3DScreen();       // show the CALIBRATING overlay first
            runProbeCalibration();
        }
        return;
    }
    if (pendantProbeV2.calState == 2) {                    // result: CANCEL | APPLY
        if (isTouchInBounds(x, y, 28, 175, 78, 32)) {      // CANCEL
            pendantProbeV2.calState = 0; drawProbeCfg3DScreen();
        } else if (isTouchInBounds(x, y, 114, 175, 98, 32)) {  // APPLY
            pendantProbeV2.deflection = pendantProbeV2.calResult;
            saveProbeSettings();
            pendantProbeV2.calState = 0; drawProbeCfg3DScreen();
        }
        return;
    }
    if (pendantProbeV2.calState == 3) {                    // error: OK dismiss
        if (isTouchInBounds(x, y, 71, 175, 98, 32)) { pendantProbeV2.calState = 0; drawProbeCfg3DScreen(); }
        return;
    }
    if (pendantProbeV2.calState == 1) return;              // busy — ignore taps

    // ── KV fields — tap to focus ──────────────────────────────────────────
    bool redraw = false;
    if (isTouchInBounds(x, y,  7,  79, 112, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 0) ? -1 : 0; redraw = true; }
    if (isTouchInBounds(x, y, 122,  79, 111, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 1) ? -1 : 1; redraw = true; }
    if (isTouchInBounds(x, y,  7, 140, 112, 40)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 2) ? -1 : 2; redraw = true; }
    if (redraw) { drawProbeCfg3DScreen(); return; }

    // ── Calibrate button → confirm first (motion safety) ──────────────────
    if (isTouchInBounds(x, y, 122, 140, 111, 40)) {
        if (!pendantConnected) { pendantProbeV2.calState = 3; drawProbeCfg3DScreen(); return; }
        pendantProbeV2.calState = 4;     // ask before any motion
        drawProbeCfg3DScreen();
        return;
    }

    // Back — return to the hub without saving
    if (isTouchInBounds(x, y, 5, 280, 112, 40)) {
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }
    // Save — persist + return to the hub
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
    display.setTextColor(PROBE_C_GREEN);
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
    probeDrawKVTouch(7, 79, 112, 40, "Thickness", pendantProbeV2.plateThick, "mm", PROBE_C_BLUE, fo==0, 3);
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
