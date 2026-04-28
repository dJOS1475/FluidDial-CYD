#include "pendant_shared.h"
#include "screen_probing_work.h"

void enterProbingWork() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();

    if (ESP.getFreeHeap() < 50000) return;

    spriteAxisDisplay.createSprite(230, 45);
    spriteAxisDisplay.setColorDepth(16);
    spriteValueDisplay.createSprite(230, 45);
    spriteValueDisplay.setColorDepth(16);
}

void exitProbingWork() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
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

    {
        const char* axisLabels[] = { "X", "Y", "Z", "A" };
        int numAx     = pendantMachine.numAxes;
        int totalBtns = numAx + 1;
        int btnW      = 230 / totalBtns;
        for (int i = 0; i < numAx; i++)
            drawButton(5 + i * btnW, 230, btnW - 2, 38, axisLabels[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
        drawButton(5 + numAx * btnW, 230, btnW - 2, 38, "ALL", COLOR_DARK_GREEN, COLOR_WHITE, 2);
    }

    drawButton(5,   277, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(123, 277, 112, 40, "Jog",       COLOR_BLUE, COLOR_WHITE, 2);
}

void updateWorkMachinePos() {
    if (currentPendantScreen != PSCREEN_PROBING_WORK) return;
    if (!spriteAxisDisplay.getBuffer()) return;

    float px, py, pz, pa;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        px = pendantMachine.workX; py = pendantMachine.workY;
        pz = pendantMachine.workZ; pa = pendantMachine.workA;
        xSemaphoreGive(stateMutex);
    } else {
        px = pendantMachine.workX; py = pendantMachine.workY;
        pz = pendantMachine.workZ; pa = pendantMachine.workA;
    }

    const char* axisNames[] = { "X", "Y", "Z", "A" };
    float       positions[] = { px, py, pz, pa };
    spriteAxisDisplay.fillSprite(COLOR_BACKGROUND);
    spriteAxisDisplay.setTextColor(COLOR_ORANGE);
    spriteAxisDisplay.setTextSize(2);
    for (int i = 0; i < pendantMachine.numAxes; i++) {
        spriteAxisDisplay.setCursor((i % 2) ? 120 : 0, 5 + (i / 2) * 20);
        spriteAxisDisplay.print(axisNames[i]); spriteAxisDisplay.print(":"); spriteAxisDisplay.print(positions[i], 1);
    }
    spriteAxisDisplay.pushSprite(5, 108);
}

void updateWorkAreaPos() {
    if (currentPendantScreen != PSCREEN_PROBING_WORK) return;
    if (!spriteValueDisplay.getBuffer()) return;

    float wx, wy, wz, wa;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        wx = pendantMachine.posX; wy = pendantMachine.posY;
        wz = pendantMachine.posZ; wa = pendantMachine.posA;
        xSemaphoreGive(stateMutex);
    } else {
        wx = pendantMachine.posX; wy = pendantMachine.posY;
        wz = pendantMachine.posZ; wa = pendantMachine.posA;
    }

    const char* wAxisNames[] = { "X", "Y", "Z", "A" };
    float       workPos[]    = { wx, wy, wz, wa };
    spriteValueDisplay.fillSprite(COLOR_BACKGROUND);
    spriteValueDisplay.setTextColor(COLOR_CYAN);
    spriteValueDisplay.setTextSize(2);
    for (int i = 0; i < pendantMachine.numAxes; i++) {
        spriteValueDisplay.setCursor((i % 2) ? 120 : 0, 5 + (i / 2) * 20);
        spriteValueDisplay.print(wAxisNames[i]); spriteValueDisplay.print(":"); spriteValueDisplay.print(workPos[i], 1);
    }
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

    // Set Work Zero buttons — P number matches selected coord system (G54=P1 … G57=P4)
    {
        const char* axisLabels[] = { "X", "Y", "Z", "A" };
        const char* axisLetters[] = { "X", "Y", "Z", "A" };
        int pNum      = pendantProbing.selectedCoordIndex + 1;  // G54→1, G55→2, G56→3, G57→4
        int numAx     = pendantMachine.numAxes;
        int totalBtns = numAx + 1;
        int btnW      = 230 / totalBtns;
        for (int i = 0; i < numAx; i++) {
            if (isTouchInBounds(x, y, 5 + i * btnW, 230, btnW - 2, 38)) {
                drawButton(5 + i * btnW, 230, btnW - 2, 38, axisLabels[i], COLOR_WHITE, COLOR_DARK_GREEN, 3);
                delay_ms(150);
                drawButton(5 + i * btnW, 230, btnW - 2, 38, axisLabels[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
                if (pendantConnected) {
                    char cmd[32];
                    snprintf(cmd, sizeof(cmd), "G10 L20 P%d %s0", pNum, axisLetters[i]);
                    send_line(cmd);
                }
                return;
            }
        }
        int allX = 5 + numAx * btnW;
        if (isTouchInBounds(x, y, allX, 230, btnW - 2, 38)) {
            drawButton(allX, 230, btnW - 2, 38, "ALL", COLOR_WHITE, COLOR_DARK_GREEN, 2);
            delay_ms(150);
            drawButton(allX, 230, btnW - 2, 38, "ALL", COLOR_DARK_GREEN, COLOR_WHITE, 2);
            if (pendantConnected) {
                char cmd[48];
                snprintf(cmd, sizeof(cmd), "G10 L20 P%d", pNum);
                const char* letters[] = { " X0", " Y0", " Z0", " A0" };
                for (int i = 0; i < numAx; i++) strncat(cmd, letters[i], sizeof(cmd) - strlen(cmd) - 1);
                send_line(cmd);
            }
            return;
        }
    }

    if      (isTouchInBounds(x, y, 5,   277, 112, 40)) currentPendantScreen = PSCREEN_MAIN_MENU;
    else if (isTouchInBounds(x, y, 123, 277, 112, 40)) currentPendantScreen = PSCREEN_JOG_HOMING;
}
