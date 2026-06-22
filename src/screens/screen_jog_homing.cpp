#include "pendant_shared.h"
#include "screen_jog_homing.h"
#include "screen_probe.h"   // probeDrawKVTouch — shared adjustable-field style

// ===== Increment sets =====
struct IncrementSet {
    const char* labels[4];
    float       values[4];
};

static IncrementSet currentIncrements() {
    IncrementSet s;
    if (pendantMachine.inInches) {
        if (pendantJog.fineIncrements) {
            // fine imperial: .0001 .001 .010 .100
            s.labels[0]=".0001"; s.labels[1]=".001"; s.labels[2]=".010"; s.labels[3]=".100";
            s.values[0]=0.0001f; s.values[1]=0.001f; s.values[2]=0.010f; s.values[3]=0.100f;
        } else {
            // coarse imperial: .05 .5 2.0 4.0 in  (~1, 10, 50, 100 mm)
            s.labels[0]=".05";   s.labels[1]=".5";   s.labels[2]="2.0";  s.labels[3]="4.0";
            s.values[0]=0.05f;   s.values[1]=0.5f;   s.values[2]=2.0f;   s.values[3]=4.0f;
        }
    } else {
        if (pendantJog.fineIncrements) {
            // fine metric: 0.01 0.1 1 10
            s.labels[0]="0.01"; s.labels[1]="0.1"; s.labels[2]="1";   s.labels[3]="10";
            s.values[0]=0.01f;  s.values[1]=0.1f;  s.values[2]=1.0f;  s.values[3]=10.0f;
        } else {
            // coarse metric: 1 10 50 100
            s.labels[0]="1";    s.labels[1]="10";   s.labels[2]="50";  s.labels[3]="100";
            s.values[0]=1.0f;   s.values[1]=10.0f;  s.values[2]=50.0f; s.values[3]=100.0f;
        }
    }
    return s;
}

// ===== Layout constants =====
// Bottom row: Main Menu | Speed | Work Area  (3 equal-ish buttons, 2px gaps)
// x=5, w=73 | x=80, w=80 | x=162, w=73
static const int SPD_X = 80;
static const int SPD_W = 80;
static const int SPD_Y = 277;
static const int SPD_H = 40;

// Triple-tap state for the rightmost increment button — toggles fine/coarse.
// Kept at file scope so enterJogHoming() can reset between visits (a stale tap
// count from a previous session would otherwise count toward the next toggle).
static int           incTapCount = 0;
static unsigned long incTapMs    = 0;

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

    // Re-apply the current increment in case units or fine/coarse mode changed since last visit
    {
        IncrementSet incs = currentIncrements();
        pendantJog.increment = incs.values[pendantJog.selectedIncrement];
    }

    // Reset triple-tap state so a partial sequence from a prior visit doesn't carry over.
    incTapCount = 0;
    incTapMs    = 0;

    // Ensure a valid axis is selected on entry — exit speed dial mode if active
    if (pendantJog.speedDialMode || pendantJog.selectedAxis < 0) {
        pendantJog.speedDialMode = false;
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
        String modeStr = pendantJog.fineIncrements ? " — fine" : " — coarse";
        display.print("JOG INCREMENT (" + unitStr + ")" + modeStr);
    }
}

void drawJogHomingScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("JOG & HOMING");

    display.fillRoundRect(5, 40, 230, 55, 5, COLOR_DARKER_BG);
    updateJogAxisDisplay();

    String axisNames[] = { "X", "Y", "Z", "A" };
    int numAx = pendantMachine.numAxes;
    int btnW  = 230 / numAx;

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 103);
    display.print("HOME");

    {
        const int HW = 57;
        String homeNames[4] = { "X", "Y", "Z", numAx < 4 ? "ALL" : "A" };
        int    numHome       = (numAx < 4) ? numAx + 1 : 4;
        for (int i = 0; i < numHome; i++) {
            int sz = (i == numAx && numAx < 4) ? 2 : 3;
            drawButton(5 + i * HW, 115, HW - 4, 38, homeNames[i], COLOR_DARK_GREEN, COLOR_WHITE, sz);
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
        drawButton(5 + i * btnW, 173, btnW - 4, 38, axisNames[i], bg, COLOR_WHITE, 3);
    }

    redrawJogIncrementLabel();

    IncrementSet incs = currentIncrements();
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (i == pendantJog.selectedIncrement) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 56, 231, 52, 38, incs.labels[i], bg, COLOR_WHITE, 2);
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
    float px, py, pz, pa;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    px = pendantMachine.posX; py = pendantMachine.posY;
    pz = pendantMachine.posZ; pa = pendantMachine.posA;
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
    int btnW  = 230 / numAx;
    for (int i = 0; i < numAx; i++) {
        uint16_t bg = (!pendantJog.speedDialMode && i == pendantJog.selectedAxis)
                      ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * btnW, 173, btnW - 4, 38, axisNames[i], bg, COLOR_WHITE, 3);
    }
    updateJogAxisDisplay();
}

void redrawJogIncrementButtons() {
    if (currentPendantScreen != PSCREEN_JOG_HOMING) return;
    IncrementSet incs = currentIncrements();
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (i == pendantJog.selectedIncrement) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 56, 231, 52, 38, incs.labels[i], bg, COLOR_WHITE, 2);
    }
}

// ===== Touch handler =====

void handleJogHomingTouch(int x, int y) {
    int numAx = pendantMachine.numAxes;
    int btnW  = 230 / numAx;

    // Home buttons — always 4 at fixed 57px width
    {
        const int HW = 57;
        String homeNames[4] = { "X", "Y", "Z", numAx < 4 ? "ALL" : "A" };
        int    numHome       = (numAx < 4) ? numAx + 1 : 4;
        for (int i = 0; i < numHome; i++) {
            if (isTouchInBounds(x, y, 5 + i * HW, 115, HW - 4, 38)) {
                int sz = (i == numAx && numAx < 4) ? 2 : 3;
                drawButton(5 + i * HW, 115, HW - 4, 38, homeNames[i], COLOR_WHITE, COLOR_DARK_GREEN, sz);
                delay_ms(150);
                drawButton(5 + i * HW, 115, HW - 4, 38, homeNames[i], COLOR_DARK_GREEN, COLOR_WHITE, sz);
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
        if (isTouchInBounds(x, y, 5 + i * btnW, 173, btnW - 4, 38)) {
            pendantJog.speedDialMode = false;
            pendantJog.selectedAxis  = i;
            pendantJog.homingAxis    = -1;   // manual selection cancels any homing DRO override
            redrawJogAxisButtons();
            redrawJogSpeedButton();
            redrawJogIncrementLabel();       // A↔X/Y/Z changes the unit (deg vs mm/in)
            return;
        }
    }

    // Increment selection — triple-tap button 3 (rightmost) toggles fine/coarse set
    {
        for (int i = 0; i < 4; i++) {
            if (isTouchInBounds(x, y, 5 + i * 56, 231, 52, 38)) {
                if (i == 3) {
                    unsigned long now = millis();
                    if (now - incTapMs < 600) {
                        incTapCount++;
                    } else {
                        incTapCount = 1;
                    }
                    incTapMs = now;

                    if (incTapCount >= 3) {
                        incTapCount = 0;
                        pendantJog.fineIncrements = !pendantJog.fineIncrements;
                        IncrementSet incs = currentIncrements();
                        pendantJog.increment = incs.values[pendantJog.selectedIncrement];
                        saveJogPrefs();
                        drawJogHomingScreen();  // full redraw to update label
                        return;
                    }
                } else {
                    incTapCount = 0;  // reset triple-tap if another button tapped
                }

                // Normal: select this increment
                pendantJog.selectedIncrement = i;
                IncrementSet incs = currentIncrements();
                pendantJog.increment = incs.values[i];
                saveJogPrefs();
                redrawJogIncrementButtons();
                return;
            }
        }
    }

    // Speed button — enter speed dial mode, deselect axis
    if (isTouchInBounds(x, y, SPD_X, SPD_Y, SPD_W, SPD_H)) {
        pendantJog.speedDialMode = true;
        pendantJog.selectedAxis  = -1;
        redrawJogAxisButtons();
        redrawJogSpeedButton();
        updateJogAxisDisplay();
        return;
    }

    if (isTouchInBounds(x, y, 5,   SPD_Y, 73, SPD_H)) { currentPendantScreen = PSCREEN_MAIN_MENU;    return; }
    if (isTouchInBounds(x, y, 162, SPD_Y, 73, SPD_H)) { currentPendantScreen = PSCREEN_PROBING_WORK; return; }
}
