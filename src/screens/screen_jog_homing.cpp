#include "pendant_shared.h"
#include "screen_jog_homing.h"

void enterJogHoming() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

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

void drawJogHomingScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("JOG & HOMING");

    display.fillRoundRect(5, 40, 230, 55, 5, COLOR_DARKER_BG);
    updateJogAxisDisplay();

    String axisNames[] = { "X", "Y", "Z", "A" };

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 103);
    display.print("JOG AXIS");

    for (int i = 0; i < 4; i++) {
        uint16_t bg = (i == pendantJog.selectedAxis) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 56, 115, 52, 38, axisNames[i], bg, COLOR_WHITE, 3);
    }

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 161);
    display.print("HOME");

    for (int i = 0; i < 4; i++) {
        drawButton(5 + i * 56, 173, 52, 38, axisNames[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
    }

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 219);
    display.print("JOG INCREMENT");

    String increments[] = { "0.1", "1", "10", "100" };
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (i == pendantJog.selectedIncrement) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 56, 231, 52, 38, increments[i], bg, COLOR_WHITE, 2);
    }

    drawButton(5, 277, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(123, 277, 112, 40, "Work Area", COLOR_BLUE, COLOR_WHITE, 2);
}

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

    String axisNames[] = { "X", "Y", "Z", "A" };
    float  positions[] = { px, py, pz, pa };

    // Large selected axis + position on one line
    char posBuf[12];
    dtostrf(positions[pendantJog.selectedAxis], 1, 2, posBuf);
    char mainLine[32];
    snprintf(mainLine, sizeof(mainLine), "%s %s mm", axisNames[pendantJog.selectedAxis].c_str(), posBuf);
    spriteAxisDisplay.setTextColor(COLOR_GREEN);
    spriteAxisDisplay.setTextSize(3);
    spriteAxisDisplay.setCursor(5, 5);
    spriteAxisDisplay.print(mainLine);

    // Non-selected axes in a small row underneath
    spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteAxisDisplay.setTextSize(1);
    int col = 5;
    for (int i = 0; i < 4; i++) {
        if (i == pendantJog.selectedAxis) continue;
        char valBuf[10];
        dtostrf(positions[i], 1, 2, valBuf);
        char buf[16];
        snprintf(buf, sizeof(buf), "%s:%s", axisNames[i].c_str(), valBuf);
        spriteAxisDisplay.setCursor(col, 38);
        spriteAxisDisplay.print(buf);
        col += 75;
    }

    spriteAxisDisplay.pushSprite(5, 40);
}

void redrawJogAxisButtons() {
    if (currentPendantScreen != PSCREEN_JOG_HOMING) return;
    String axisNames[] = { "X", "Y", "Z", "A" };
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (i == pendantJog.selectedAxis) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 56, 115, 52, 38, axisNames[i], bg, COLOR_WHITE, 3);
    }
    updateJogAxisDisplay();
}

void redrawJogIncrementButtons() {
    if (currentPendantScreen != PSCREEN_JOG_HOMING) return;
    String increments[] = { "0.1", "1", "10", "100" };
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (i == pendantJog.selectedIncrement) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 56, 231, 52, 38, increments[i], bg, COLOR_WHITE, 2);
    }
}

void handleJogHomingTouch(int x, int y) {
    // Axis selection
    for (int i = 0; i < 4; i++) {
        if (isTouchInBounds(x, y, 5 + i * 56, 115, 52, 38)) {
            pendantJog.selectedAxis = i;
            redrawJogAxisButtons();
            return;
        }
    }

    // Home buttons
    String axisNames[] = { "X", "Y", "Z", "A" };
    for (int i = 0; i < 4; i++) {
        if (isTouchInBounds(x, y, 5 + i * 56, 173, 52, 38)) {
            drawButton(5 + i * 56, 173, 52, 38, axisNames[i], COLOR_WHITE, COLOR_DARK_GREEN, 3);
            delay_ms(150);
            drawButton(5 + i * 56, 173, 52, 38, axisNames[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
            if (!pendantConnected) return;
            char cmd[16];
            snprintf(cmd, sizeof(cmd), "$H%s", axisNames[i].c_str());
            send_line(cmd);
            return;
        }
    }

    // Increment selection
    for (int i = 0; i < 4; i++) {
        if (isTouchInBounds(x, y, 5 + i * 56, 231, 52, 38)) {
            pendantJog.selectedIncrement = i;
            float increments[] = { 0.1f, 1.0f, 10.0f, 100.0f };
            pendantJog.increment = increments[i];
            redrawJogIncrementButtons();
            return;
        }
    }

    if      (isTouchInBounds(x, y, 5,   277, 112, 40)) currentPendantScreen = PSCREEN_MAIN_MENU;
    else if (isTouchInBounds(x, y, 123, 277, 112, 40)) currentPendantScreen = PSCREEN_PROBING_WORK;
}
