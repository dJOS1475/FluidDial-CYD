/*
 * screen_probe_bore_boss.cpp  —  SCR3a: Bore / SCR3b: Boss
 *
 * Both routines find the XY centre of a round feature with crash-safe two-pass
 * probing: every wall is reached with a G38.2 probing move (fast seek + slow
 * re-probe) — never a blind rapid — so a wrong nominal diameter can't drive the
 * tip into a wall.  Each re-centres on X before the Y pair so Y runs through the
 * true diameter (square to the wall) for better precision.
 *
 * BORE (inside circle):
 *   Operator places the probe tip INSIDE the bore at any comfortable depth.
 *   No Z motion at all — Z work-zero is handled separately by the Z Surface
 *   probe (the screen says so).  Probe +X/-X, re-centre, probe +Y/-Y, then
 *   G10 L20 sets X0 Y0.
 *
 * BOSS (outside circle):
 *   Operator starts above the boss centre.
 *   1. Touch the flat top and set Z0 there (the top is the natural Z datum).
 *   2. For each side: move clear of the boss, plunge beside it, two-pass probe
 *      inward.  Re-centre on X before the Y pair.
 *   3. Move to centre, G10 L20 sets X0 Y0 (Z0 already set at the top).
 *
 * focusedField for bore:  0=boreDia  1=boreOffset
 * focusedField for boss:  0=bossDia  1=bossDepth  2=bossClear
 */

#include "pendant_shared.h"
#include "screen_probe.h"
#include "screen_probe_bore_boss.h"

// One crash-safe edge for centre-finding: two-pass approach (probeSeekFine),
// save the fine trigger to a named param, then back off so the probe input is
// open for the next G38.2.  seekDist is signed; resultReg is the probe-result
// register (#5061 X / #5062 Y).
static void probeEdge2Pass(const char* axis, float seekDist, const char* resultReg,
                           const char* namedParam, float seekF, float fineF) {
    const float BACKOFF = 1.5f;
    int  dir = (seekDist >= 0.0f) ? 1 : -1;
    char b[96];
    probeSeekFine(axis, seekDist, seekF, fineF);                     // ends at the trigger
    snprintf(b, sizeof(b), "%s = %s", namedParam, resultReg);        send_line(b);  // save trigger
    snprintf(b, sizeof(b), "G0 %s%.3f F1000", axis, -dir * BACKOFF); send_line(b);  // open probe
}

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
// No Z motion — the tip is pre-placed inside the bore.  Every wall is reached
// with a G38.2 probing move (two-pass), so the nominal diameter is only a travel
// hint and a wrong value can't crash the tip.  We re-centre on X before probing
// Y so the Y pair runs through the true diameter.  Sets X0/Y0 only.

static void runProbeBore() {
    if (!pendantConnected) return;

    int   pNum   = pendantProbing.selectedCoordIndex + 1;
    float seekF  = pendantProbeV2.seekRate;
    float fineF  = pendantProbeV2.probeRate;
    float rad    = pendantProbeV2.boreDia / 2.0f;
    float wallOf = pendantProbeV2.boreOffset;   // extra seek over-travel margin
    // Seek travel from ~centre to a wall, and across to the opposite wall.
    float toWall  = rad + wallOf + 3.0f;
    float across  = 2.0f * rad + wallOf + 3.0f;

    send_line("G91 G21");

    // ── X pair ──────────────────────────────────────────────────────────────
    probeEdge2Pass("X",  toWall,  "#5061", "#<bx_pos>", seekF, fineF);
    probeEdge2Pass("X", -across,  "#5061", "#<bx_neg>", seekF, fineF);

    // Move to the computed X centre (work coords) before the Y pair.
    send_line("G90");
    send_line("G0 X[{#<bx_pos>+#<bx_neg>}/2] F1000");
    send_line("G91");

    // ── Y pair (now through the true X centre) ────────────────────────────────
    probeEdge2Pass("Y",  toWall,  "#5062", "#<by_pos>", seekF, fineF);
    probeEdge2Pass("Y", -across,  "#5062", "#<by_neg>", seekF, fineF);

    // Move to centre and set XY origin.  Z is left untouched (use Z Surface).
    char buf[80];
    send_line("G90");
    send_line("G0 X[{#<bx_pos>+#<bx_neg>}/2] Y[{#<by_pos>+#<by_neg>}/2] F1000");
    snprintf(buf, sizeof(buf), "G10 L20 P%d X0 Y0", pNum);
    send_line(buf);
}

// ── Draw helpers (bore) ──────────────────────────────────────────────────────

// Top-down diagram of bore probing, drawn in the left column under the SEQUENCE
// list: the hole outline with arrows probing outward to the walls and a centre
// mark (XY centre finding).
static void drawBoreDiagram() {
    // Inverted boss: a pocket recessed into the stock (cross-section).  Stylus
    // descends into the hole; arrows probe outward to the side walls (XY).
    display.fillRect(18, 182, 84, 20, PROBE_C_DIMBLUE);   // stock block (surface y182)
    display.fillRect(46, 182, 28, 13, PROBE_BG_PANEL);    // bored pocket (void, boss-width)
    // Probe body/stem + stylus descending into the hole
    display.fillRoundRect(55, 150, 10, 12, 2, PROBE_C_LBLUE);
    display.drawLine(60, 161, 60, 189, PROBE_C_YELLOW);
    display.fillCircle(60, 191, 2, PROBE_C_YELLOW);
    // Side-wall probes (XY) — outward arrows
    display.drawLine(60, 188, 48, 188, PROBE_C_GREEN);    // left shaft
    display.drawLine(48, 188, 52, 185, PROBE_C_GREEN);    // left head
    display.drawLine(48, 188, 52, 191, PROBE_C_GREEN);
    display.drawLine(60, 188, 72, 188, PROBE_C_GREEN);    // right shaft
    display.drawLine(72, 188, 68, 185, PROBE_C_GREEN);    // right head
    display.drawLine(72, 188, 68, 191, PROBE_C_GREEN);
}

void drawProbeBoreScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("BORE");
    probeDrawPosPanel(38);

    // ── Combined panel: sequence (left) + settings & result (right) ───────────
    display.fillRoundRect(5, 70, 230, 146, 4, PROBE_BG_PANEL);

    // Left column: sequence steps
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 73);
    display.print("SEQUENCE");
    drawSeqStep( 8, 87,  1, "Probe XY walls", true);
    drawSeqStep( 8, 105, 2, "Re-centre on X", false);
    drawSeqStep( 8, 123, 3, "Set X0 Y0",      false);
    drawBoreDiagram();

    // Right column: 2 KV-touch settings (h=27 so the value isn't clipped)
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(122, 73);
    display.print("SETTINGS");

    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch(122, 84,  111, 27, "Nominal dia.", pendantProbeV2.boreDia,    "mm", PROBE_C_YELLOW,  fo==0, 3);
    probeDrawKVTouch(122, 113, 111, 27, "Wall offset",  pendantProbeV2.boreOffset, "mm", PROBE_C_BLUE,    fo==1, 3);

    // Right column, centred: result line, then the Z-Surface note.
    display.setTextSize(1);
    {
        const char* s = "Sets X0 Y0";
        display.setTextColor(PROBE_C_GREEN);
        display.setCursor(177 - display.textWidth(s) / 2, 150);
        display.print(s);
        const char* a = "Z work-zero:";
        display.setTextColor(PROBE_C_LBLUE);
        display.setCursor(177 - display.textWidth(a) / 2, 168);
        display.print(a);
        display.setTextColor(PROBE_C_GREEN);
        const char* b = "use Z Surface";
        display.setCursor(177 - display.textWidth(b) / 2, 182);
        display.print(b);
        const char* c = "probe routine";
        display.setCursor(177 - display.textWidth(c) / 2, 196);
        display.print(c);
    }

    probeDrawWarn(220, "! Place tip inside the bore", true);

    // Bottom-nav row: Back (left) | work-area selector (right)
    drawButton(5, 239, 112, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
    probeDrawWorkAreaButton(123, 239, 112, 38);

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
    if (redraw) { drawProbeBoreScreen(); return; }

    if (isTouchInBounds(x, y, 5, 239, 112, 38)) {
        pendantProbeV2.returnScreen  = PSCREEN_PROBE_BORE;
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }
    if (isTouchInBounds(x, y, 123, 239, 112, 38)) {
        probeCycleWorkArea();
        drawProbeBoreScreen();
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
// Starts above the boss centre.  Touches the flat top and sets Z0 there (the top
// is the natural Z datum for an outside feature).  For each side it moves clear
// of the boss, plunges beside it, and two-pass probes inward; re-centres on X
// before the Y pair.  Every wall is found with a G38.2 probing move.  Because the
// centre is the average of opposing walls, the ball radius cancels for XY.

static void runProbeBoss() {
    if (!pendantConnected) return;

    int   pNum  = pendantProbing.selectedCoordIndex + 1;
    float seekF = pendantProbeV2.seekRate;
    float fineF = pendantProbeV2.probeRate;
    float depth = pendantProbeV2.bossDepth;    // mm below boss top to probe at
    float rad   = pendantProbeV2.bossDia / 2.0f;
    float clear = pendantProbeV2.bossClear;
    float retZ  = pendantProbeV2.retractDist;
    float maxZ  = pendantProbeV2.maxZTravel;
    float platZ = probeIs3D() ? pendantProbeV2.ballDia / 2.0f
                              : pendantProbeV2.plateThick;

    float out   = rad + clear;             // radial move to clear the boss
    float inSeek= clear + rad + 5.0f;      // inward seek travel toward a wall
    float plunge= depth + retZ;            // from (top+retZ) down to (top-depth)

    char buf[80];

    send_line("G91 G21");

    // Touch the top and set Z0 there (the boss top is the Z datum).
    snprintf(buf, sizeof(buf), "G38.2 Z-%.3f F%.0f", maxZ, fineF);
    send_line(buf);
    send_line("G90");
    snprintf(buf, sizeof(buf), "G10 L20 P%d Z%.3f", pNum, platZ);
    send_line(buf);
    send_line("G91");
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", retZ);   // up to top + retZ
    send_line(buf);

    // Each side: move clear of the boss, plunge beside it, two-pass probe inward,
    // then lift STRAIGHT UP (the probe is already 1.5 mm off the wall) before
    // traversing — no horizontal retract, which would otherwise carry the tip
    // back over the boss and crash the next plunge.

    // ── +X side (probe inward, -X) ────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "G0 X%.3f F1000", out);     send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", plunge);  send_line(buf);
    probeEdge2Pass("X", -inSeek, "#5061", "#<px_pos>", seekF, fineF);
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", plunge);   send_line(buf);   // lift over top

    // ── -X side (probe inward, +X) ────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "G0 X-%.3f F1000", 2.0f * out); send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", plunge);      send_line(buf);
    probeEdge2Pass("X",  inSeek, "#5061", "#<px_neg>", seekF, fineF);
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", plunge);  send_line(buf);    // lift over top

    // Re-centre on X (work coords) at safe height before the Y pair.
    send_line("G90");
    send_line("G0 X[{#<px_pos>+#<px_neg>}/2] F1000");
    send_line("G91");

    // ── +Y side (probe inward, -Y) ────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "G0 Y%.3f F1000", out);     send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", plunge);  send_line(buf);
    probeEdge2Pass("Y", -inSeek, "#5062", "#<py_pos>", seekF, fineF);
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", plunge);   send_line(buf);

    // ── -Y side (probe inward, +Y) ────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "G0 Y-%.3f F1000", 2.0f * out); send_line(buf);
    snprintf(buf, sizeof(buf), "G0 Z-%.3f F500", plunge);      send_line(buf);
    probeEdge2Pass("Y",  inSeek, "#5062", "#<py_neg>", seekF, fineF);
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", plunge);  send_line(buf);    // lift to safe height

    // Move to centre and set XY origin.  Z is left untouched (use Z Surface).
    send_line("G90");
    send_line("G0 X[{#<px_pos>+#<px_neg>}/2] Y[{#<py_pos>+#<py_neg>}/2] F1000");
    snprintf(buf, sizeof(buf), "G10 L20 P%d X0 Y0", pNum);
    send_line(buf);
}

// ── Layout SCR3b ─────────────────────────────────────────────────────────────
//   drawTitle     y=0   h=35
//   Pos panel     y=38  h=28
//   Seq+settings  y=70  h=130
//   Warn          y=204 h=14
//   Sets line     y=224 h=10
//   Back          y=239 h=38
//   Btn pair      y=280 h=38

// Small side-view diagram of boss probing, drawn in the left column under the
// SEQUENCE list: a stylus touching the raised top (Z0) and inward arrows probing
// the side walls (XY centre).
static void drawBossDiagram() {
    // Raised boss on the stock (cross-section).  Stylus touches the top (Z0);
    // arrows probe inward to the side walls (XY).
    display.fillRect(18, 196, 84, 9, PROBE_C_DIMBLUE);   // stock slab
    display.fillRect(46, 182, 28, 14, PROBE_C_LBLUE);    // raised boss
    // Probe body/stem + stylus touching the boss top (Z0)
    display.fillRoundRect(55, 141, 10, 12, 2, PROBE_C_LBLUE);
    display.drawLine(60, 152, 60, 180, PROBE_C_YELLOW);
    display.fillCircle(60, 182, 2, PROBE_C_YELLOW);
    display.drawLine(57, 165, 60, 169, PROBE_C_YELLOW);  // down chevron
    display.drawLine(63, 165, 60, 169, PROBE_C_YELLOW);
    // Side-wall probes (XY) — inward arrows
    display.drawLine(26, 189, 44, 189, PROBE_C_GREEN);   // left shaft
    display.drawLine(44, 189, 40, 186, PROBE_C_GREEN);   // left head
    display.drawLine(44, 189, 40, 192, PROBE_C_GREEN);
    display.drawLine(94, 189, 76, 189, PROBE_C_GREEN);   // right shaft
    display.drawLine(76, 189, 80, 186, PROBE_C_GREEN);   // right head
    display.drawLine(76, 189, 80, 192, PROBE_C_GREEN);
}

void drawProbeBossScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("BOSS");
    probeDrawPosPanel(38);

    // Combined panel: sequence + diagram (left) / settings + result (right).
    display.fillRoundRect(5, 70, 230, 146, 4, PROBE_BG_PANEL);

    // Left: sequence + diagram
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 73);
    display.print("SEQUENCE");
    drawSeqStep( 8, 87,  1, "Touch top->Z0",  true);
    drawSeqStep( 8, 105, 2, "Probe XY walls", false);
    drawSeqStep( 8, 123, 3, "Set X0 Y0",      false);
    drawBossDiagram();

    // Right: KV settings (h=27 so the value isn't clipped)
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(122, 73);
    display.print("SETTINGS");

    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch(122, 84,  111, 27, "Nominal dia.", pendantProbeV2.bossDia,   "mm", PROBE_C_YELLOW,  fo==0, 3);
    probeDrawKVTouch(122, 113, 111, 27, "Probe depth",  pendantProbeV2.bossDepth, "mm", PROBE_C_RED,     fo==1, 3);
    probeDrawKVTouch(122, 142, 111, 27, "Clearance",    pendantProbeV2.bossClear, "mm", PROBE_C_BLUE,    fo==2, 3);

    // Result line — what axes the probe will set (in the right column, below clearance)
    {
        const char* s = "Sets X0 Y0 Z0";
        display.setTextSize(1);
        display.setTextColor(PROBE_C_GREEN);
        display.setCursor(177 - display.textWidth(s) / 2, 182);
        display.print(s);
    }

    probeDrawWarn(220, "! Start above centre of boss");

    // Bottom-nav row: Back (left) | work-area selector (right)
    drawButton(5, 239, 112, 38, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
    probeDrawWorkAreaButton(123, 239, 112, 38);

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
    if (redraw) { drawProbeBossScreen(); return; }

    if (isTouchInBounds(x, y, 5, 239, 112, 38)) {
        pendantProbeV2.returnScreen  = PSCREEN_PROBE_BOSS;
        currentPendantScreen = PSCREEN_PROBE;
        return;
    }
    if (isTouchInBounds(x, y, 123, 239, 112, 38)) {
        probeCycleWorkArea();
        drawProbeBossScreen();
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
