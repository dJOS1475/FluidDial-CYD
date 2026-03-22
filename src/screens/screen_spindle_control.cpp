#include "pendant_shared.h"
#include "screen_spindle_control.h"

void enterSpindleControl() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

    if (ESP.getFreeHeap() < 50000) return;

    spriteValueDisplay.createSprite(230, 60);
    spriteValueDisplay.setColorDepth(16);
    spritesInitialized = true;
}

void exitSpindleControl() {
    spriteValueDisplay.deleteSprite();
    spritesInitialized = false;
}

void drawSpindleControlScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("SPINDLE CONTROL");

    display.fillRoundRect(5, 40, 230, 60, 5, COLOR_DARKER_BG);
    updateSpindleRPMDisplay();

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 106);
    display.print("DIRECTION");

    drawButton(5,   118, 112, 38, "Fwd", pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(123, 118, 112, 38, "Rev", !pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 166);
    display.print("RPM PRESETS");

    String labels[] = { "6000", "12000", "24000" };
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantSpindle.selectedPreset) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 75, 178, 70, 37, labels[i], bg, COLOR_WHITE, 2);
    }

    drawButton(5,   230, 112, 40, "Start", COLOR_DARK_GREEN, COLOR_WHITE, 2);
    drawButton(123, 230, 112, 40, "Stop",  COLOR_RED,        COLOR_WHITE, 2);
    drawButton(5,   280, 230, 37, "Main Menu", COLOR_BLUE,   COLOR_WHITE, 2);
}

void updateSpindleRPMDisplay() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_SPINDLE_CONTROL) return;
    if (!spriteValueDisplay.getBuffer()) return;

    int spindleRPM;
    String spindleDir;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        spindleRPM = pendantMachine.spindleRPM;
        spindleDir = pendantMachine.spindleDir;
        xSemaphoreGive(stateMutex);
    } else {
        spindleRPM = pendantMachine.spindleRPM;
        spindleDir = pendantMachine.spindleDir;
    }

    spriteValueDisplay.fillSprite(COLOR_DARKER_BG);
    spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteValueDisplay.setTextSize(1);
    spriteValueDisplay.setCursor(5, 5);
    spriteValueDisplay.print("RPM");
    spriteValueDisplay.setTextColor(COLOR_ORANGE);
    spriteValueDisplay.setTextSize(4);
    spriteValueDisplay.setCursor(5, 20);
    spriteValueDisplay.print(spindleRPM);
    spriteValueDisplay.setTextColor(COLOR_CYAN);
    spriteValueDisplay.setTextSize(2);
    spriteValueDisplay.setCursor(155, 30);
    spriteValueDisplay.print(spindleDir);
    spriteValueDisplay.pushSprite(5, 40);
}

void redrawSpindleDirectionButtons() {
    if (currentPendantScreen != PSCREEN_SPINDLE_CONTROL) return;
    drawButton(5,   118, 112, 38, "Fwd", pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(123, 118, 112, 38, "Rev", !pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    updateSpindleRPMDisplay();
}

void redrawSpindlePresetButtons() {
    if (currentPendantScreen != PSCREEN_SPINDLE_CONTROL) return;
    String labels[] = { "6000", "12000", "24000" };
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantSpindle.selectedPreset) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 75, 178, 70, 37, labels[i], bg, COLOR_WHITE, 2);
    }
    updateSpindleRPMDisplay();
}

void handleSpindleControlTouch(int x, int y) {
    if (isTouchInBounds(x, y, 5, 118, 112, 38)) {
        pendantSpindle.directionFwd = true;
        pendantMachine.spindleDir   = "Fwd";
        redrawSpindleDirectionButtons();
    } else if (isTouchInBounds(x, y, 123, 118, 112, 38)) {
        pendantSpindle.directionFwd = false;
        pendantMachine.spindleDir   = "Rev";
        redrawSpindleDirectionButtons();
    }

    int presets[] = { 6000, 12000, 24000 };
    for (int i = 0; i < 3; i++) {
        if (isTouchInBounds(x, y, 5 + i * 75, 178, 70, 37)) {
            pendantSpindle.selectedPreset = i;
            pendantMachine.spindleRPM     = presets[i];
            redrawSpindlePresetButtons();
            return;
        }
    }

    if (isTouchInBounds(x, y, 5, 230, 112, 40)) {
        drawButton(5, 230, 112, 40, "Start", COLOR_WHITE, COLOR_DARK_GREEN, 2);
        delay_ms(150);
        drawButton(5, 230, 112, 40, "Start", COLOR_DARK_GREEN, COLOR_WHITE, 2);
        if (pendantConnected) {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "M3 S%d", pendantMachine.spindleRPM);
            send_line(cmd);
        }
        pendantMachine.spindleRunning = true;
    } else if (isTouchInBounds(x, y, 123, 230, 112, 40)) {
        drawButton(123, 230, 112, 40, "Stop", COLOR_WHITE, COLOR_RED, 2);
        delay_ms(150);
        drawButton(123, 230, 112, 40, "Stop", COLOR_RED, COLOR_WHITE, 2);
        if (pendantConnected) send_line("M5");
        pendantMachine.spindleRunning = false;
    }

    if (isTouchInBounds(x, y, 5, 280, 230, 37)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    }
}
