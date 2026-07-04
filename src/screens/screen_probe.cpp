/*
 * screen_probe.cpp  —  SCR0: Probe Hub
 *
 * The entry point from the main menu.  Shows:
 *   • Current XYZ position
 *   • Probe type selector (Z-Height Plate / XYZ Plate / 3D Probe) + Config button
 *   • 4 shared settings KV fields (probe rate, seek rate, retract dist, max Z travel)
 *   • 4 probe-routine buttons (Z Surface, XYZ Corner, Bore, Boss)
 *   • Main Menu button
 *
 * Also contains all shared drawing helpers and NVS persistence used by the
 * remaining probe screens (screen_probe_z, _corner, _bore_boss, _cfg).
 */

#include "pendant_shared.h"
#include "screen_probe.h"
#include <Preferences.h>

extern Preferences preferences;

// ── NVS persistence ─────────────────────────────────────────────────────────

void saveProbeSettings() {
    preferences.begin("probe", false);
    preferences.putInt  ("probeType",   pendantProbeV2.probeTypeIdx);
    preferences.putFloat("probeRate",   pendantProbeV2.probeRate);
    preferences.putFloat("seekRate",    pendantProbeV2.seekRate);
    preferences.putFloat("retractDist", pendantProbeV2.retractDist);
    preferences.putFloat("maxZTravel",  pendantProbeV2.maxZTravel);
    preferences.putFloat("ballDia",     pendantProbeV2.ballDia);
    preferences.putFloat("deflection",  pendantProbeV2.deflection);
    preferences.putFloat("plateThick",  pendantProbeV2.plateThick);
    preferences.putFloat("plateWidth",  pendantProbeV2.plateWidth);
    preferences.putFloat("plateOffX",   pendantProbeV2.plateOffX);
    preferences.putFloat("plateOffY",   pendantProbeV2.plateOffY);
    preferences.putInt  ("cornerIdx",   pendantProbeV2.cornerIdx);
    preferences.putInt  ("axesIdx",     pendantProbeV2.axesIdx);
    preferences.putFloat("cornerDepth", pendantProbeV2.cornerDepth);
    preferences.putFloat("cornerOver",  pendantProbeV2.cornerOver);
    preferences.putFloat("cornerRetXY", pendantProbeV2.cornerRetXY);
    preferences.putFloat("boreDia",     pendantProbeV2.boreDia);
    preferences.putFloat("boreDepth",   pendantProbeV2.boreDepth);
    preferences.putFloat("boreOffset",  pendantProbeV2.boreOffset);
    preferences.putInt  ("borePasses",  pendantProbeV2.borePasses);
    preferences.putFloat("bossDia",     pendantProbeV2.bossDia);
    preferences.putFloat("bossDepth",   pendantProbeV2.bossDepth);
    preferences.putFloat("bossClear",   pendantProbeV2.bossClear);
    preferences.putInt  ("bossPasses",  pendantProbeV2.bossPasses);
    preferences.putBool ("bossRect",    pendantProbeV2.bossRect);
    preferences.putFloat("bossSizeY",   pendantProbeV2.bossSizeY);
    preferences.end();
}

void loadProbeSettings() {
    preferences.begin("probe", true);
    pendantProbeV2.probeTypeIdx = preferences.getInt  ("probeType",   0);
    pendantProbeV2.probeRate    = preferences.getFloat("probeRate",   150.0f);
    pendantProbeV2.seekRate     = preferences.getFloat("seekRate",    500.0f);
    pendantProbeV2.retractDist  = preferences.getFloat("retractDist", 20.0f);
    pendantProbeV2.maxZTravel   = preferences.getFloat("maxZTravel",  20.0f);
    pendantProbeV2.ballDia      = preferences.getFloat("ballDia",      2.0f);
    pendantProbeV2.deflection   = preferences.getFloat("deflection",   0.0f);
    pendantProbeV2.plateThick   = preferences.getFloat("plateThick",  10.0f);
    pendantProbeV2.plateWidth   = preferences.getFloat("plateWidth",  50.0f);
    pendantProbeV2.plateOffX    = preferences.getFloat("plateOffX",    0.0f);
    pendantProbeV2.plateOffY    = preferences.getFloat("plateOffY",    0.0f);
    pendantProbeV2.cornerIdx    = preferences.getInt  ("cornerIdx",   0);
    pendantProbeV2.axesIdx      = preferences.getInt  ("axesIdx",     0);
    pendantProbeV2.cornerDepth  = preferences.getFloat("cornerDepth",  5.0f);
    pendantProbeV2.cornerOver   = preferences.getFloat("cornerOver",   2.0f);
    pendantProbeV2.cornerRetXY  = preferences.getFloat("cornerRetXY",  3.0f);
    pendantProbeV2.boreDia      = preferences.getFloat("boreDia",     60.0f);
    pendantProbeV2.boreDepth    = preferences.getFloat("boreDepth",    8.0f);
    pendantProbeV2.boreOffset   = preferences.getFloat("boreOffset",   5.0f);
    pendantProbeV2.borePasses   = preferences.getInt  ("borePasses",  2);
    pendantProbeV2.bossDia      = preferences.getFloat("bossDia",     60.0f);
    pendantProbeV2.bossDepth    = preferences.getFloat("bossDepth",    5.0f);
    pendantProbeV2.bossClear    = preferences.getFloat("bossClear",    5.0f);
    pendantProbeV2.bossPasses   = preferences.getInt  ("bossPasses",  2);
    pendantProbeV2.bossRect     = preferences.getBool ("bossRect",    false);
    pendantProbeV2.bossSizeY    = preferences.getFloat("bossSizeY",   60.0f);
    preferences.end();

    // Clamp integral fields
    pendantProbeV2.probeTypeIdx = constrain(pendantProbeV2.probeTypeIdx, 0, PROBE_TYPE_COUNT - 1);
    pendantProbeV2.cornerIdx    = constrain(pendantProbeV2.cornerIdx,    0, 3);
    pendantProbeV2.axesIdx      = constrain(pendantProbeV2.axesIdx,      0, 2);
    pendantProbeV2.borePasses   = constrain(pendantProbeV2.borePasses,   1, 4);
    pendantProbeV2.bossPasses   = constrain(pendantProbeV2.bossPasses,   1, 4);
}

// ── Shared drawing helpers ───────────────────────────────────────────────────

// Position panel: shows live X Y Z work coordinates.
// Rendered into a sprite then pushed as one write — eliminates flicker on live updates.
// h >= 38: two-row textSize=2 layout (axis labels row + values row).
// h <  38: original single-row textSize=1 layout.
void probeDrawPosPanel(int y, int h) {
    float px, py, pz;
    bool  inInch;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    px = pendantMachine.posX; py = pendantMachine.posY; pz = pendantMachine.posZ;
    inInch = pendantMachine.inInches;
    xSemaphoreGive(stateMutex);

    const char* uu  = inInch ? "in" : "mm";
    char buf[12];

    // Shared 16-bit scratch panel (true colour for PROBE_BG_PANEL; direct-draw
    // fallback at (5, y) if it can't allocate — never blank).
    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, h, ox, oy, 5, y);
    g->fillRoundRect(ox, oy, 230, h, 4, PROBE_BG_PANEL);

    if (h >= 38) {
        // ── Large layout: textSize=2, two rows ───────────────────────────
        // Columns: X=5, Y=80, Z=155  (75px each, fits 6 chars × 12px)
        const int   cols[3]     = { 5, 80, 155 };
        const float vals[3]     = { px, py, pz };
        const char* axLabels[3] = { "X", "Y", "Z" };
        const char* fmt = inInch ? "%.3f" : "%.1f";

        // Row 1 — axis labels (green) + unit hint (dim, top-right)
        g->setTextSize(2);
        for (int i = 0; i < 3; i++) {
            g->setTextColor(PROBE_C_GREEN);
            g->setCursor(ox + cols[i], oy + 3);
            g->print(axLabels[i]);
        }
        g->setTextSize(1);
        g->setTextColor(PROBE_C_DIMBLUE);
        g->setCursor(ox + 212, oy + 3);
        g->print(uu);

        // Row 2 — position values (yellow)
        g->setTextSize(2);
        g->setTextColor(PROBE_C_YELLOW);
        for (int i = 0; i < 3; i++) {
            snprintf(buf, sizeof(buf), fmt, vals[i]);
            g->setCursor(ox + cols[i], oy + 20);
            g->print(buf);
        }
    } else {
        // ── Compact layout: textSize=1, single row ────────────────────────
        g->setTextSize(1);
        g->setTextColor(PROBE_C_LBLUE);
        g->setCursor(ox + 5, oy + 3);
        g->print("CURRENT POSITION");

        const char* fmt = inInch ? "%.4f" : "%.2f";

        g->setTextColor(PROBE_C_GREEN);
        g->setCursor(ox + 5, oy + 14);
        g->print("X");
        g->setTextColor(PROBE_C_YELLOW);
        snprintf(buf, sizeof(buf), fmt, px);
        g->setCursor(ox + 13, oy + 14);
        g->print(buf);

        g->setTextColor(PROBE_C_GREEN);
        g->setCursor(ox + 83, oy + 14);
        g->print("Y");
        g->setTextColor(PROBE_C_YELLOW);
        snprintf(buf, sizeof(buf), fmt, py);
        g->setCursor(ox + 91, oy + 14);
        g->print(buf);

        g->setTextColor(PROBE_C_GREEN);
        g->setCursor(ox + 161, oy + 14);
        g->print("Z");
        g->setTextColor(PROBE_C_YELLOW);
        snprintf(buf, sizeof(buf), fmt, pz);
        g->setCursor(ox + 169, oy + 14);
        g->print(buf);

        g->setTextColor(PROBE_C_DIMBLUE);
        g->setCursor(ox + 215, oy + 18);
        g->print(uu);
    }

    endPanelSprite(230, h, 5, y);
}

// Settings link bar — tap to navigate to SCR0 (shared settings)
void probeDrawSettingsLink(int y) {
    // "Back" — returns to the probe hub (SCR0).  Unified label across all
    // routine + config screens (was "Settings"/"Setup").
    drawButton(5, y, 230, 24, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
}

// Focused-field selection bar — drawn at bottom of the settings region
void probeDrawSelBar(int y, const char* fieldName, float value, const char* unit, int decimals) {
    display.fillRoundRect(5, y, 230, 20, 3, PROBE_SEL_BG);
    display.drawRoundRect(5, y, 230, 20, 3, PROBE_C_YELLOW);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, y + 6);
    display.print(fieldName);

    char vbuf[16];
    if (decimals == 0)      snprintf(vbuf, sizeof(vbuf), "%.0f", value);
    else if (decimals == 1) snprintf(vbuf, sizeof(vbuf), "%.1f", value);
    else if (decimals == 2) snprintf(vbuf, sizeof(vbuf), "%.2f", value);
    else                    snprintf(vbuf, sizeof(vbuf), "%.3f", value);

    display.setTextSize(2);
    display.setTextColor(PROBE_C_YELLOW);
    int16_t vw = display.textWidth(vbuf);
    display.setCursor(215 - vw - display.textWidth(unit), y + 3);
    display.print(vbuf);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_DIMBLUE);
    display.setCursor(218 - display.textWidth(unit), y + 8);
    display.print(unit);
}

void probeDrawSelBarInt(int y, const char* fieldName, int value) {
    display.fillRoundRect(5, y, 230, 20, 3, PROBE_SEL_BG);
    display.drawRoundRect(5, y, 230, 20, 3, PROBE_C_YELLOW);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, y + 6);
    display.print(fieldName);
    display.setTextSize(2);
    display.setTextColor(PROBE_C_YELLOW);
    char vbuf[8];
    snprintf(vbuf, sizeof(vbuf), "%d", value);
    int16_t vw = display.textWidth(vbuf);
    display.setCursor(228 - vw, y + 3);
    display.print(vbuf);
}

// A single KV-touch row inside a panel.
// x,y,w,h define the hit rect.  focused=true adds yellow border.
void probeDrawKVTouch(int x, int y, int w, int h,
                      const char* label, float value, const char* unit,
                      uint16_t valColor, bool focused, int decimals) {
    uint16_t bg  = focused ? PROBE_SEL_BG : PROBE_BG_SCREEN;
    uint16_t bdr = focused ? PROBE_C_YELLOW : PROBE_C_TAPBDR;
    display.fillRoundRect(x, y, w, h, 2, bg);
    display.drawRoundRect(x, y, w, h, 2, bdr);
    display.setTextSize(1);
    display.setTextColor(focused ? COLOR_WHITE : PROBE_C_LBLUE);
    display.setCursor(x + 3, y + 2);
    display.print(label);

    char vbuf[14];
    if (decimals == 0)      snprintf(vbuf, sizeof(vbuf), "%.0f", value);
    else if (decimals == 1) snprintf(vbuf, sizeof(vbuf), "%.1f", value);
    else if (decimals == 2) snprintf(vbuf, sizeof(vbuf), "%.2f", value);
    else                    snprintf(vbuf, sizeof(vbuf), "%.3f", value);

    display.setTextSize(2);
    display.setTextColor(focused ? PROBE_C_YELLOW : valColor);
    display.setCursor(x + 3, y + 11);
    display.print(vbuf);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_DIMBLUE);
    int16_t vw = display.textWidth(vbuf) * 2;
    display.setCursor(x + 3 + vw + 1, y + 14);
    display.print(unit);
}

void probeDrawKVTouchInt(int x, int y, int w, int h,
                         const char* label, int value,
                         uint16_t valColor, bool focused) {
    uint16_t bg  = focused ? PROBE_SEL_BG : PROBE_BG_SCREEN;
    uint16_t bdr = focused ? PROBE_C_YELLOW : PROBE_C_TAPBDR;
    display.fillRoundRect(x, y, w, h, 2, bg);
    display.drawRoundRect(x, y, w, h, 2, bdr);
    display.setTextSize(1);
    display.setTextColor(focused ? COLOR_WHITE : PROBE_C_LBLUE);
    display.setCursor(x + 3, y + 2);
    display.print(label);
    display.setTextSize(2);
    display.setTextColor(focused ? PROBE_C_YELLOW : valColor);
    char vbuf[8];
    snprintf(vbuf, sizeof(vbuf), "%d", value);
    display.setCursor(x + 3, y + 11);
    display.print(vbuf);
}

void probeDrawWarn(int y, const char* msg, bool isRed, int h) {
    uint16_t bg  = isRed ? PROBE_WARNR_BG  : PROBE_WARN_BG;
    uint16_t bdr = isRed ? PROBE_WARNR_BDR : PROBE_WARN_BDR;
    uint16_t fg  = isRed ? PROBE_C_RED     : PROBE_AMBER;
    display.fillRoundRect(5, y, 230, h, 3, bg);
    display.drawRoundRect(5, y, 230, h, 3, bdr);
    display.setTextSize(1);
    display.setTextColor(fg);
    display.setCursor(10, y + (h - 8) / 2);
    display.print(msg);
}

// Confirm overlay — drawn over the current screen when user taps a PROBE button.
void probeDrawConfirmOverlay(const char* routineName) {
    // Semi-transparent dark panel
    display.fillRoundRect(20, 100, 200, 120, 8, PROBE_BG_PANEL);
    display.drawRoundRect(20, 100, 200, 120, 8, PROBE_C_YELLOW);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_YELLOW);
    display.setCursor(30, 110);
    display.print("CONFIRM PROBE?");
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    int16_t tw = display.textWidth(routineName);
    display.setCursor(120 - tw / 2, 126);
    display.print(routineName);
    // Buttons
    display.fillRoundRect(28,  175, 78, 32, 5, COLOR_BUTTON_GRAY);  // CANCEL
    display.fillRoundRect(114, 175, 98, 32, 5, PROBE_BTN_GREEN);  // CONFIRM
    display.setTextSize(2);
    display.setTextColor(COLOR_WHITE);
    display.setCursor(36, 183);
    display.print("CANCEL");
    int16_t cw = display.textWidth("CONFIRM");
    display.setCursor(114 + (98 - cw) / 2, 183);
    display.print("CONFIRM");
}

// Dial acceleration — returns effective step multiplier.
// Accelerates to 10× after 5 rapid same-direction detents within 500 ms.
float probeDialStep(int delta, float baseStep) {
    unsigned long now = millis();
    if ((now - pendantProbeV2.dialLastMs) < 500) {
        pendantProbeV2.dialAccelCount++;
    } else {
        pendantProbeV2.dialAccelCount = 0;
    }
    pendantProbeV2.dialLastMs = now;
    float mult = (pendantProbeV2.dialAccelCount >= 5) ? 10.0f : 1.0f;
    return baseStep * mult;
}

// Crash-safe two-pass approach: fast seek to contact, back off, slow re-probe.
// Ends AT the fine trigger so the caller can set a WCS axis there.  Stays G91.
void probeSeekFine(const char* axis, float seekDist, float seekF, float fineF) {
    const float BACKOFF = 1.5f;
    int  dir = (seekDist >= 0.0f) ? 1 : -1;
    char b[96];
    snprintf(b, sizeof(b), "G38.2 %s%.3f F%.0f", axis, seekDist, seekF);                send_line(b);
    snprintf(b, sizeof(b), "G0 %s%.3f F1000",    axis, -dir * BACKOFF);                 send_line(b);
    snprintf(b, sizeof(b), "G38.2 %s%.3f F%.0f", axis, dir * (BACKOFF + 1.0f), fineF);  send_line(b);
}

// Work-area selector button — styled like the Z-Surface "Sets" box (rounded
// rect, small label over the WCS).  Shared by every routine screen so the WCS
// the probe zeroes can be cycled from each one.
void probeDrawWorkAreaButton(int x, int y, int w, int h) {
    display.fillRoundRect(x, y, w, h, 8, PROBE_BG_SCREEN);
    display.drawRoundRect(x, y, w, h, 8, PROBE_C_TAPBDR);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    const char* lbl = "WORK AREA";
    int16_t lw = display.textWidth(lbl);
    display.setCursor(x + (w - lw) / 2, y + 5);
    display.print(lbl);
    display.setTextSize(2);
    display.setTextColor(PROBE_C_BLUE);
    const char* v = pendantProbing.selectedCoordSystem.c_str();
    int16_t vw = display.textWidth(v);
    display.setCursor(x + (w - vw) / 2, y + 17);
    display.print(v);
}

// Cycle G54 → G55 → G56 → G57 → G54.  Selection only — the button doesn't switch
// the active WCS (no surprise DRO jumps while choosing); the probe routine
// activates and zeroes the chosen system when you actually probe.
void probeCycleWorkArea() {
    static const char* coords[] = { "G54", "G55", "G56", "G57" };
    pendantProbing.selectedCoordIndex  = (pendantProbing.selectedCoordIndex + 1) % 4;
    pendantProbing.selectedCoordSystem = coords[pendantProbing.selectedCoordIndex];
}

// Activate the selected WCS on the controller so the probe's G10 L20 zero is
// immediately in effect in the system shown on screen.
void probeActivateWcs() {
    static const char* coords[] = { "G54", "G55", "G56", "G57" };
    send_line(coords[pendantProbing.selectedCoordIndex]);
}

// Sequence-step badge: filled numbered circle + label beside it.
void drawSeqStep(int x, int y, int num, const char* txt, bool active) {
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

// ── SCR0 screen lifecycle ────────────────────────────────────────────────────

void enterProbe() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
    pendantProbeV2.focusedField  = -1;
    pendantProbeV2.confirmActive = false;
    pendantProbeV2.dialAccelCount = 0;
}

void exitProbe() {
    // nothing to tear down — no sprites on SCR0
}

// ── SCR0 draw ────────────────────────────────────────────────────────────────

// Layout constants (px, 240×320)
// drawTitle    y=0    h=35
// Type row     y=38   h=40   (3 segmented type buttons — direct tap, no cycle)
// Shared KV    y=82   h=84   (4 fields in 2×2 grid)
// Routines     y=170  h=104  (1, 2 or 4 buttons, gated by probe type)
// Bottom row   y=280  h=38   (Main Menu | Configure, two equal halves)

// Short labels for the three segmented type buttons.
static const char* kProbeTypeLabels[PROBE_TYPE_COUNT] = { "Z Plate", "XYZ Plate", "3D Probe" };
static const int   kProbeTypeX[PROBE_TYPE_COUNT]      = { 5, 82, 159 };
static const int   kProbeTypeW[PROBE_TYPE_COUNT]      = { 74, 74, 76 };

static void drawProbeTypeRow() {
    // Three big segmented buttons — tap the one you want (no cycling).  The
    // selected type is highlighted; the others are gray.
    for (int i = 0; i < PROBE_TYPE_COUNT; i++) {
        bool sel = (pendantProbeV2.probeTypeIdx == i);
        drawButton(kProbeTypeX[i], 38, kProbeTypeW[i], 40, kProbeTypeLabels[i],
                   sel ? PROBE_C_YELLOW : (uint16_t)COLOR_BUTTON_GRAY, COLOR_WHITE, 1);
    }
}

static void drawSharedKVPanel() {
    display.fillRoundRect(5, 82, 230, 84, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 84);
    display.print("SHARED SETTINGS");

    // 2×2 grid: probe rate | seek rate
    //           retract    | max Z travel
    int fo = pendantProbeV2.focusedField;
    probeDrawKVTouch( 7,  94, 112, 33, "Probe rate", pendantProbeV2.probeRate,   "mm/m", PROBE_C_BLUE, fo==0, 0);
    probeDrawKVTouch(122,  94, 111, 33, "Seek rate", pendantProbeV2.seekRate,    "mm/m", PROBE_C_BLUE, fo==1, 0);
    probeDrawKVTouch( 7, 130, 112, 33, "Retract",   pendantProbeV2.retractDist, "mm",   PROBE_C_BLUE,  fo==2, 3);
    probeDrawKVTouch(122, 130, 111, 33, "Max Z trvl",pendantProbeV2.maxZTravel,  "mm",   PROBE_C_BLUE,   fo==3, 3);
}

static void drawRoutineButtons() {
    display.fillRoundRect(5, 170, 230, 104, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, 172);
    display.print("PROBE ROUTINES");

    // Routines available depend on the probe type:
    //   Z-Height Plate → Z Surface only
    //   XYZ Plate      → Z Surface, XYZ Corner
    //   3D Probe       → all four
    int n = probeRoutineCount();
    if (n == 1) {
        drawButton(7, 186, 226, 40, "Z Surface", PROBE_BTN_GREEN, COLOR_WHITE, 2);
    } else if (n == 2) {
        // XYZ Plate — two full-width buttons, one per row.
        drawButton(7, 186, 226, 40, "Z Surface",  PROBE_BTN_GREEN,  COLOR_WHITE, 2);
        drawButton(7, 230, 226, 40, "XYZ Corner", PROBE_BTN_YELLOW, COLOR_WHITE, 2);
    } else {  // n == 4 (3D probe) — 2×2 grid
        drawButton(  7, 186, 112, 40, "Z Surf",  PROBE_BTN_GREEN,  COLOR_WHITE, 2);
        drawButton(121, 186, 112, 40, "XYZ Cnr", PROBE_BTN_YELLOW, COLOR_WHITE, 2);
        drawButton(  7, 230, 112, 40, "Bore", PROBE_BTN_BLUE,   COLOR_WHITE, 2);
        drawButton(121, 230, 112, 40, "Boss", (uint16_t)0x8010, COLOR_WHITE, 2);
    }
}

void drawProbeScreen() {
    display.fillScreen(PROBE_BG_SCREEN);
    drawTitle("PROBE");
    drawProbeTypeRow();
    drawSharedKVPanel();
    drawRoutineButtons();
    // Bottom row — two equal halves: Main Menu | Configure (opens the config
    // screen for the selected probe type).
    drawButton(  5, 280, 112, 38, "Main Menu", PROBE_BTN_BLUE, COLOR_WHITE, 2);
    drawButton(123, 280, 112, 38, "Configure", PROBE_BTN_TEAL, COLOR_WHITE, 2);
}

void updateProbeScreen() {
    // nothing to live-update on SCR0
}

// ── SCR0 touch ───────────────────────────────────────────────────────────────

void handleProbeTouch(int x, int y) {
    // Bottom row — Main Menu (left half) | Configure (right half)
    if (isTouchInBounds(x, y, 5, 280, 112, 38)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
        return;
    }
    if (isTouchInBounds(x, y, 123, 280, 112, 38)) {
        pendantProbeV2.returnScreen = PSCREEN_PROBE;
        currentPendantScreen = probeIs3D() ? PSCREEN_PROBE_CFG_3D : PSCREEN_PROBE_CFG_PLATE;
        return;
    }

    // Type segmented buttons — tap to select (re-gates the routine list).
    for (int i = 0; i < PROBE_TYPE_COUNT; i++) {
        if (isTouchInBounds(x, y, kProbeTypeX[i], 38, kProbeTypeW[i], 40)) {
            if (pendantProbeV2.probeTypeIdx != i) {
                pendantProbeV2.probeTypeIdx = i;
                drawProbeScreen();   // type change → re-gate routines (full redraw)
            }
            return;
        }
    }

    // Shared KV fields — tap to focus
    bool redraw = false;
    if (isTouchInBounds(x, y,  7,  94, 112, 33)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 0) ? -1 : 0; redraw = true; }
    if (isTouchInBounds(x, y, 122,  94, 111, 33)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 1) ? -1 : 1; redraw = true; }
    if (isTouchInBounds(x, y,  7, 130, 112, 33))  { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 2) ? -1 : 2; redraw = true; }
    if (isTouchInBounds(x, y, 122, 130, 111, 33)) { pendantProbeV2.focusedField = (pendantProbeV2.focusedField == 3) ? -1 : 3; redraw = true; }
    if (redraw) { drawSharedKVPanel(); return; }

    // Routine buttons (only the ones the current type exposes) → routine screen
    int n = probeRoutineCount();
    if (n == 1) {
        if (isTouchInBounds(x, y, 7, 186, 226, 40)) { currentPendantScreen = PSCREEN_PROBE_Z; return; }
    } else if (n == 2) {
        if (isTouchInBounds(x, y, 7, 186, 226, 40)) { currentPendantScreen = PSCREEN_PROBE_Z;      return; }
        if (isTouchInBounds(x, y, 7, 230, 226, 40)) { currentPendantScreen = PSCREEN_PROBE_CORNER; return; }
    } else {  // n == 4
        if (isTouchInBounds(x, y,   7, 186, 112, 40)) { currentPendantScreen = PSCREEN_PROBE_Z;      return; }
        if (isTouchInBounds(x, y, 121, 186, 112, 40)) { currentPendantScreen = PSCREEN_PROBE_CORNER; return; }
        if (isTouchInBounds(x, y,   7, 230, 112, 40)) { currentPendantScreen = PSCREEN_PROBE_BORE; return; }
        if (isTouchInBounds(x, y, 121, 230, 112, 40)) { currentPendantScreen = PSCREEN_PROBE_BOSS; return; }
    }

    // Tap outside all fields — clear focus
    if (y >= 82 && y <= 166) {
        pendantProbeV2.focusedField = -1;
        drawSharedKVPanel();
    }
}
