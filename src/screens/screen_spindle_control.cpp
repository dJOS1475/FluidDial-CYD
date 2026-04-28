#include "pendant_shared.h"
#include "screen_spindle_control.h"
#include "../CNC_Pendant_UI.h"

// Compute dynamic presets: 25%, 50%, 100% of max, floored to nearest 100 RPM
static void getSpindlePresets(int presets[3]) {
    int maxRPM = pendantMachine.spindleMaxRPM > 0 ? pendantMachine.spindleMaxRPM : 24000;
    int minRPM = pendantMachine.spindleMinRPM;
    presets[0] = max(minRPM, (maxRPM / 4 / 100) * 100);
    presets[1] = max(minRPM, (maxRPM / 2 / 100) * 100);
    presets[2] = maxRPM;
}

// Format RPM as "Xk" if divisible by 1000, else plain number
static String fmtRPM(int rpm) {
    if (rpm >= 1000 && rpm % 1000 == 0) return String(rpm / 1000) + "k";
    return String(rpm);
}

void enterSpindleControl() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();

    // Defensive re-fetch of $30/$31 on entry. The connect-edge fetch is the
    // primary source, but if it was dropped (UART contention at connect time)
    // we'd silently fall back to default 24000/0 — observable as preset
    // buttons reverting to defaults after a Start/Stop cycle. Re-querying
    // here costs two short bytes and guarantees current values.
    requestSpindleConfig();

    // Initialise targetRPM from selected preset on first entry
    if (pendantSpindle.targetRPM == 0) {
        int presets[3];
        getSpindlePresets(presets);
        pendantSpindle.targetRPM = presets[pendantSpindle.selectedPreset];
    }

    if (ESP.getFreeHeap() < 50000) return;

    spriteValueDisplay.createSprite(230, 60);
    spriteValueDisplay.setColorDepth(16);
}

void exitSpindleControl() {
    spriteValueDisplay.deleteSprite();
}

void drawSpindleControlScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("SPINDLE CONTROL");

    display.fillRoundRect(5, 40, 230, 60, 5, COLOR_DARKER_BG);
    updateSpindleRPMDisplay();

    drawButton(5,   110, 112, 38, "Fwd", pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(123, 110, 112, 38, "Rev", !pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

    // Min/Max from controller
    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 152);
    display.printf("Min: %d  Max: %d RPM", pendantMachine.spindleMinRPM, pendantMachine.spindleMaxRPM);

    // 3 preset buttons + 1 Dial button, 4 across 230px: w=56, spacing=58
    int presets[3];
    getSpindlePresets(presets);
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (!pendantSpindle.dialMode && i == pendantSpindle.selectedPreset)
                      ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 58, 163, 56, 37, fmtRPM(presets[i]), bg, COLOR_WHITE, 2);
    }
    // Dial button — teal background, highlighted when dial mode active
    uint16_t dialBg = pendantSpindle.dialMode ? COLOR_TEAL_BRIGHT : COLOR_TEAL;
    drawButton(179, 163, 56, 37, "Dial", dialBg, COLOR_WHITE, 2);

    drawButton(5,   218, 112, 40, "Start", COLOR_DARK_GREEN, COLOR_WHITE, 2);
    drawButton(123, 218, 112, 40, "Stop",  COLOR_RED,        COLOR_WHITE, 2);
    drawButton(5,   268, 230, 37, "Main Menu", COLOR_BLUE,   COLOR_WHITE, 2);
}

void updateSpindleRPMDisplay() {
    if (currentPendantScreen != PSCREEN_SPINDLE_CONTROL) return;
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

    // Left column — current actual spindle RPM
    spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteValueDisplay.setTextSize(1);
    spriteValueDisplay.setCursor(5, 5);
    spriteValueDisplay.print("RPM");
    spriteValueDisplay.setTextColor(COLOR_ORANGE);
    spriteValueDisplay.setTextSize(3);
    spriteValueDisplay.setCursor(5, 22);
    spriteValueDisplay.print(spindleRPM);

    // Right column — user-selected target RPM
    spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteValueDisplay.setTextSize(1);
    spriteValueDisplay.setCursor(120, 5);
    spriteValueDisplay.print("Target RPM");
    spriteValueDisplay.setTextColor(COLOR_DARK_GREEN);
    spriteValueDisplay.setTextSize(3);
    spriteValueDisplay.setCursor(120, 22);
    spriteValueDisplay.print(pendantSpindle.targetRPM);

    spriteValueDisplay.pushSprite(5, 40);
}

void redrawSpindleDirectionButtons() {
    if (currentPendantScreen != PSCREEN_SPINDLE_CONTROL) return;
    drawButton(5,   110, 112, 38, "Fwd", pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(123, 110, 112, 38, "Rev", !pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    updateSpindleRPMDisplay();
}

void redrawSpindlePresetButtons() {
    if (currentPendantScreen != PSCREEN_SPINDLE_CONTROL) return;
    int presets[3];
    getSpindlePresets(presets);
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (!pendantSpindle.dialMode && i == pendantSpindle.selectedPreset)
                      ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 58, 163, 56, 37, fmtRPM(presets[i]), bg, COLOR_WHITE, 2);
    }
    uint16_t dialBg = pendantSpindle.dialMode ? display.color565(0, 180, 180) : display.color565(0, 100, 100);
    drawButton(179, 163, 56, 37, "Dial", dialBg, COLOR_WHITE, 2);
    updateSpindleRPMDisplay();
}

void handleSpindleControlTouch(int x, int y) {
    if (isTouchInBounds(x, y, 5, 110, 112, 38)) {
        pendantSpindle.directionFwd = true;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            pendantMachine.spindleDir = "Fwd";
            xSemaphoreGive(stateMutex);
        }
        redrawSpindleDirectionButtons();
        return;
    } else if (isTouchInBounds(x, y, 123, 110, 112, 38)) {
        pendantSpindle.directionFwd = false;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            pendantMachine.spindleDir = "Rev";
            xSemaphoreGive(stateMutex);
        }
        redrawSpindleDirectionButtons();
        return;
    }

    int presets[3];
    getSpindlePresets(presets);
    for (int i = 0; i < 3; i++) {
        if (isTouchInBounds(x, y, 5 + i * 58, 163, 56, 37)) {
            pendantSpindle.dialMode       = false;
            pendantSpindle.selectedPreset = i;
            pendantSpindle.targetRPM      = presets[i];
            redrawSpindlePresetButtons();
            return;
        }
    }
    // Dial button — toggle dial mode
    if (isTouchInBounds(x, y, 179, 163, 56, 37)) {
        pendantSpindle.dialMode = !pendantSpindle.dialMode;
        // Clamp targetRPM to valid range when entering dial mode
        if (pendantSpindle.dialMode) {
            int maxRPM = pendantMachine.spindleMaxRPM > 0 ? pendantMachine.spindleMaxRPM : 24000;
            int minRPM = pendantMachine.spindleMinRPM;
            pendantSpindle.targetRPM = constrain(pendantSpindle.targetRPM, minRPM, maxRPM);
        }
        redrawSpindlePresetButtons();
        return;
    }

    if (isTouchInBounds(x, y, 5, 218, 112, 40)) {
        drawButton(5, 218, 112, 40, "Start", COLOR_WHITE, COLOR_DARK_GREEN, 2);
        if (pendantConnected) {
            char cmd[32];
            // M3 = clockwise (Fwd), M4 = counterclockwise (Rev)
            snprintf(cmd, sizeof(cmd), "%s S%d", pendantSpindle.directionFwd ? "M3" : "M4", pendantSpindle.targetRPM);
            send_line(cmd);
        }
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            pendantMachine.spindleRunning = true;
            xSemaphoreGive(stateMutex);
        }
        delay_ms(150);
        drawButton(5, 218, 112, 40, "Start", COLOR_DARK_GREEN, COLOR_WHITE, 2);
        return;
    }

    if (isTouchInBounds(x, y, 123, 218, 112, 40)) {
        drawButton(123, 218, 112, 40, "Stop", COLOR_WHITE, COLOR_RED, 2);
        if (pendantConnected) send_line("M5");
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            pendantMachine.spindleRunning = false;
            xSemaphoreGive(stateMutex);
        }
        delay_ms(150);
        drawButton(123, 218, 112, 40, "Stop", COLOR_RED, COLOR_WHITE, 2);
        return;
    }

    // Any tap below the Start/Stop row (y > 258) navigates to Main Menu.
    // Full screen width and no bottom limit so edge taps always register.
    if (y > 258) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    }
}
