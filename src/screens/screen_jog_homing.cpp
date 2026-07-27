#include "pendant_shared.h"
#include "screen_jog_homing.h"
#include "screen_probe.h"   // probeDrawKVTouch — shared adjustable-field style

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
// Bottom row: Main Menu | Speed | Work Area  (3 equal-ish buttons, 2px gaps)
// x=5, w=73 | x=80, w=80 | x=162, w=73
static const int SPD_X = 80;
static const int SPD_W = 80;
static const int SPD_Y = 277;
static const int SPD_H = 40;

// Button rows use the shared rowBtnX()/rowBtnWAt() grid from pendant_shared.h, so
// HOME, JOG AXIS and the increment row all span 5..235 and align with each other.
static const int ROW_H  = 38;
static const int HOME_Y = 115;
static const int AXIS_Y = 173;

// Increment row: 4 slots on the shared grid — rowBtnW(4) == 56, pitch 58.
static const int INC_Y = 231;
static const int INC_H = ROW_H;
static const int INC_W = 56;
static inline int incX(int i) { return rowBtnX(4, i); }

// Axis to restore when the coarse dial box hands the encoder back to jogging.
static int incDialPrevAxis = 0;

// Slot 3 is the coarse dial box.  Drawn as an adjustable field rather than a plain
// button — small "DIAL" cap, value below, tappable border — so it reads as
// something the encoder drives.  Yellow border/value while incDialMode is live,
// matching every other dial-driven field in the UI.
static void drawIncDialButton() {
    bool selected = (pendantJog.selectedIncrement == 3);
    bool active   = pendantJog.incDialMode;
    IncrementSet incs = currentIncrements();

    uint16_t bg  = active ? PROBE_SEL_BG : (selected ? COLOR_ORANGE : COLOR_BUTTON_GRAY);
    uint16_t bdr = active ? PROBE_C_YELLOW : PROBE_C_TAPBDR;
    int x = incX(3);
    display.fillRoundRect(x, INC_Y, INC_W, INC_H, 5, bg);
    display.drawRoundRect(x, INC_Y, INC_W, INC_H, 5, bdr);

    display.setTextSize(1);
    display.setTextColor(active ? PROBE_C_YELLOW : COLOR_WHITE);
    int16_t lw = display.textWidth("DIAL");
    display.setCursor(x + (INC_W - lw) / 2, INC_Y + 5);
    display.print("DIAL");

    const char* v = incs.labels[3];
    display.setTextSize(2);
    display.setTextColor(active ? PROBE_C_YELLOW : COLOR_WHITE);
    int16_t vw = display.textWidth(v);
    display.setCursor(x + (INC_W - vw) / 2, INC_Y + 17);
    display.print(v);
}

// ===== Helpers =====

void redrawJogSpeedButton() {
    // Adjustable-field style, matching the tap-to-edit buttons on the Probe
    // screens: a bordered box with a small label on top and the value below;
    // the border + value highlight (yellow) while speed-dial mode is active.
    bool        active = pendantJog.speedDialMode;
    float       spd    = pendantMachine.inInches ? (float)pendantJog.jogSpeedIn
                                                 : (float)pendantJog.jogSpeedMm;
    const char* unit   = pendantMachine.inInches ? "ipm" : "mm/m";
    probeDrawKVTouch(SPD_X, SPD_Y, SPD_W, SPD_H, "SPEED", spd, unit,
                     COLOR_GREEN, active, 0);
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

    // The big DRO uses a transient 16-bit panel (see updateJogAxisDisplay); the
    // direct-draw fallback keeps it from ever being blank under heap pressure.
    releasePanelSprites();
}

void exitJogHoming() {
    releasePanelSprites();
}

// ===== Draw =====

// Redraws just the "JOG INCREMENT (unit) — mode" label row.  Pulled out so it
// can refresh when the selected axis changes (A → "deg", X/Y/Z → "mm"/"in")
// without a full-screen redraw.
static void redrawJogIncrementLabel() {
    display.fillRect(5, 219, 230, 9, COLOR_BACKGROUND);   // clear the old text row
    display.setTextSize(1);
    display.setCursor(5, 219);
    if (pendantMachine.status.startsWith("Alarm")) {
        display.setTextColor(TFT_RED);
        display.print("JOG INCREMENT  *** " + pendantMachine.status + " ***");
    } else {
        display.setTextColor(COLOR_GRAY_TEXT);
        // A axis (rotary) → label the increments in degrees, not mm/in.
        String unitStr = (pendantJog.selectedAxis == 3) ? "deg"
                                                        : (pendantMachine.inInches ? "in" : "mm");
        display.print("JOG INCREMENT (" + unitStr + ")");
    }
}

void drawJogHomingScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("JOG & HOMING");

    display.fillRoundRect(5, 40, 230, 55, 5, COLOR_DARKER_BG);
    updateJogAxisDisplay();

    String axisNames[] = { "X", "Y", "Z", "A" };
    int numAx = pendantMachine.numAxes;

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 103);
    display.print("HOME");

    {
        String homeNames[4] = { "X", "Y", "Z", numAx < 4 ? "ALL" : "A" };
        int    numHome       = (numAx < 4) ? numAx + 1 : 4;
        for (int i = 0; i < numHome; i++) {
            int sz = (i == numAx && numAx < 4) ? 2 : 3;
            drawButton(rowBtnX(numHome, i), HOME_Y, rowBtnWAt(numHome, i), ROW_H,
                       homeNames[i], COLOR_DARK_GREEN, COLOR_WHITE, sz);
        }
    }

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 161);
    display.print("JOG AXIS");

    for (int i = 0; i < numAx; i++) {
        // Deselect all axis buttons when in speed dial mode
        uint16_t bg = (!pendantJog.speedDialMode && i == pendantJog.selectedAxis)
                      ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(rowBtnX(numAx, i), AXIS_Y, rowBtnWAt(numAx, i), ROW_H,
                   axisNames[i], bg, COLOR_WHITE, 3);
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

    // Bottom row: Main Menu | Speed | Work Area
    drawButton(5,   SPD_Y, 73, SPD_H, "Main Menu", COLOR_BLUE, COLOR_WHITE, 1);
    redrawJogSpeedButton();
    drawButton(162, SPD_Y, 73, SPD_H, "Work Area", COLOR_BLUE, COLOR_WHITE, 1);
}

// ===== Sprite update =====

// Tiny degree "°" glyph drawn from two concentric rings — font-independent, so
// it works regardless of whether the active font carries a degree character.
static void drawDegreeIcon(LovyanGFX* g, int x, int y, uint16_t color) {
    g->drawCircle(x, y, 3, color);
    g->drawCircle(x, y, 2, color);
}

void updateJogAxisDisplay() {
    if (currentPendantScreen != PSCREEN_JOG_HOMING) return;

    // Snapshot under the lock; skip the frame if briefly held.  Panel is
    // 230 x 55, pushed at (5, 40); shared 16-bit scratch, direct-draw fallback.
    // Jog & Homing shows MACHINE coordinates (workX/Y/Z/A = MPos) — homing and
    // travel-limit reasoning are all in machine space.  (posX/Y/Z are work coords.)
    float px, py, pz, pa;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    px = pendantMachine.workX; py = pendantMachine.workY;
    pz = pendantMachine.workZ; pa = pendantMachine.workA;
    xSemaphoreGive(stateMutex);

    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, 55, ox, oy, 5, 40);
    g->fillRect(ox, oy, 230, 55, COLOR_DARKER_BG);

    if (pendantJog.speedDialMode) {
        // Speed dial mode — show jog speed prominently
        g->setTextColor(COLOR_GREEN);
        g->setTextSize(1);
        int16_t lw = g->textWidth("JOG SPEED");
        g->setCursor(ox + 115 - lw / 2, oy + 5);
        g->print("JOG SPEED");

        String speedStr = pendantMachine.inInches
            ? "F:" + String(pendantJog.jogSpeedIn) + " ipm"
            : "F:" + String(pendantJog.jogSpeedMm) + " mm/m";
        g->setTextSize(2);
        int16_t sw = g->textWidth(speedStr.c_str());
        g->setCursor(ox + 115 - sw / 2, oy + 20);
        g->print(speedStr);

        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        int16_t hw = g->textWidth("Select an axis to jog");
        g->setCursor(ox + 115 - hw / 2, oy + 42);
        g->print("Select an axis to jog");
    } else if (pendantJog.incDialMode) {
        // Coarse dial mode — mirrors the speed panel so the encoder's job is never
        // ambiguous: it's stepping the increment, not moving the machine.
        IncrementSet incs = currentIncrements();
        const char*  unit = (pendantJog.selectedAxis == 3) ? "deg"
                                                           : (pendantMachine.inInches ? "in" : "mm");
        g->setTextColor(PROBE_C_YELLOW);
        g->setTextSize(1);
        int16_t lw = g->textWidth("JOG INCREMENT");
        g->setCursor(ox + 115 - lw / 2, oy + 5);
        g->print("JOG INCREMENT");

        String incStr = String(incs.labels[3]) + " " + unit;
        g->setTextSize(2);
        int16_t sw = g->textWidth(incStr.c_str());
        g->setCursor(ox + 115 - sw / 2, oy + 20);
        g->print(incStr);

        // Name the toggle explicitly — the second tap is the quick way back to
        // jogging, and nothing else on screen would tell you it exists.
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        const char* hint = "Tap DIAL again to jog";
        int16_t hw2 = g->textWidth(hint);
        g->setCursor(ox + 115 - hw2 / 2, oy + 42);
        g->print(hint);
    } else {
        String axisNames[] = { "X", "Y", "Z", "A" };
        float  positions[] = { px, py, pz, pa };
        bool   inAlarm     = pendantMachine.status.startsWith("Alarm");

        // While homing, the big DRO shows the axis being homed (pendantJog.homingAxis);
        // otherwise it shows the user's jog-button selection (selectedAxis).  When
        // homing ends, homingAxis goes back to -1 and the DRO returns to selectedAxis.
        int dispAxis = (pendantJog.homingAxis >= 0) ? pendantJog.homingAxis
                                                     : pendantJog.selectedAxis;

        // Large display: axis + position on one line.  Unit handling:
        //   • in alarm        → unit slot shows the alarm state (red)
        //   • A axis (rotary)  → no "mm"/"in"; a degree symbol is drawn instead
        //   • X / Y / Z        → "mm" or "in" as usual
        bool isAAxis = (dispAxis == 3);
        char posBuf[12];
        int  decPlaces = pendantMachine.inInches ? 4 : 2;
        dtostrf(positions[dispAxis], 1, decPlaces, posBuf);

        char mainLine[32];
        if (inAlarm) {
            snprintf(mainLine, sizeof(mainLine), "%s %s %s",
                     axisNames[dispAxis].c_str(), posBuf, pendantMachine.status.c_str());
        } else if (isAAxis) {
            // No text unit — a degree icon is overlaid after the value below.
            snprintf(mainLine, sizeof(mainLine), "%s %s",
                     axisNames[dispAxis].c_str(), posBuf);
        } else {
            snprintf(mainLine, sizeof(mainLine), "%s %s %s",
                     axisNames[dispAxis].c_str(), posBuf,
                     pendantMachine.inInches ? "in" : "mm");
        }
        g->setTextColor(inAlarm ? TFT_RED : COLOR_GREEN);
        g->setTextSize(3);
        g->setCursor(ox + 5, oy + 5);
        g->print(mainLine);

        // Rotary A axis: draw a degree "°" after the value (font-independent).
        if (isAAxis && !inAlarm) {
            int iconX = ox + 5 + g->textWidth(mainLine) + 6;
            drawDegreeIcon(g, iconX, oy + 8, COLOR_GREEN);
        }

        // Non-selected axes in a small row underneath
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        int numAx      = pendantMachine.numAxes;
        int colSpacing = (numAx > 1) ? 230 / (numAx - 1) : 230;
        int col        = 5;
        for (int i = 0; i < numAx; i++) {
            if (i == dispAxis) continue;  // already shown large above
            char valBuf[10];
            dtostrf(positions[i], 1, 2, valBuf);
            char buf[16];
            snprintf(buf, sizeof(buf), "%s:%s", axisNames[i].c_str(), valBuf);
            g->setCursor(ox + col, oy + 38);
            g->print(buf);
            col += colSpacing;
        }
    }

    endPanelSprite(230, 55, 5, 40);
}

// ===== Partial redraws =====

void redrawJogAxisButtons() {
    if (currentPendantScreen != PSCREEN_JOG_HOMING) return;
    String axisNames[] = { "X", "Y", "Z", "A" };
    int numAx = pendantMachine.numAxes;
    for (int i = 0; i < numAx; i++) {
        uint16_t bg = (!pendantJog.speedDialMode && i == pendantJog.selectedAxis)
                      ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(rowBtnX(numAx, i), AXIS_Y, rowBtnWAt(numAx, i), ROW_H,
                   axisNames[i], bg, COLOR_WHITE, 3);
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
        String homeNames[4] = { "X", "Y", "Z", numAx < 4 ? "ALL" : "A" };
        int    numHome       = (numAx < 4) ? numAx + 1 : 4;
        for (int i = 0; i < numHome; i++) {
            int hx = rowBtnX(numHome, i), hw = rowBtnWAt(numHome, i);
            if (isTouchInBounds(x, y, hx, HOME_Y, hw, ROW_H)) {
                int sz = (i == numAx && numAx < 4) ? 2 : 3;
                drawButton(hx, HOME_Y, hw, ROW_H, homeNames[i], COLOR_WHITE, COLOR_DARK_GREEN, sz);
                delay_ms(150);
                drawButton(hx, HOME_Y, hw, ROW_H, homeNames[i], COLOR_DARK_GREEN, COLOR_WHITE, sz);
                if (!pendantConnected) return;
                char cmd[16];
                if (i == numAx) {
                    send_line("$H");
                    // Home-All: start the big DRO on X; onDROChange() then walks
                    // it through whichever axis is actively homing.
                    pendantJog.homingAxis = 0;
                } else {
                    String axisNames[] = { "X", "Y", "Z", "A" };
                    snprintf(cmd, sizeof(cmd), "$H%s", axisNames[i].c_str());
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

    if (isTouchInBounds(x, y, 5,   SPD_Y, 73, SPD_H)) { currentPendantScreen = PSCREEN_MAIN_MENU;    return; }
    if (isTouchInBounds(x, y, 162, SPD_Y, 73, SPD_H)) { currentPendantScreen = PSCREEN_PROBING_WORK; return; }
}
