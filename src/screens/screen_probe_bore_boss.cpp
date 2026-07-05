/*
 * screen_probe_bore_boss.cpp  —  SCR3a: Bore / SCR3b: Boss
 *
 * Both routines find the XY centre of a round feature by probing FOUR points
 * radially at 90° (±X, ±Y) and averaging the two opposed pairs: the centre is
 * simply the midpoint of the +X/−X touches (X) and the +Y/−Y touches (Y).  Each
 * point is a crash-safe two-pass move (fast seek + slow re-probe) approaching the
 * wall RADIALLY, so contact is perpendicular to the curve — no tangential skid.
 * Opposed pairs also cancel the probe tip radius / deflection per axis, so no
 * ball-radius compensation is needed, and the maths is division-free (unlike a
 * three-point circumcentre, which can divide by a near-zero triangle area).
 *
 * The X pair is probed first; the tool then RE-CENTRES in X before the Y pair, so
 * the +Y/−Y probes run through the true vertical diameter and contact each wall
 * head-on even if the operator started well off-centre.
 *
 * Coordinate frames: FluidNC returns G38 probe results (#5061/#5062) in MACHINE
 * coordinates, so the centre maths and the final move are done in machine coords
 * (G53).  The between-probe return uses the saved start (current-position params
 * #5420/#5421, work coords) via G90 — the same convention as the reference macro.
 *
 * BORE (inside circle):
 *   Operator places the probe tip INSIDE the bore at any comfortable depth.  No Z
 *   motion — Z work-zero is handled separately by the Z Surface probe.  Probe 4
 *   points outward, average the pairs, G53 move to the centre, G10 L20 sets X0 Y0.
 *
 * BOSS (outside circle):
 *   Operator starts above the boss centre.
 *   1. Touch the flat top and set Z0 there (the top is the natural Z datum).
 *   2. For each of 4 directions: move clear, plunge beside the boss, two-pass
 *      probe inward.
 *   3. Average the opposed pairs, G53 move to the centre, G10 L20 sets X0 Y0
 *      (Z0 already set).
 *
 * focusedField for bore:  0=boreDia  1=boreOffset
 * focusedField for boss:  0=bossDia  1=bossDepth  2=bossClear
 */

#include "pendant_shared.h"
#include "screen_probe.h"
#include "screen_probe_bore_boss.h"

// Two-pass probe along a unit direction (ux,uy): fast seek to contact, back off,
// slow re-probe.  Ends at the fine trigger (machine pos in #5061/#5062).  G91.
static void probeRadial2Pass(float ux, float uy, float seekDist, float seekF, float fineF) {
    const float BACKOFF = 1.5f;
    char b[96];
    snprintf(b, sizeof(b), "G38.2 G91 X%.3f Y%.3f F%.0f", seekDist * ux, seekDist * uy, seekF); send_line(b);
    snprintf(b, sizeof(b), "G0 G91 X%.3f Y%.3f F1000",   -BACKOFF * ux, -BACKOFF * uy);         send_line(b);
    snprintf(b, sizeof(b), "G38.2 G91 X%.3f Y%.3f F%.0f", (BACKOFF + 1.0f) * ux, (BACKOFF + 1.0f) * uy, fineF); send_line(b);
}

// Two-pass probe along (ux,uy), then store the along-axis machine-coord result:
// axisX -> #5061 (X wall), else #5062 (Y wall).  Only the coordinate on the
// probe axis is needed for that pair's midpoint.
static void probeWallStore(float ux, float uy, float seek, float seekF, float fineF,
                           const char* storeVar, bool axisX) {
    probeRadial2Pass(ux, uy, seek, seekF, fineF);
    char b[64];
    snprintf(b, sizeof(b), "%s = #%s", storeVar, axisX ? "5061" : "5062");
    send_line(b);
}

// Move to the found centre (machine #<xc>,#<yc>) and zero X/Y there.
static void emitMoveCentreZero(int pNum) {
    send_line("G53 G0 X#<xc> Y#<yc>");
    char b[64];
    snprintf(b, sizeof(b), "G10 L20 P%d X0 Y0", pNum);
    send_line(b);
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

// ── G-code: bore centre finding (4-point radial) ─────────────────────────────
// No Z motion — the tip is pre-placed inside the bore.  Probe the ±X pair, re-
// centre X, then probe the ±Y pair through that centred X (true vertical
// diameter), average each pair, G53-move to the centre and set X0/Y0.

static void runProbeBore() {
    if (!pendantConnected) return;

    int   pNum   = pendantProbing.selectedCoordIndex + 1;
    float seekF  = pendantProbeV2.seekRate;
    float fineF  = pendantProbeV2.probeRate;
    float rad    = pendantProbeV2.boreDia / 2.0f;
    float wallOf = pendantProbeV2.boreOffset;
    // Generous outward seek — a diameter + margin covers an off-centre start
    // (the G38.2 stops on contact, so over-estimating is safe).
    float d = 2.0f * rad + wallOf + 3.0f;

    probeActivateWcs();          // zero into the system shown on screen
    send_line("G21 G90");
    send_line("#<sx> = #5420");   // save start (work coords) to return between probes
    send_line("#<sy> = #5421");

    // ── X pair: probe +X and −X along Y = start, average to the centre X ──
    probeWallStore( 1.0f, 0.0f, d, seekF, fineF, "#<ax>", true);
    send_line("G90 G0 X#<sx> Y#<sy> F1000");                 // back to start
    probeWallStore(-1.0f, 0.0f, d, seekF, fineF, "#<cx>", true);
    send_line("G90 G0 X#<sx> Y#<sy> F1000");                 // back to start
    send_line("#<xc> = [[#<ax> + #<cx>] / 2]");              // machine X of centre
    send_line("G53 G0 X#<xc>");                              // re-centre X (Y stays at start)

    // ── Y pair: probe +Y and −Y through the centred X (true vertical diameter) ──
    probeWallStore(0.0f,  1.0f, d, seekF, fineF, "#<by>", false);
    send_line("G90 G0 Y#<sy> F1000");                        // Y back to start (X stays centred)
    probeWallStore(0.0f, -1.0f, d, seekF, fineF, "#<dy>", false);
    send_line("#<yc> = [[#<by> + #<dy>] / 2]");              // machine Y of centre

    emitMoveCentreZero(pNum);
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
    drawSeqStep( 8, 87,  1, "Probe 4 points", true);
    drawSeqStep( 8, 105, 2, "Find centre",    false);
    drawSeqStep( 8, 123, 3, "Set X0 Y0",      false);
    drawBoreDiagram();

    // Right column: 2 KV-touch settings (h=27 so the value isn't clipped)
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(122, 73);
    display.print("SETTINGS");

    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch(122, 84,  111, 27, "Nominal dia.", pendantProbeV2.boreDia,    "mm", PROBE_C_BLUE,  fo==0, 3);
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

    probeDrawWarn(220, "! Place tip inside the bore");

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

// One boss wall: move clear along (ux,uy) at safe Z, plunge beside the boss,
// two-pass probe INWARD (−ux,−uy), store the along-axis result, then lift back
// to safe Z.  Leaves XY at the probed wall for the caller to return home.
static void bossWallStore(float ux, float uy, float out, float inSeek, float plunge,
                          float seekF, float fineF, const char* storeVar, bool axisX) {
    char b[80];
    snprintf(b, sizeof(b), "G0 G91 X%.3f Y%.3f F1000", out * ux, out * uy); send_line(b);
    snprintf(b, sizeof(b), "G0 G91 Z-%.3f F500", plunge);                   send_line(b);
    probeWallStore(-ux, -uy, inSeek, seekF, fineF, storeVar, axisX);        // inward
    snprintf(b, sizeof(b), "G0 G91 Z%.3f F500", plunge);                    send_line(b);  // lift
}

// ── G-code: boss centre finding (4-point radial) ─────────────────────────────
// Starts above the boss centre.  Touches the flat top and sets Z0 there.  Probes
// the ±X pair, re-centres X, then probes the ±Y pair through that centred X;
// averages each pair, G53-moves to the centre and sets X0/Y0.  Works for a
// circular boss (one diameter) or a rectangular boss (independent X/Y sizes) —
// the only difference is that the per-axis clear/seek distances are sized from
// half the X size for the X pair and half the Y size for the Y pair.  The
// opposed-pair midpoint gives the true centre for either shape.

static void runProbeBoss() {
    if (!pendantConnected) return;

    int   pNum  = pendantProbing.selectedCoordIndex + 1;
    float seekF = pendantProbeV2.seekRate;
    float fineF = pendantProbeV2.probeRate;
    float depth = pendantProbeV2.bossDepth;    // mm below boss top to probe at
    float clear = pendantProbeV2.bossClear;
    float retZ  = pendantProbeV2.retractDist;
    float maxZ  = pendantProbeV2.maxZTravel;
    float platZ = probeIs3D() ? probeTipOffset3D()
                              : pendantProbeV2.plateThick;

    // Half-size per axis.  Circular: both halves = bossDia/2.  Rectangular: X uses
    // bossDia/2, Y uses bossSizeY/2.
    float radX = pendantProbeV2.bossDia / 2.0f;
    float radY = (pendantProbeV2.bossRect ? pendantProbeV2.bossSizeY : pendantProbeV2.bossDia) / 2.0f;

    float outX    = radX + clear;           // clear the boss on X before plunging
    float inSeekX = clear + radX + 5.0f;    // inward seek travel toward an X wall
    float outY    = radY + clear;
    float inSeekY = clear + radY + 5.0f;
    float plunge  = depth + retZ;           // from (top+retZ) down to (top-depth)

    char buf[80];
    probeActivateWcs();          // zero into the system shown on screen
    send_line("G21 G90");
    send_line("#<sx> = #5420");   // save start (work coords)
    send_line("#<sy> = #5421");

    // Touch the top and set Z0 there (the boss top is the Z datum), then lift.
    send_line("G91");
    snprintf(buf, sizeof(buf), "G38.2 Z-%.3f F%.0f", maxZ, fineF);  send_line(buf);
    send_line("G90");
    snprintf(buf, sizeof(buf), "G10 L20 P%d Z%.3f", pNum, platZ);   send_line(buf);
    send_line("G91");
    snprintf(buf, sizeof(buf), "G0 Z%.3f F500", retZ);             send_line(buf);  // to top+retZ
    send_line("G90");

    // Each wall: move clear (at safe Z), plunge beside the boss, two-pass probe
    // inward, lift straight up, return home.
    // ── X pair along Y = start ──
    bossWallStore( 1.0f, 0.0f, outX, inSeekX, plunge, seekF, fineF, "#<ax>", true);
    send_line("G90 G0 X#<sx> Y#<sy> F1000");                 // back to start
    bossWallStore(-1.0f, 0.0f, outX, inSeekX, plunge, seekF, fineF, "#<cx>", true);
    send_line("G90 G0 X#<sx> Y#<sy> F1000");                 // back to start
    send_line("#<xc> = [[#<ax> + #<cx>] / 2]");              // machine X of centre
    send_line("G53 G0 X#<xc>");                              // re-centre X at safe Z (Y at start)

    // ── Y pair through the centred X ──
    bossWallStore(0.0f,  1.0f, outY, inSeekY, plunge, seekF, fineF, "#<by>", false);
    send_line("G90 G0 Y#<sy> F1000");                        // Y back to start (X stays centred)
    bossWallStore(0.0f, -1.0f, outY, inSeekY, plunge, seekF, fineF, "#<dy>", false);
    send_line("#<yc> = [[#<by> + #<dy>] / 2]");              // machine Y of centre

    emitMoveCentreZero(pNum);   // sets X0 Y0; Z0 already set at the top
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

// Top-down diagram of RECTANGULAR boss probing: the boss outline (plan view) with
// four inward arrows probing the ±X / ±Y faces and a Z0 tick at the centre.  The
// top-down projection (vs the circular boss's side view) is the visual cue that
// rectangular mode is active.
static void drawBossDiagramRect() {
    // Boss outline (plan view), centred on (60, 175).
    display.drawRect(40, 161, 41, 29, PROBE_C_LBLUE);
    // Inward face-probe arrows (green), one per side, pointing at the face middle.
    // +X (from the right, probing toward −X)
    display.drawLine(94, 175, 83, 175, PROBE_C_GREEN);
    display.drawLine(83, 175, 87, 172, PROBE_C_GREEN);
    display.drawLine(83, 175, 87, 178, PROBE_C_GREEN);
    // −X (from the left, probing toward +X)
    display.drawLine(26, 175, 37, 175, PROBE_C_GREEN);
    display.drawLine(37, 175, 33, 172, PROBE_C_GREEN);
    display.drawLine(37, 175, 33, 178, PROBE_C_GREEN);
    // +Y (from the top, probing downward)
    display.drawLine(60, 145, 60, 158, PROBE_C_GREEN);
    display.drawLine(60, 158, 57, 154, PROBE_C_GREEN);
    display.drawLine(60, 158, 63, 154, PROBE_C_GREEN);
    // −Y (from the bottom, probing upward)
    display.drawLine(60, 205, 60, 192, PROBE_C_GREEN);
    display.drawLine(60, 192, 57, 196, PROBE_C_GREEN);
    display.drawLine(60, 192, 63, 196, PROBE_C_GREEN);
    // Z0 tick at the centre (the top touch still sets Z0 first).
    display.fillCircle(60, 175, 2, PROBE_C_YELLOW);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_YELLOW);
    display.setCursor(64, 172);
    display.print("Z0");
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
    drawSeqStep( 8, 105, 2, "Probe 4 points", false);
    drawSeqStep( 8, 123, 3, "Set X0 Y0",      false);
    // Tappable diagram: a single tap cycles Circular <-> Rectangular boss.  The
    // grey border marks it as a button (our tappable-field convention) and the
    // diagram itself is the mode indicator, so tapping it to change shape reads
    // naturally.  Replaces the old triple-tap on the first settings field.
    display.drawRoundRect(6, 136, 112, 79, 3, PROBE_C_TAPBDR);
    if (pendantProbeV2.bossRect) drawBossDiagramRect();
    else                         drawBossDiagram();
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 206);
    display.print("Tap: shape");

    // Right: KV settings (h=27 so the value isn't clipped).  Rectangular mode
    // splits the nominal size into X-size / Y-size, so it shows one extra field.
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(122, 73);
    display.print("SETTINGS");

    int fo = pendantProbeV2.focusedField;
    if (pendantProbeV2.bossRect) {
        // 0=X size (bossDia) 1=Y size (bossSizeY) 2=Probe depth Z 3=Clearance
        probeDrawKVTouch(122, 84,  111, 27, "X size",       pendantProbeV2.bossDia,   "mm", PROBE_C_BLUE, fo==0, 3);
        probeDrawKVTouch(122, 113, 111, 27, "Y size",       pendantProbeV2.bossSizeY, "mm", PROBE_C_BLUE, fo==1, 3);
        probeDrawKVTouch(122, 142, 111, 27, "Probe depth Z", pendantProbeV2.bossDepth, "mm", PROBE_C_BLUE, fo==2, 3);
        probeDrawKVTouch(122, 171, 111, 27, "Clearance",    pendantProbeV2.bossClear, "mm", PROBE_C_BLUE, fo==3, 3);
    } else {
        // 0=Nominal dia. (bossDia) 1=Probe depth Z 2=Clearance
        probeDrawKVTouch(122, 84,  111, 27, "Nominal dia.",  pendantProbeV2.bossDia,   "mm", PROBE_C_BLUE, fo==0, 3);
        probeDrawKVTouch(122, 113, 111, 27, "Probe depth Z", pendantProbeV2.bossDepth, "mm", PROBE_C_BLUE, fo==1, 3);
        probeDrawKVTouch(122, 142, 111, 27, "Clearance",     pendantProbeV2.bossClear, "mm", PROBE_C_BLUE, fo==2, 3);
    }

    // Result line — what axes the probe will set (below the last field)
    {
        const char* s = "Sets X0 Y0 Z0";
        display.setTextSize(1);
        display.setTextColor(PROBE_C_GREEN);
        display.setCursor(177 - display.textWidth(s) / 2, pendantProbeV2.bossRect ? 205 : 182);
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

    // Tappable diagram box — single tap cycles Circular <-> Rectangular boss.
    if (isTouchInBounds(x, y, 6, 136, 112, 79)) {
        pendantProbeV2.bossRect = !pendantProbeV2.bossRect;
        // Clamp focus to the field count of the new mode (rect=4, circ=3).
        int maxField = pendantProbeV2.bossRect ? 3 : 2;
        if (pendantProbeV2.focusedField > maxField) pendantProbeV2.focusedField = -1;
        saveProbeSettings();
        drawProbeBossScreen();
        return;
    }

    // Settings fields — tap to focus for dial editing.
    bool redraw = false;
    if (isTouchInBounds(x, y, 122, 84,  111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==0)?-1:0; redraw=true; }
    if (isTouchInBounds(x, y, 122, 113, 111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==1)?-1:1; redraw=true; }
    if (isTouchInBounds(x, y, 122, 142, 111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==2)?-1:2; redraw=true; }
    if (pendantProbeV2.bossRect &&
        isTouchInBounds(x, y, 122, 171, 111, 27)) { pendantProbeV2.focusedField=(pendantProbeV2.focusedField==3)?-1:3; redraw=true; }
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
