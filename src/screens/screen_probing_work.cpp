#include "pendant_shared.h"
#include "screen_probing_work.h"

void enterProbingWork() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

    if (ESP.getFreeHeap() < 50000) return;

    spriteAxisDisplay.createSprite(230, 45);
    spriteAxisDisplay.setColorDepth(16);
    spriteValueDisplay.createSprite(230, 45);
    spriteValueDisplay.setColorDepth(16);
    spritesInitialized = true;
}

void exitProbingWork() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spritesInitialized = false;
}

void drawProbingWorkScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("WORK AREA");

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 43);
    display.print("COORDINATE SYSTEM");

    redrawWorkCoordButtons();

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 100);
    display.print("MACHINE POS");
    updateWorkMachinePos();

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 158);
    display.print("WORK POS");
    updateWorkAreaPos();

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 218);
    display.print("SET WORK ZERO");

    drawButton(5,   230, 46, 38, "X",   COLOR_DARK_GREEN, COLOR_WHITE, 3);
    drawButton(52,  230, 46, 38, "Y",   COLOR_DARK_GREEN, COLOR_WHITE, 3);
    drawButton(99,  230, 46, 38, "Z",   COLOR_DARK_GREEN, COLOR_WHITE, 3);
    drawButton(146, 230, 46, 38, "A",   COLOR_DARK_GREEN, COLOR_WHITE, 3);
    drawButton(193, 230, 46, 38, "ALL", COLOR_DARK_GREEN, COLOR_WHITE, 2);

    drawButton(5,   277, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(123, 277, 112, 40, "Jog",       COLOR_BLUE, COLOR_WHITE, 2);
}

void updateWorkMachinePos() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_PROBING_WORK) return;
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

    spriteAxisDisplay.fillSprite(COLOR_BACKGROUND);
    spriteAxisDisplay.setTextColor(COLOR_ORANGE);
    spriteAxisDisplay.setTextSize(2);
    spriteAxisDisplay.setCursor(0,   5); spriteAxisDisplay.print("X:"); spriteAxisDisplay.print(px, 1);
    spriteAxisDisplay.setCursor(120, 5); spriteAxisDisplay.print("Y:"); spriteAxisDisplay.print(py, 1);
    spriteAxisDisplay.setCursor(0,   25); spriteAxisDisplay.print("Z:"); spriteAxisDisplay.print(pz, 1);
    spriteAxisDisplay.setCursor(120, 25); spriteAxisDisplay.print("A:"); spriteAxisDisplay.print(pa, 1);
    spriteAxisDisplay.pushSprite(5, 108);
}

void updateWorkAreaPos() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_PROBING_WORK) return;
    if (!spriteValueDisplay.getBuffer()) return;

    float wx, wy, wz, wa;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        wx = pendantMachine.workX; wy = pendantMachine.workY;
        wz = pendantMachine.workZ; wa = pendantMachine.workA;
        xSemaphoreGive(stateMutex);
    } else {
        wx = pendantMachine.workX; wy = pendantMachine.workY;
        wz = pendantMachine.workZ; wa = pendantMachine.workA;
    }

    spriteValueDisplay.fillSprite(COLOR_BACKGROUND);
    spriteValueDisplay.setTextColor(COLOR_CYAN);
    spriteValueDisplay.setTextSize(2);
    spriteValueDisplay.setCursor(0,   5); spriteValueDisplay.print("X:"); spriteValueDisplay.print(wx, 1);
    spriteValueDisplay.setCursor(120, 5); spriteValueDisplay.print("Y:"); spriteValueDisplay.print(wy, 1);
    spriteValueDisplay.setCursor(0,   25); spriteValueDisplay.print("Z:"); spriteValueDisplay.print(wz, 1);
    spriteValueDisplay.setCursor(120, 25); spriteValueDisplay.print("A:"); spriteValueDisplay.print(wa, 1);
    spriteValueDisplay.pushSprite(5, 166);
}

void redrawWorkCoordButtons() {
    String coordSystems[] = { "G54", "G55", "G56", "G57" };
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (i == pendantProbing.selectedCoordIndex) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 56, 55, 52, 38, coordSystems[i], bg, COLOR_WHITE, 2);
    }
}

void handleProbingWorkTouch(int x, int y) {
    // Coordinate system selection
    String coords[] = { "G54", "G55", "G56", "G57" };
    for (int i = 0; i < 4; i++) {
        if (isTouchInBounds(x, y, 5 + i * 56, 55, 52, 38)) {
            pendantProbing.selectedCoordIndex  = i;
            pendantProbing.selectedCoordSystem = coords[i];
            redrawWorkCoordButtons();
            if (pendantConnected) send_line(coords[i].c_str());
            return;
        }
    }

    // Set Work Zero buttons
    struct { int bx; const char* axis; const char* cmd; } zeros[] = {
        { 5,   "X",   "G10 L20 P1 X0"         },
        { 52,  "Y",   "G10 L20 P1 Y0"         },
        { 99,  "Z",   "G10 L20 P1 Z0"         },
        { 146, "A",   "G10 L20 P1 A0"         },
        { 193, "ALL", "G10 L20 P1 X0 Y0 Z0 A0" },
    };
    int btnW[] = { 46, 46, 46, 46, 46 };
    int textSz[] = { 3, 3, 3, 3, 2 };
    for (int i = 0; i < 5; i++) {
        if (isTouchInBounds(x, y, zeros[i].bx, 230, 46, 38)) {
            drawButton(zeros[i].bx, 230, 46, 38, zeros[i].axis, COLOR_WHITE, COLOR_DARK_GREEN, textSz[i]);
            delay_ms(150);
            drawButton(zeros[i].bx, 230, 46, 38, zeros[i].axis, COLOR_DARK_GREEN, COLOR_WHITE, textSz[i]);
            if (pendantConnected) send_line(zeros[i].cmd);
            return;
        }
    }

    if      (isTouchInBounds(x, y, 5,   277, 112, 40)) currentPendantScreen = PSCREEN_MAIN_MENU;
    else if (isTouchInBounds(x, y, 123, 277, 112, 40)) currentPendantScreen = PSCREEN_JOG_HOMING;
}
