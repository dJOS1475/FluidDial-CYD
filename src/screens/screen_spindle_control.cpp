#include "pendant_shared.h"
#include "screen_spindle_control.h"
#include "../CNC_Pendant_UI.h"
#include "screen_probe.h"   // PROBE_* colours — shared adjustable-field style

// Dial toggle drawn in the Probe screens' adjustable-field style: a bordered
// box that highlights (yellow border + text) while dial mode is active.  Label
// only — no value — because the target RPM is shown in the readout panel above.
static void drawSpindleDialButton() {
    bool     active = pendantSpindle.dialMode;
    uint16_t bg     = active ? PROBE_SEL_BG   : PROBE_BG_SCREEN;
    uint16_t bdr    = active ? PROBE_C_YELLOW : PROBE_C_DIMBLUE;
    display.fillRoundRect(179, 163, 56, 37, 2, bg);
    display.drawRoundRect(179, 163, 56, 37, 2, bdr);
    display.setTextSize(2);
    display.setTextColor(active ? PROBE_C_YELLOW : COLOR_TEAL_BRIGHT);
    int16_t tw = display.textWidth("Dial");
    display.setCursor(179 + (56 - tw) / 2, 163 + (37 - 16) / 2);
    display.print("Dial");
}

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
    releasePanelSprites();

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

    // RPM panel uses a transient 16-bit sprite (see updateSpindleRPMDisplay).
}

void exitSpindleControl() {
    releasePanelSprites();
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
    // Dial toggle — adjustable-field style (label only; target RPM shown above)
    drawSpindleDialButton();

    drawButton(5,   218, 112, 40, "Start", COLOR_DARK_GREEN, COLOR_WHITE, 2);
    drawButton(123, 218, 112, 40, "Stop",  COLOR_RED,        COLOR_WHITE, 2);
    drawButton(5,   268, 230, 37, "Main Menu", COLOR_BLUE,   COLOR_WHITE, 2);
}

void updateSpindleRPMDisplay() {
    if (currentPendantScreen != PSCREEN_SPINDLE_CONTROL) return;

    int spindleRPM;
    String spindleDir;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    spindleRPM = pendantMachine.spindleRPM;
    spindleDir = pendantMachine.spindleDir;
    xSemaphoreGive(stateMutex);

    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, 60, ox, oy, 5, 40);
    g->fillRect(ox, oy, 230, 60, COLOR_DARKER_BG);

    // Left column — current actual spindle RPM
    g->setTextColor(COLOR_GRAY_TEXT);
    g->setTextSize(1);
    g->setCursor(ox + 5, oy + 5);
    g->print("RPM");
    g->setTextColor(COLOR_ORANGE);
    g->setTextSize(3);
    g->setCursor(ox + 5, oy + 22);
    g->print(spindleRPM);

    // Right column — user-selected target RPM
    g->setTextColor(COLOR_GRAY_TEXT);
    g->setTextSize(1);
    g->setCursor(ox + 120, oy + 5);
    g->print("Target RPM");
    g->setTextColor(COLOR_DARK_GREEN);
    g->setTextSize(3);
    g->setCursor(ox + 120, oy + 22);
    g->print(pendantSpindle.targetRPM);

    endPanelSprite(230, 60, 5, 40);
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
    drawSpindleDialButton();
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
