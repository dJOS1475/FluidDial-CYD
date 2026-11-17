#include "pendant_shared.h"
#include "screen_jog_homing.h"
#include "screen_probe.h"   // PROBE_* colours

// ===== Increment sets =====
struct IncrementSet {
    const char* labels[4];
    float       values[4];
};

// Slots 0-2 are fixed fine increments; slot 3 is the dial box, holding whichever
// coarse value pendantJog.coarseIdx selects.  Imperial mirrors metric one-for-one
// in real terms (.001"≈0.01mm … 4.0"≈100mm) using round inch numbers, so the row
// behaves identically in either unit.
static const char* kCoarseLabelsMm[JOG_COARSE_COUNT] = { "10", "50", "100" };
static const float kCoarseValuesMm[JOG_COARSE_COUNT] = { 10.0f, 50.0f, 100.0f };
static const char* kCoarseLabelsIn[JOG_COARSE_COUNT] = { ".5", "2.0", "4.0" };
static const float kCoarseValuesIn[JOG_COARSE_COUNT] = { 0.5f, 2.0f, 4.0f };

static IncrementSet currentIncrements() {
    IncrementSet s;
    int ci = constrain(pendantJog.coarseIdx, 0, JOG_COARSE_COUNT - 1);
    if (pendantMachine.inInches) {
        // fine imperial: .001 .010 .100  (≈ 0.025 / 0.25 / 2.5 mm)
        s.labels[0]=".001";  s.labels[1]=".010"; s.labels[2]=".100";
        s.values[0]=0.001f;  s.values[1]=0.010f; s.values[2]=0.100f;
        s.labels[3]=kCoarseLabelsIn[ci];
        s.values[3]=kCoarseValuesIn[ci];
    } else {
        // fine metric: 0.01 0.1 1
        s.labels[0]="0.01"; s.labels[1]="0.1"; s.labels[2]="1";
        s.values[0]=0.01f;  s.values[1]=0.1f;  s.values[2]=1.0f;
        s.labels[3]=kCoarseLabelsMm[ci];
        s.values[3]=kCoarseValuesMm[ci];
    }
    return s;
}

void jogApplyCoarseIncrement() {
    if (pendantJog.selectedIncrement != 3) return;
    IncrementSet incs = currentIncrements();
    pendantJog.increment = incs.values[3];
}

// ===== Layout constants =====
// Top to bottom: MPos/WPos panel, HOME, JOG AXIS, JOG INCREMENT (each under a
// small section label), then Main Menu | SPEED | Work Area.
static const int PANEL_Y = 38;
static const int PANEL_H = 52;

// Bottom row: Main Menu | Speed | Work Area  (x=5,w=70 | x=78,w=84 | x=165,w=70)
static const int MM_X  = 5;
static const int MM_W  = 70;
static const int SPD_X = 78;
static const int SPD_W = 84;
static const int WA_X  = 165;
static const int WA_W  = 70;
static const int SPD_Y = 266;
static const int SPD_H = 50;

// Button rows use the shared rowBtnX()/rowBtnWAt() grid from pendant_shared.h, so
// HOME, JOG AXIS and the increment row all span 5..235 and align with each other.
static const int ROW_H   = 36;
static const int HOME_Y  = 110;
static const int AXIS_Y  = 166;
static const int AXIS_TS = 4;    // axis letters: FreeSansBold 18pt
static const int ALL_TS  = 3;    // "ALL": 12pt (18pt won't fit the 56 px slot)

// Increment row: 4 slots on the shared grid — rowBtnW(4) == 56, pitch 58.
static const int INC_Y = 222;
static const int INC_H = 38;
static const int INC_W = 56;
static inline int incX(int i) { return rowBtnX(4, i); }

// Axis to restore when the coarse dial box hands the encoder back to jogging.
static int incDialPrevAxis = 0;

// One run of a value line: a font, text and colour sharing one baseline.
struct ValPart { const lgfx::IFont* font; const char* text; uint16_t color; };

// A small Font2 caption over a value, the pair centred (caps to caps) on cy and
// each line centred across [x, x+w).  The value is a run of parts 3 px apart on
// one baseline — e.g. "5000" in bold with "mm/m" in Font2.
static void captionValue(LovyanGFX* g, int x, int w, float cy, const char* caption, uint16_t capColor,
                         const ValPart* parts, int n) {
    const int GAP = 5;
    int ca, cb, va, vb;
    uiFont2(g);
    uiCapBox(g, ca, cb);
    g->setFont(parts[0].font);
    uiCapBox(g, va, vb);
    int blockH    = (cb - ca) + GAP + (vb - va);
    int capTop    = (int)lroundf(cy - blockH / 2.0f);
    int valCapTop = capTop + (cb - ca) + GAP;

    uiFont2(g);
    g->setTextColor(capColor);
    g->setCursor(x + (w - g->textWidth(caption)) / 2, capTop - ca);
    g->print(caption);

    int baseline = valCapTop + (vb - va);
    int total    = 3 * (n - 1);
    for (int i = 0; i < n; ++i) { g->setFont(parts[i].font); total += g->textWidth(parts[i].text); }
    int px = x + (w - total) / 2;
    for (int i = 0; i < n; ++i) {
        int a, b;
        g->setFont(parts[i].font);
        g->setTextColor(parts[i].color);
        uiCapBox(g, a, b);
        g->setCursor(px, baseline - b);
        g->print(parts[i].text);
        px += g->textWidth(parts[i].text) + 3;
    }
    uiText(g, 1);
}

// Slot 3 is the coarse dial box: a button with a small "DIAL" caption over the
// coarse value — blue, or yellow when selected — and an inset field with a
// yellow edge while the encoder is stepping it (incDialMode), matching every
// other dial-driven field in the UI.
static void drawIncDialButton() {
    bool selected = (pendantJog.selectedIncrement == 3);
    bool active   = pendantJog.incDialMode;
    IncrementSet incs = currentIncrements();
    int x = incX(3);
    uint16_t fg;
    if (active) {
        display.fillRoundRect(x, INC_Y, INC_W, INC_H, 7, UI_FIELD_BG);
        display.drawRoundRect(x, INC_Y, INC_W, INC_H, 7, UI_BTN_YELLOW);
        display.drawRoundRect(x + 1, INC_Y + 1, INC_W - 2, INC_H - 2, 6, UI_BTN_YELLOW);
        fg = UI_BTN_YELLOW;
    } else {
        UiBtnColors c = uiButtonColors(selected ? COLOR_ORANGE : COLOR_BUTTON_GRAY, COLOR_WHITE);
        uiButtonFrame(&display, x, INC_Y, INC_W, INC_H, c);
        fg = c.text;
    }
    ValPart v[1] = { { &fonts::FreeSansBold9pt7b, incs.labels[3], fg } };
    captionValue(&display, x, INC_W, INC_Y + INC_H / 2.0f, "DIAL", fg, v, 1);
}

// ===== Helpers =====

// SPEED box: an inset field with a "SPEED" caption over the value in bold
// green with its unit in Font2; yellow edge while the encoder is setting it.
void redrawJogSpeedButton() {
    bool  active = pendantJog.speedDialMode;
    char  spd[12];
    snprintf(spd, sizeof(spd), "%d", pendantMachine.inInches ? (int)pendantJog.jogSpeedIn
                                                             : (int)pendantJog.jogSpeedMm);
    const char* unit = pendantMachine.inInches ? "ipm" : "mm/m";
    display.fillRect(SPD_X, SPD_Y, SPD_W, SPD_H, COLOR_BACKGROUND);
    display.fillRoundRect(SPD_X, SPD_Y, SPD_W, SPD_H, 7, UI_FIELD_BG);
    display.drawRoundRect(SPD_X, SPD_Y, SPD_W, SPD_H, 7, active ? UI_BTN_YELLOW : UI_EDGE_LT);
    display.drawRoundRect(SPD_X + 1, SPD_Y + 1, SPD_W - 2, SPD_H - 2, 6, active ? UI_BTN_YELLOW : UI_EDGE);
    uint16_t vc = active ? UI_BTN_YELLOW : COLOR_GREEN;
    ValPart v[2] = { { &fonts::FreeSansBold9pt7b, spd, vc }, { &fonts::Font2, unit, vc } };
    captionValue(&display, SPD_X, SPD_W, SPD_Y + SPD_H / 2.0f, "SPEED",
                 active ? UI_BTN_YELLOW : COLOR_WHITE, v, 2);
}

// ===== Lifecycle =====

void enterJogHoming() {
    releasePanelSprites();

    // Re-apply the current increment in case units changed since the last visit
    {
        IncrementSet incs = currentIncrements();
        pendantJog.increment = incs.values[pendantJog.selectedIncrement];
    }

    // Ensure a valid axis is selected on entry — leave either dial mode if active
    if (pendantJog.speedDialMode || pendantJog.incDialMode || pendantJog.selectedAxis < 0) {
        pendantJog.speedDialMode = false;
        pendantJog.incDialMode   = false;
        pendantJog.selectedAxis  = 0;
    }

    // $110/$130-$133 are cached on connect — no UART query here.

    // The DRO panel uses the shared 16-bit scratch (see updateJogAxisDisplay); the
    // direct-draw fallback keeps it from ever being blank under heap pressure.
    releasePanelSprites();
}

void exitJogHoming() {
    releasePanelSprites();
}

// ===== Draw =====

// Section label: GLCD size 1 in the label colour, 4 px above its row — the same
// treatment every other screen uses.  Returns the x just past the text.
static int sectionLabel(int rowY, const char* text, uint16_t color = COLOR_GRAY_TEXT) {
    int y = rowY - 12;
    display.fillRect(5, y - 1, 230, 10, COLOR_BACKGROUND);
    uiText(&display, 1);
    display.setTextColor(color);
    display.setCursor(5, y);
    display.print(text);
    return 5 + display.textWidth(text);
}

// Redraws just the "JOG INCREMENT (unit)" label row.  Pulled out so it can
// refresh when the selected axis changes (A → "deg", X/Y/Z → "mm"/"in")
// without a full-screen redraw.  In Alarm it turns red and names the alarm.
static void redrawJogIncrementLabel() {
    if (pendantMachine.status.startsWith("Alarm")) {
        int end = sectionLabel(INC_Y, "JOG INCREMENT", TFT_RED);
        display.setCursor(end + 6, INC_Y - 12);
        display.print("*** " + pendantMachine.status + " ***");
    } else {
        // A axis (rotary) → label the increments in degrees, not mm/in.
        const char* unitStr = (pendantJog.selectedAxis == 3) ? "deg"
                                                             : (pendantMachine.inInches ? "in" : "mm");
        char buf[32];
        snprintf(buf, sizeof(buf), "JOG INCREMENT (%s)", unitStr);
        sectionLabel(INC_Y, buf);
    }
}

static void drawHomeButton(int i, bool pressed) {
    const int   numAx   = pendantMachine.numAxes;
    const int   numHome = (numAx < 4) ? numAx + 1 : numAx;
    const bool  isAll   = (numAx < 4) && (i == numAx);
    drawButton(rowBtnX(numHome, i), HOME_Y, rowBtnWAt(numHome, i), ROW_H,
               isAll ? "ALL" : AXIS_NAMES[i],
               pressed ? COLOR_WHITE : COLOR_DARK_GREEN, pressed ? COLOR_DARK_GREEN : COLOR_WHITE,
               isAll ? ALL_TS : AXIS_TS);
}

void drawJogHomingScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("JOG & HOMING");

    updateJogAxisDisplay();   // draws the framed MPos/WPos panel

    int numAx = pendantMachine.numAxes;

    // One HOME button per reported axis, plus "ALL" when there is room for it.
    sectionLabel(HOME_Y, "HOME");
    {
        const int numHome = (numAx < 4) ? numAx + 1 : numAx;
        for (int i = 0; i < numHome; i++) drawHomeButton(i, false);
    }

    sectionLabel(AXIS_Y, "JOG AXIS");
    for (int i = 0; i < numAx; i++) {
        // Deselect all axis buttons when in speed dial mode
        uint16_t bg = (!pendantJog.speedDialMode && i == pendantJog.selectedAxis)
                      ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(rowBtnX(numAx, i), AXIS_Y, rowBtnWAt(numAx, i), ROW_H,
                   AXIS_NAMES[i], bg, COLOR_WHITE, AXIS_TS);
    }

    redrawJogIncrementLabel();
    {
        IncrementSet incs = currentIncrements();
        for (int i = 0; i < 3; i++) {
            uint16_t bg = (i == pendantJog.selectedIncrement) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
            drawButton(incX(i), INC_Y, INC_W, INC_H, incs.labels[i], bg, COLOR_WHITE, 2);
        }
        drawIncDialButton();   // slot 3 — coarse dial box
    }

    // Bottom row: Main Menu | Speed | Work Area.  Two-line bold labels — one line
    // won't fit 70 px in FreeSansBold.
    drawMultiLineButton(MM_X, SPD_Y, MM_W, SPD_H, "Main", "Menu", COLOR_BLUE, COLOR_WHITE, 2);
    redrawJogSpeedButton();
    drawMultiLineButton(WA_X, SPD_Y, WA_W, SPD_H, "Work", "Area", COLOR_BLUE, COLOR_WHITE, 2);
}

// ===== Sprite update =====

void updateJogAxisDisplay() {
    if (currentPendantScreen != PSCREEN_JOG_HOMING) return;

    // Snapshot under the lock; skip the frame if briefly held.  Panel is
    // 230 x 52, pushed at (5, 38); shared 16-bit scratch, direct-draw fallback.
    float mpos[PENDANT_MAX_AXES], wpos[PENDANT_MAX_AXES];
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    for (int i = 0; i < PENDANT_MAX_AXES; i++) {
        mpos[i] = pendantMachine.mpos[i];
        wpos[i] = pendantMachine.wpos[i];
    }
    xSemaphoreGive(stateMutex);

    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, PANEL_H, ox, oy, 5, PANEL_Y);
    uiPanel(g, ox, oy, 230, PANEL_H, 6);
    const int cx = ox + 115;

    if (pendantJog.speedDialMode || pendantJog.incDialMode) {
        // Dial modes take the panel over so it is never ambiguous what the knob
        // is driving: caption + hint on one line, the value large beneath.
        char cap[48], val[24];
        uint16_t col;
        if (pendantJog.speedDialMode) {
            col = COLOR_GREEN;
            snprintf(cap, sizeof(cap), "JOG SPEED  -  tap an axis to jog");
            if (pendantMachine.inInches) snprintf(val, sizeof(val), "%d ipm", (int)pendantJog.jogSpeedIn);
            else                         snprintf(val, sizeof(val), "%d mm/min", (int)pendantJog.jogSpeedMm);
        } else {
            const char* unit = (pendantJog.selectedAxis == 3) ? "deg" : (pendantMachine.inInches ? "in" : "mm");
            col = UI_BTN_YELLOW;
            snprintf(cap, sizeof(cap), "JOG INCREMENT  -  tap DIAL to jog");
            snprintf(val, sizeof(val), "%s %s", currentIncrements().labels[3], unit);
        }
        g->setTextColor(col);
        uiFont2(g);
        g->setCursor(cx - g->textWidth(cap) / 2, oy + 4);
        g->print(cap);
        uiText(g, 3);
        g->setCursor(cx - g->textWidth(val) / 2, oy + 23);
        g->print(val);
        uiText(g, 1);
        endPanelSprite(230, PANEL_H, 5, PANEL_Y);
        return;
    }

    // MPos (yellow caption) over WPos (light blue), every axis in columns; the
    // axis the knob moves — or the one being homed — is green.
    const int numAx = pendantMachine.numAxes;
    const int dec   = pendantMachine.inInches ? 3 : 2;
    const int hot   = (pendantJog.homingAxis >= 0) ? pendantJog.homingAxis : pendantJog.selectedAxis;
    const int COL0  = ox + 42, PITCH = 61;
    const char*     caps[2]    = { "MPos", "WPos" };
    const uint16_t  capCols[2] = { COLOR_ORANGE, COLOR_GRAY_TEXT };
    const float*    rows[2]    = { mpos, wpos };
    char buf[20], v[12];

    for (int r = 0; r < 2; r++) {
        if (numAx <= 3) {
            // One Font2 row per coordinate system; bold Font2 caption.
            int y = oy + 5 + r * 25;
            uiFont2(g);
            g->setTextColor(capCols[r]);
            uiPrintBold2(g, ox + 5, y, caps[r]);
            for (int i = 0; i < numAx; i++) {
                dtostrf(rows[r][i], 1, dec, v);
                snprintf(buf, sizeof(buf), "%s:%s", AXIS_NAMES[i], v);
                g->setTextColor(i == hot ? COLOR_GREEN : COLOR_WHITE);
                g->setCursor(COL0 + i * PITCH, y);
                g->print(buf);
            }
        } else {
            // 4-6 axes don't fit one Font2 row: two GLCD lines of three per system.
            int y = oy + 4 + r * 24;
            uiText(g, 1);
            g->setTextColor(capCols[r]);
            g->setCursor(ox + 5, y + 5);
            g->print(caps[r]);
            for (int i = 0; i < numAx; i++) {
                dtostrf(rows[r][i], 1, dec, v);
                snprintf(buf, sizeof(buf), "%s:%s", AXIS_NAMES[i], v);
                g->setTextColor(i == hot ? COLOR_GREEN : COLOR_WHITE);
                g->setCursor(COL0 + (i % 3) * PITCH, y + (i < 3 ? 0 : 10));
                g->print(buf);
            }
        }
    }
    g->drawFastHLine(ox + 5, oy + 25, 220, UI_EDGE);
    uiText(g, 1);

    endPanelSprite(230, PANEL_H, 5, PANEL_Y);
}

// ===== Partial redraws =====

void redrawJogAxisButtons() {
    if (currentPendantScreen != PSCREEN_JOG_HOMING) return;
    int numAx = pendantMachine.numAxes;
    for (int i = 0; i < numAx; i++) {
        uint16_t bg = (!pendantJog.speedDialMode && i == pendantJog.selectedAxis)
                      ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(rowBtnX(numAx, i), AXIS_Y, rowBtnWAt(numAx, i), ROW_H,
                   AXIS_NAMES[i], bg, COLOR_WHITE, AXIS_TS);
    }
    updateJogAxisDisplay();
}

void redrawJogIncrementButtons() {
    if (currentPendantScreen != PSCREEN_JOG_HOMING) return;
    IncrementSet incs = currentIncrements();
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantJog.selectedIncrement) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(incX(i), INC_Y, INC_W, INC_H, incs.labels[i], bg, COLOR_WHITE, 2);
    }
    drawIncDialButton();   // slot 3 — coarse dial box
}

// ===== Touch handler =====

void handleJogHomingTouch(int x, int y) {
    int numAx = pendantMachine.numAxes;

    // Home buttons — laid out on the shared row grid
    {
        const int numHome = (numAx < 4) ? numAx + 1 : numAx;
        for (int i = 0; i < numHome; i++) {
            int hx = rowBtnX(numHome, i), hw = rowBtnWAt(numHome, i);
            if (isTouchInBounds(x, y, hx, HOME_Y, hw, ROW_H)) {
                drawHomeButton(i, true);    // brief press flash
                delay_ms(150);
                drawHomeButton(i, false);
                if (!pendantConnected) return;
                char cmd[16];
                if (i == numAx) {
                    send_line("$H");
                    // Home-All: start the big DRO on X; onDROChange() then walks
                    // it through whichever axis is actively homing.
                    pendantJog.homingAxis = 0;
                } else {
                    snprintf(cmd, sizeof(cmd), "$H%s", AXIS_NAMES[i]);
                    send_line(cmd);
                    // Show the axis being homed in the big DRO.  This uses the
                    // transient homingAxis, NOT selectedAxis — so once homing
                    // finishes the DRO reverts to the jog-button selection and
                    // the user's manual axis choice is never disturbed.
                    pendantJog.homingAxis = i;
                }
                updateJogAxisDisplay();   // refresh the big DRO only
                return;
            }
        }
    }

    // Axis selection — also exits speed dial mode
    for (int i = 0; i < numAx; i++) {
        if (isTouchInBounds(x, y, rowBtnX(numAx, i), AXIS_Y, rowBtnWAt(numAx, i), ROW_H)) {
            bool leavingIncDial = pendantJog.incDialMode;
            pendantJog.speedDialMode = false;
            pendantJog.incDialMode   = false;   // picking an axis hands the dial back to jogging
            pendantJog.selectedAxis  = i;
            pendantJog.homingAxis    = -1;   // manual selection cancels any homing DRO override
            redrawJogAxisButtons();
            redrawJogSpeedButton();
            if (leavingIncDial) redrawJogIncrementButtons();   // drop the dial-box highlight
            redrawJogIncrementLabel();       // A↔X/Y/Z changes the unit (deg vs mm/in)
            return;
        }
    }

    // Increment selection.  Slots 0-2 are plain picks.  Slot 3 is the coarse dial
    // box, which walks three states so a tap can never surprise you by taking the
    // encoder away mid-jog:
    //   tap 1 (not yet selected) — just SELECT the coarse value; you keep jogging
    //   tap 2 (already selected) — hand the encoder over to stepping 10/50/100
    //                              (axis deselected, like the Speed box)
    //   tap 3 (adjusting)        — hand it back to jogging at the value just set,
    //                              restoring the axis you were on
    // After the first selection it simply toggles jog <-> adjust.  Tapping an axis
    // is still the other way out of adjust mode.
    {
        for (int i = 0; i < 4; i++) {
            if (isTouchInBounds(x, y, incX(i), INC_Y, INC_W, INC_H)) {
                bool wasSelected = (pendantJog.selectedIncrement == i);
                pendantJog.selectedIncrement = i;
                IncrementSet incs = currentIncrements();
                pendantJog.increment = incs.values[i];
                saveJogPrefs();

                if (i == 3) {
                    if (!wasSelected) {
                        // Tap 1 — select only, leave the encoder on jogging.
                        pendantJog.incDialMode = false;
                    } else if (pendantJog.incDialMode) {
                        // Tap 3 — back to jogging with the value just set.
                        pendantJog.incDialMode  = false;
                        pendantJog.selectedAxis = constrain(incDialPrevAxis, 0,
                                                            pendantMachine.numAxes - 1);
                    } else {
                        // Tap 2 — the encoder now steps the coarse value.
                        incDialPrevAxis = (pendantJog.selectedAxis >= 0)
                                          ? pendantJog.selectedAxis : 0;
                        pendantJog.incDialMode   = true;
                        pendantJog.speedDialMode = false;
                        pendantJog.selectedAxis  = -1;
                    }
                    redrawJogAxisButtons();      // also refreshes the DRO
                    redrawJogSpeedButton();
                } else if (pendantJog.incDialMode) {
                    pendantJog.incDialMode = false;   // leaving the dial box
                }
                redrawJogIncrementButtons();
                return;
            }
        }
    }

    // Speed button — enter speed dial mode, deselect axis
    if (isTouchInBounds(x, y, SPD_X, SPD_Y, SPD_W, SPD_H)) {
        bool leavingIncDial = pendantJog.incDialMode;
        pendantJog.speedDialMode = true;
        pendantJog.incDialMode   = false;   // only one dial mode at a time
        pendantJog.selectedAxis  = -1;
        redrawJogAxisButtons();
        redrawJogSpeedButton();
        if (leavingIncDial) redrawJogIncrementButtons();
        updateJogAxisDisplay();
        return;
    }

    if (isTouchInBounds(x, y, MM_X, SPD_Y, MM_W, SPD_H)) { currentPendantScreen = PSCREEN_MAIN_MENU;    return; }
    if (isTouchInBounds(x, y, WA_X, SPD_Y, WA_W, SPD_H)) { currentPendantScreen = PSCREEN_PROBING_WORK; return; }
}
