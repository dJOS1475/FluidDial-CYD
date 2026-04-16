#include "pendant_shared.h"
#include "screen_jog_homing.h"

// ===== Layout constants =====
// Bottom row: Main Menu | Speed | Work Area  (3 equal-ish buttons, 2px gaps)
// x=5, w=73 | x=80, w=80 | x=162, w=73
static const int SPD_X = 80;
static const int SPD_W = 80;
static const int SPD_Y = 277;
static const int SPD_H = 40;

// ===== Helpers =====

static String speedLabel() {
    return pendantMachine.inInches ? String(pendantJog.jogSpeedIn)
                                   : String(pendantJog.jogSpeedMm);
}

void redrawJogSpeedButton() {
    uint16_t bg = pendantJog.speedDialMode ? COLOR_GREEN : COLOR_BUTTON_GRAY;
    drawButton(SPD_X, SPD_Y, SPD_W, SPD_H, speedLabel(), bg, COLOR_WHITE, 2);
}

// ===== Lifecycle =====

void enterJogHoming() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

    // Re-apply the current increment in case units changed since last visit
    {
        float mmInc[] = { 0.1f,   1.0f,   10.0f,  100.0f };
        float inInc[] = { 0.001f, 0.010f, 0.100f, 1.000f };
        int   idx     = pendantJog.selectedIncrement;
        pendantJog.increment = pendantMachine.inInches ? inInc[idx] : mmInc[idx];
    }

    // Ensure a valid axis is selected on entry — exit speed dial mode if active
    if (pendantJog.speedDialMode || pendantJog.selectedAxis < 0) {
        pendantJog.speedDialMode = false;
        pendantJog.selectedAxis  = 0;
    }

    // Request per-axis max rate from controller ($110) to set jog speed cap
    if (pendantConnected) requestJogConfig();

    if (ESP.getFreeHeap() < 50000) return;

    spriteAxisDisplay.createSprite(230, 55);
    spriteAxisDisplay.setColorDepth(16);
    spriteValueDisplay.createSprite(230, 40);
    spriteValueDisplay.setColorDepth(16);
    spritesInitialized = true;
}

void exitJogHoming() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spritesInitialized = false;
}

// ===== Draw =====

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
    display.print("JOG AXIS");

    for (int i = 0; i < numAx; i++) {
        // Deselect all axis buttons when in speed dial mode
        uint16_t bg = (!pendantJog.speedDialMode && i == pendantJog.selectedAxis)
                      ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * btnW, 115, btnW - 4, 38, axisNames[i], bg, COLOR_WHITE, 3);
    }

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 161);
    display.print("HOME");

    {
        const int HW = 57;
        String homeNames[4] = { "X", "Y", "Z", numAx < 4 ? "ALL" : "A" };
        int    numHome       = (numAx < 4) ? numAx + 1 : 4;
        for (int i = 0; i < numHome; i++) {
            int sz = (i == numAx && numAx < 4) ? 2 : 3;
            drawButton(5 + i * HW, 173, HW - 4, 38, homeNames[i], COLOR_DARK_GREEN, COLOR_WHITE, sz);
        }
    }

    display.setTextSize(1);
    display.setCursor(5, 219);
    if (pendantMachine.status.startsWith("Alarm")) {
        display.setTextColor(TFT_RED);
        display.print("JOG INCREMENT  *** " + pendantMachine.status + " ***");
    } else {
        display.setTextColor(COLOR_GRAY_TEXT);
        display.print(pendantMachine.inInches ? "JOG INCREMENT (in)" : "JOG INCREMENT (mm)");
    }

    const char* mmInc[] = { "0.1",  "1",    "10",   "100"  };
    const char* inInc[] = { ".001", ".010", ".100", "1.00" };
    const char** incLabels = pendantMachine.inInches ? inInc : mmInc;
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (i == pendantJog.selectedIncrement) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 56, 231, 52, 38, incLabels[i], bg, COLOR_WHITE, 2);
    }

    // Bottom row: Main Menu | Speed | Work Area
    drawButton(5,   SPD_Y, 73, SPD_H, "Main Menu", COLOR_BLUE, COLOR_WHITE, 1);
    redrawJogSpeedButton();
    drawButton(162, SPD_Y, 73, SPD_H, "Work Area", COLOR_BLUE, COLOR_WHITE, 1);
}

// ===== Sprite update =====

void updateJogAxisDisplay() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_JOG_HOMING) return;
    if (!spriteAxisDisplay.getBuffer()) return;

    float px, py, pz, pa;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        px = pendantMachine.posX; py = pendantMachine.posY;
        pz = pendantMachine.posZ; pa = pendantMachine.posA;
        xSemaphoreGive(stateMutex);
    } else {
        px = pendantMachine.posX; py = pendantMachine.posY;
        pz = pendantMachine.posZ; pa = pendantMachine.posA;
    }

    spriteAxisDisplay.fillSprite(COLOR_DARKER_BG);

    if (pendantJog.speedDialMode) {
        // Speed dial mode — show jog speed prominently
        spriteAxisDisplay.setTextColor(COLOR_GREEN);
        spriteAxisDisplay.setTextSize(1);
        int16_t lw = spriteAxisDisplay.textWidth("JOG SPEED");
        spriteAxisDisplay.setCursor(115 - lw / 2, 5);
        spriteAxisDisplay.print("JOG SPEED");

        String speedStr = pendantMachine.inInches
            ? "F:" + String(pendantJog.jogSpeedIn) + " ipm"
            : "F:" + String(pendantJog.jogSpeedMm) + " mm/m";
        spriteAxisDisplay.setTextSize(2);
        int16_t sw = spriteAxisDisplay.textWidth(speedStr.c_str());
        spriteAxisDisplay.setCursor(115 - sw / 2, 20);
        spriteAxisDisplay.print(speedStr);

        spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT);
        spriteAxisDisplay.setTextSize(1);
        int16_t hw = spriteAxisDisplay.textWidth("Select an axis to jog");
        spriteAxisDisplay.setCursor(115 - hw / 2, 42);
        spriteAxisDisplay.print("Select an axis to jog");
    } else {
        String axisNames[] = { "X", "Y", "Z", "A" };
        float  positions[] = { px, py, pz, pa };
        bool   inAlarm     = pendantMachine.status.startsWith("Alarm");

        // Large selected axis + position on one line; unit replaced with alarm state when active
        char posBuf[12];
        int  decPlaces = pendantMachine.inInches ? 4 : 2;
        dtostrf(positions[pendantJog.selectedAxis], 1, decPlaces, posBuf);
        String unitOrAlarm = inAlarm ? pendantMachine.status
                                     : (pendantMachine.inInches ? "in" : "mm");
        char mainLine[32];
        snprintf(mainLine, sizeof(mainLine), "%s %s %s",
                 axisNames[pendantJog.selectedAxis].c_str(), posBuf,
                 unitOrAlarm.c_str());
        spriteAxisDisplay.setTextColor(inAlarm ? TFT_RED : COLOR_GREEN);
        spriteAxisDisplay.setTextSize(3);
        spriteAxisDisplay.setCursor(5, 5);
        spriteAxisDisplay.print(mainLine);

        // Non-selected axes in a small row underneath
        spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT);
        spriteAxisDisplay.setTextSize(1);
        int numAx      = pendantMachine.numAxes;
        int colSpacing = (numAx > 1) ? 230 / (numAx - 1) : 230;
        int col        = 5;
        for (int i = 0; i < numAx; i++) {
            if (i == pendantJog.selectedAxis) continue;
            char valBuf[10];
            dtostrf(positions[i], 1, 2, valBuf);
            char buf[16];
            snprintf(buf, sizeof(buf), "%s:%s", axisNames[i].c_str(), valBuf);
            spriteAxisDisplay.setCursor(col, 38);
            spriteAxisDisplay.print(buf);
            col += colSpacing;
        }
    }

    spriteAxisDisplay.pushSprite(5, 40);
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
        drawButton(5 + i * btnW, 115, btnW - 4, 38, axisNames[i], bg, COLOR_WHITE, 3);
    }
    updateJogAxisDisplay();
}

void redrawJogIncrementButtons() {
    if (currentPendantScreen != PSCREEN_JOG_HOMING) return;
    const char* mmInc[] = { "0.1",  "1",    "10",   "100"  };
    const char* inInc[] = { ".001", ".010", ".100", "1.00" };
    const char** incLabels = pendantMachine.inInches ? inInc : mmInc;
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (i == pendantJog.selectedIncrement) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 56, 231, 52, 38, incLabels[i], bg, COLOR_WHITE, 2);
    }
}

// ===== Touch handler =====

void handleJogHomingTouch(int x, int y) {
    int numAx = pendantMachine.numAxes;
    int btnW  = 230 / numAx;

    // Axis selection — also exits speed dial mode
    for (int i = 0; i < numAx; i++) {
        if (isTouchInBounds(x, y, 5 + i * btnW, 115, btnW - 4, 38)) {
            pendantJog.speedDialMode = false;
            pendantJog.selectedAxis  = i;
            redrawJogAxisButtons();
            redrawJogSpeedButton();
            return;
        }
    }

    // Home buttons — always 4 at fixed 57px width
    {
        const int HW = 57;
        String homeNames[4] = { "X", "Y", "Z", numAx < 4 ? "ALL" : "A" };
        int    numHome       = (numAx < 4) ? numAx + 1 : 4;
        for (int i = 0; i < numHome; i++) {
            if (isTouchInBounds(x, y, 5 + i * HW, 173, HW - 4, 38)) {
                int sz = (i == numAx && numAx < 4) ? 2 : 3;
                drawButton(5 + i * HW, 173, HW - 4, 38, homeNames[i], COLOR_WHITE, COLOR_DARK_GREEN, sz);
                delay_ms(150);
                drawButton(5 + i * HW, 173, HW - 4, 38, homeNames[i], COLOR_DARK_GREEN, COLOR_WHITE, sz);
                if (!pendantConnected) return;
                char cmd[16];
                if (i == numAx) {
                    send_line("$H");
                } else {
                    String axisNames[] = { "X", "Y", "Z", "A" };
                    snprintf(cmd, sizeof(cmd), "$H%s", axisNames[i].c_str());
                    send_line(cmd);
                }
                return;
            }
        }
    }

    // Increment selection
    for (int i = 0; i < 4; i++) {
        if (isTouchInBounds(x, y, 5 + i * 56, 231, 52, 38)) {
            pendantJog.selectedIncrement = i;
            float mmInc[] = { 0.1f,   1.0f,   10.0f,  100.0f };
            float inInc[] = { 0.001f, 0.010f, 0.100f, 1.000f };
            pendantJog.increment = pendantMachine.inInches ? inInc[i] : mmInc[i];
            redrawJogIncrementButtons();
            return;
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

    if (isTouchInBounds(x, y, 5,   SPD_Y, 73, SPD_H)) navigateTo(PSCREEN_MAIN_MENU);
    if (isTouchInBounds(x, y, 162, SPD_Y, 73, SPD_H)) navigateTo(PSCREEN_PROBING_WORK);
}
