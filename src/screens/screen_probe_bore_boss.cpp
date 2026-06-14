/*
 * screen_probe_bore_boss.cpp  —  SCR3a: Bore / SCR3b: Boss
 *
 * BORE (inside circle):
 *   Probe starts at bore centre above the part.
 *   1. Lower to probe depth.
 *   2. Probe +X, -X, +Y, -Y walls using G38.2.
 *      Named GRBL params capture each probe position.
 *   3. Move to computed centre (average of opposing probes).
 *   4. Touch top surface → set Z0.
 *   5. G10 L20 sets XYZ origin at bore centre.
 *   6. Retract.
 *
 * BOSS (outside circle):
 *   Probe starts above boss centre.
 *   1. Probe Z top → set Z0.
 *   2. Retract, move outward to +X clearance position.
 *   3. Probe –X wall, retract, repeat for –X, +Y, –Y positions.
 *   4. Move to computed centre, G10 L20 sets XY origin.
 *   5. Retract.
 *
 * focusedField for bore:  0=boreDia  1=boreDepth  2=boreOffset  3=borePasses
 * focusedField for boss:  0=bossDia  1=bossDepth  2=bossClear   3=bossPasses
 */

#include "pendant_shared.h"
#include "screen_probe.h"
#include "screen_probe_bore_boss.h"

// ══════════════════════════════════════════════════════════════════════════════
//  SCR3a — BORE PROBE
// ══════════════════════════════════════════════════════════════════════════════

void enterProbeBore() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
    pendantProbeV2.focusedField   = 0;
    pendantProbeV2.confirmActive  = false;
    pendantProbeV2.dialAccelCount = 0;
}

void exitProbeBore() {}

// ── G-code: bore centre finding ──────────────────────────────────────────────
// Strategy: all moves relative (G91) so we don't need to know absolute position.
// +X probe, return past centre, -X probe, return past centre, repeat for Y.
// After 4 probes, move to the average of the X pair and Y pair (requires G90 +
// GRBL named param arithmetic), then probe Z top and set all three WCS axes.

static void runProbeBore() {
    if (!pendantConnected) return;

    int   pNum   = pendantProbing.selectedCoordIndex + 1;
    float rate   = pendantProbeV2.probeRate;
    float depth  = pendantProbeV2.boreDepth;    // positive mm
    float rad    = pendantProbeV2.boreDia / 2.0f;
    float wallOf = pendantProbeV2.boreOffset;
    float retZ   = pendantProbeV2.retractDist;
    float maxZ   = pendantProbeV2.maxZTravel;
    float platZ  = probeIs3D() ? pendantProbeV2.ballDia / 2.0f
                               : pendantProbeV2.plateThick;
    // Approach distance for each wall: nominal radius + wall offset + a small margin
    float approach = rad + wallOf + 3.0f;

    char buf[80];

    // Lower to bore probe depth
    send_line("G91 G21");
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", depth);
    send_line(buf);

    // ── +X probe ──────────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "G38.2 X%.3f F%.0f", approach, rate);
    send_line(buf);
    send_line("#<bx_pos> = #5061");       // save trigger X
    // Retract back past centre to -X side
    snprintf(buf, sizeof(buf), "G0 X-%.3f F1000", approach + wallOf + 3.0f);
    send_line(buf);

    // ── -X probe ──────────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "G38.2 X-%.3f F%.0f", wallOf + 6.0f, rate);
    send_line(buf);
    send_line("#<bx_neg> = #5061");
    // Return to approximate centre
    snprintf(buf, sizeof(buf), "G0 X%.3f F1000", wallOf + 3.0f);
    send_line(buf);

    // ── +Y probe ──────────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "G38.2 Y%.3f F%.0f", approach, rate);
    send_line(buf);
    send_line("#<by_pos> = #5062");
    snprintf(buf, sizeof(buf), "G0 Y-%.3f F1000", approach + wallOf + 3.0f);
    send_line(buf);

    // ── -Y probe ──────────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "G38.2 Y-%.3f F%.0f", wallOf + 6.0f, rate);
    send_line(buf);
    send_line("#<by_neg> = #5062");
    snprintf(buf, sizeof(buf), "G0 Y%.3f F1000", wallOf + 3.0f);
    send_line(buf);

    // Raise back to surface level
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", depth);
    send_line(buf);

    // Move to computed bore centre (absolute machine coords from saved params)
    send_line("G90");
    send_line("G0 X[{#<bx_pos>+#<bx_neg>}/2] Y[{#<by_pos>+#<by_neg>}/2] F1000");

    // Probe Z top of bore
    send_line("G91");
    snprintf(buf, sizeof(buf), "G38.2 Z-%.3f F%.0f", maxZ, rate);
    send_line(buf);
    send_line("G90");
    snprintf(buf, sizeof(buf), "G10 L20 P%d X0 Y0 Z%.3f", pNum, platZ);
    send_line(buf);
    send_line("G91");
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", retZ);
    send_line(buf);
    send_line("G90");
}

// ── Draw helpers (bore) ──────────────────────────────────────────────────────

// Sequence step badge: filled circle with number, text beside it
static void drawSeqStep(int x, int y, int num, const char* txt, bool active) {
    uint16_t bg = active ? PROBE_AMBER    : PROBE_BG_PANEL;
    uint16_t fg = active ? COLOR_WHITE    : PROBE_C_DIMBLUE;
    uint16_t tc = active ? PROBE_C_YELLOW : PROBE_C_DIMBLUE;
    display.fillCircle(x + 6, y + 6, 6, bg);
    display.setTextSize(1);
    display.setTextColor(fg);
    int16_t nw = display.textWidth(String(num).c_str());
    display.setCursor(x + 6 - nw / 2, y + 2);
    display.print(num);
    display.setTextColor(tc);
    display.setCursor(x + 16, y + 2);
    display.print(txt);
}

// Layout SCR3a (240×320):
//   drawTitle     y=0   h=35
//   Pos panel     y=38  h=28
//   Seq+settings  y=70  h=130  (left=sequence, right=4 KV fields)
//   Warn-r        y=204 h=14
//   Sets line     y=224 h=10
//   Back          y=239 h=38
//   Btn pair      y=280 h=38

void drawProbeBoreScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("BORE");
    probeDrawPosPanel(38);

    // ── Combined panel: sequence (left) + 4 KV settings (right) ───────────
    // Taller than before so the right-column KV values (size-2, ~27 px) no
    // longer overflow into the field below — the old 20 px fields overlapped.
    display.fillRoundRect(5, 70, 230, 130, 4, PROBE_BG_PANEL);

    // Left column: sequence steps
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 73);
    display.print("SEQUENCE");
    drawSeqStep( 8, 87,  1, "Lower to depth", true);
    drawSeqStep( 8, 105, 2, "Sweep XY walls", false);
    drawSeqStep( 8, 123, 3, "Touch top->Z0",  false);

    // Right column: 4 KV-touch settings (h=27 so the value isn't clipped)
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(122, 73);
    display.print("SETTINGS");

    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch(122, 84,  111, 27, "Nominal dia.", pendantProbeV2.boreDia,   "mm", PROBE_C_YELLOW,  fo==0, 3);
    probeDrawKVTouch(122, 113, 111, 27, "Probe depth",  pendantProbeV2.boreDepth, "mm", PROBE_C_RED,     fo==1, 3);
    probeDrawKVTouch(122, 142, 111, 27, "Wall offset",  pendantProbeV2.boreOffset,"mm", PROBE_C_BLUE,    fo==2, 3);
    probeDrawKVTouchInt(122, 171, 111, 27, "Passes",    pendantProbeV2.borePasses,     PROBE_C_DIMBLUE,  fo==3);

    probeDrawWarn(204, "! Tip clear of walls at start", true);

    // Result line — what the probe will set (centred)
    {
        char s[28];
        snprintf(s, sizeof(s), "Sets %s X0 Y0 Z0", pendantProbing.selectedCoordSystem.c_str());
        display.setTextSize(1);
        display.setTextColor(PROBE_C_GREEN);
        display.setCursor(120 - display.textWidth(s) / 2, 224);
        display.print(s);
    }

    // Back — same location/style as the Z Surface screen
    drawButton(5, 239, 230, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);

    drawButton(  5, 280, 112, 38, "Main Menu", PROBE_BTN_BLUE,  COLOR_WHITE, 2);
    drawButton(123, 280, 112, 38, "Probe",     PROBE_BTN_GREEN, COLOR_WHITE, 2);

    if (pendantProbeV2.confirmActive)
        probeDrawConfirmOverlay("BORE");
}

void updateProbeBoreScreen() {
    if (currentPendantScreen != PSCREEN_PROBE_BORE) return;
    probeDrawPosPanel(38);
}

void handleProbeBoreTouch(int x, int y) {
    if (pendantProbeV2.confirmActive) {
        if (isTouchInBounds(x, y, 28, 175, 78, 32)) {
            pendantProbeV2.confirmActive = false;
            drawProbeBoreScreen();
        } else if (isTouchInBounds(x, y, 114, 175, 98, 32)) {
            pendantProbeV2.confirmActive = false;
            runProbeBore();
            currentPendantScreen = PSCREEN_STATUS;
        }
        return;
    }

    bool redraw = false;
    if (isTouchInBounds(x, y, 122, 84,  111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==0)?-1:0; redraw=true; }
    if (isTouchInBounds(x, y, 122, 113, 111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==1)?-1:1; redraw=true; }
    if (isTouchInBounds(x, y, 122, 142, 111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==2)?-1:2; redraw=true; }
    if (isTouchInBounds(x, y, 122, 171, 111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==3)?-1:3; redraw=true; }
    if (redraw) { drawProbeBoreScreen(); return; }

    if (isTouchInBounds(x, y, 5, 239, 230, 38)) {
        pendantProbeV2.returnScreen  = PSCREEN_PROBE_BORE;
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }

    if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
    if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
        if (!pendantConnected) { probeDrawWarn(204, "! Not connected", true); return; }
        pendantProbeV2.confirmActive = true;
        drawProbeBoreScreen();
        return;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  SCR3b — BOSS PROBE
// ══════════════════════════════════════════════════════════════════════════════

void enterProbeBoss() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
    pendantProbeV2.focusedField   = 0;
    pendantProbeV2.confirmActive  = false;
    pendantProbeV2.dialAccelCount = 0;
}

void exitProbeBoss() {}

// ── G-code: boss centre finding ──────────────────────────────────────────────
// Probe starts above boss centre.
// 1. Probe Z top, set Z0.
// 2. Retract, move to +X clearance position (outside boss).
// 3. Probe -X wall, move to -X clearance, probe +X wall (redundant for accuracy
//    but simpler: just probe +X, -X, +Y, -Y from outside positions).
// 4. Move to computed centre, G10 L20 P{n} X0 Y0.

static void runProbeBoss() {
    if (!pendantConnected) return;

    int   pNum  = pendantProbing.selectedCoordIndex + 1;
    float rate  = pendantProbeV2.probeRate;
    float depth = pendantProbeV2.bossDepth;    // positive mm below boss top
    float rad   = pendantProbeV2.bossDia / 2.0f;
    float clear = pendantProbeV2.bossClear;
    float retZ  = pendantProbeV2.retractDist;
    float maxZ  = pendantProbeV2.maxZTravel;
    float platZ = probeIs3D() ? pendantProbeV2.ballDia / 2.0f
                              : pendantProbeV2.plateThick;
    float edgeOfs = probeIs3D() ? pendantProbeV2.ballDia / 2.0f : 0.0f;

    char buf[80];

    // ── Step 1: Z probe (top of boss) ─────────────────────────────────────
    send_line("G91 G21");
    snprintf(buf, sizeof(buf), "G38.2 Z-%.3f F%.0f", maxZ, rate);
    send_line(buf);
    send_line("G90");
    // Set Z only for now; XY set after centre found
    // We'll use a temp WCS; actually just save Z offset via parameter
    // Simple approach: set Z in G10 L20 after finding centre
    // For now, capture the machine Z of the boss top with a param
    send_line("#<boss_z> = #5063");    // save probe Z position
    send_line("G91");
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", retZ);
    send_line(buf);

    // ── Step 2: +X side probe ─────────────────────────────────────────────
    // Move to outside +X side
    snprintf(buf, sizeof(buf), "G0 X%.3f F1000", rad + clear);
    send_line(buf);
    // Lower to probe depth below boss top
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", depth + retZ);
    send_line(buf);
    // Probe toward -X (into the boss from outside +X)
    snprintf(buf, sizeof(buf), "G38.2 X-%.3f F%.0f", clear + rad + 5.0f, rate);
    send_line(buf);
    send_line("#<px_pos> = #5061");   // +X side contact (but measured from -X direction)
    // Retract
    snprintf(buf, sizeof(buf), "G0 X%.3f F1000", clear + rad + 3.0f);
    send_line(buf);
    // Raise
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", depth + retZ);
    send_line(buf);

    // ── Step 3: -X side probe ─────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "G0 X-%.3f F1000", 2.0f * (rad + clear) + 3.0f);
    send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", depth + retZ);
    send_line(buf);
    snprintf(buf, sizeof(buf), "G38.2 X%.3f F%.0f", clear + rad + 5.0f, rate);
    send_line(buf);
    send_line("#<px_neg> = #5061");
    snprintf(buf, sizeof(buf), "G0 X-%.3f F1000", clear + rad + 3.0f);  // back to -X clearance
    send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", depth + retZ);
    send_line(buf);

    // ── Step 4: Return to X centre, then +Y side ─────────────────────────
    // Move to X centre (average of two probes is at nominal centre)
    // Faster: just go back by (rad + clear) from current -X position
    snprintf(buf, sizeof(buf), "G0 X%.3f F1000", rad + clear);
    send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Y%.3f F1000", rad + clear);
    send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", depth + retZ);
    send_line(buf);
    snprintf(buf, sizeof(buf), "G38.2 Y-%.3f F%.0f", clear + rad + 5.0f, rate);
    send_line(buf);
    send_line("#<py_pos> = #5062");
    snprintf(buf, sizeof(buf), "G0 Y%.3f F1000", clear + rad + 3.0f);
    send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", depth + retZ);
    send_line(buf);

    // ── Step 5: -Y side ───────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "G0 Y-%.3f F1000", 2.0f * (rad + clear) + 3.0f);
    send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", depth + retZ);
    send_line(buf);
    snprintf(buf, sizeof(buf), "G38.2 Y%.3f F%.0f", clear + rad + 5.0f, rate);
    send_line(buf);
    send_line("#<py_neg> = #5062");
    snprintf(buf, sizeof(buf), "G0 Y-%.3f F1000", clear + rad + 3.0f);
    send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", depth + retZ);
    send_line(buf);

    // ── Step 6: Move to computed boss centre and set WCS ──────────────────
    send_line("G90");
    send_line("G0 X[{#<px_pos>+#<px_neg>}/2] Y[{#<py_pos>+#<py_neg>}/2] F1000");
    // Set X0 Y0 at centre; Z0 = boss top (stored in #<boss_z>)
    // For Z, we need to go to the saved Z position.  Use: G10 L20 sets from current pos.
    // Easier: move to boss_z and set Z there:
    send_line("G0 Z[#<boss_z>] F500");
    snprintf(buf, sizeof(buf), "G10 L20 P%d X%.3f Y%.3f Z%.3f", pNum, edgeOfs, edgeOfs, platZ);
    send_line(buf);
    send_line("G91");
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", retZ);
    send_line(buf);
    send_line("G90");
}

// ── Layout SCR3b ─────────────────────────────────────────────────────────────
//   drawTitle     y=0   h=35
//   Pos panel     y=38  h=28
//   Seq+settings  y=70  h=130
//   Warn          y=204 h=14
//   Sets line     y=224 h=10
//   Back          y=239 h=38
//   Btn pair      y=280 h=38

void drawProbeBossScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("BOSS");
    probeDrawPosPanel(38);

    // Combined panel: sequence (left) + 4 KV settings (right).  Taller so the
    // size-2 KV values aren't clipped/overlapping (old 20 px fields overflowed).
    display.fillRoundRect(5, 70, 230, 130, 4, PROBE_BG_PANEL);

    // Left: sequence
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 73);
    display.print("SEQUENCE");
    drawSeqStep( 8, 87,  1, "Touch top->Z0",  true);
    drawSeqStep( 8, 105, 2, "Retract & move", false);
    drawSeqStep( 8, 123, 3, "Sweep XY walls", false);

    // Right: KV settings (h=27 so the value isn't clipped)
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(122, 73);
    display.print("SETTINGS");

    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch(122, 84,  111, 27, "Nominal dia.", pendantProbeV2.bossDia,   "mm", PROBE_C_YELLOW,  fo==0, 3);
    probeDrawKVTouch(122, 113, 111, 27, "Probe depth",  pendantProbeV2.bossDepth, "mm", PROBE_C_RED,     fo==1, 3);
    probeDrawKVTouch(122, 142, 111, 27, "Clearance",    pendantProbeV2.bossClear, "mm", PROBE_C_BLUE,    fo==2, 3);
    probeDrawKVTouchInt(122, 171, 111, 27, "Passes",    pendantProbeV2.bossPasses,     PROBE_C_DIMBLUE,  fo==3);

    probeDrawWarn(204, "! Start above centre of boss");

    // Result line — what the probe will set (centred)
    {
        char s[28];
        snprintf(s, sizeof(s), "Sets %s X0 Y0 Z0", pendantProbing.selectedCoordSystem.c_str());
        display.setTextSize(1);
        display.setTextColor(PROBE_C_GREEN);
        display.setCursor(120 - display.textWidth(s) / 2, 224);
        display.print(s);
    }

    // Back — same location/style as the Z Surface screen
    drawButton(5, 239, 230, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);

    drawButton(  5, 280, 112, 38, "Main Menu", PROBE_BTN_BLUE,  COLOR_WHITE, 2);
    drawButton(123, 280, 112, 38, "Probe",     PROBE_BTN_GREEN, COLOR_WHITE, 2);

    if (pendantProbeV2.confirmActive)
        probeDrawConfirmOverlay("BOSS");
}

void updateProbeBossScreen() {
    if (currentPendantScreen != PSCREEN_PROBE_BOSS) return;
    probeDrawPosPanel(38);
}

void handleProbeBossTouch(int x, int y) {
    if (pendantProbeV2.confirmActive) {
        if (isTouchInBounds(x, y, 28, 175, 78, 32)) {
            pendantProbeV2.confirmActive = false;
            drawProbeBossScreen();
        } else if (isTouchInBounds(x, y, 114, 175, 98, 32)) {
            pendantProbeV2.confirmActive = false;
            runProbeBoss();
            currentPendantScreen = PSCREEN_STATUS;
        }
        return;
    }

    bool redraw = false;
    if (isTouchInBounds(x, y, 122, 84,  111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==0)?-1:0; redraw=true; }
    if (isTouchInBounds(x, y, 122, 113, 111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==1)?-1:1; redraw=true; }
    if (isTouchInBounds(x, y, 122, 142, 111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==2)?-1:2; redraw=true; }
    if (isTouchInBounds(x, y, 122, 171, 111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==3)?-1:3; redraw=true; }
    if (redraw) { drawProbeBossScreen(); return; }

    if (isTouchInBounds(x, y, 5, 239, 230, 38)) {
        pendantProbeV2.returnScreen  = PSCREEN_PROBE_BOSS;
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }

    if (isTouchInBounds(x, y, 5, 280, 112, 38)) { currentPendantScreen = PSCREEN_MAIN_MENU; return; }
    if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
        if (!pendantConnected) { probeDrawWarn(204, "! Not connected", true); return; }
        pendantProbeV2.confirmActive = true;
        drawProbeBossScreen();
        return;
    }
}
