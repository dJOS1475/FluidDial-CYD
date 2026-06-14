#include "pendant_shared.h"
#include "screen_probing_work.h"

void enterProbingWork() {
    releasePanelSprites();
    // These panels fill with COLOR_BACKGROUND (black), which is identical in 8-
    // and 16-bit — so they keep the cheaper persistent 8-bit allocation (no
    // rgb332 colour tint to worry about, unlike the grey panels which use the
    // 16-bit beginPanelSprite scratch).  update*() falls back to direct draw if
    // allocation fails.
    allocPanelSprite(spriteAxisDisplay,  230, 45, 40000);
    allocPanelSprite(spriteValueDisplay, 230, 45);
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

    const bool hasSprite = spriteAxisDisplay.getBuffer() != nullptr;  // pushed at (5, 108)
    LovyanGFX*  g  = hasSprite ? (LovyanGFX*)&spriteAxisDisplay : (LovyanGFX*)&display;
    const int   ox = hasSprite ? 0 : 5;
    const int   oy = hasSprite ? 0 : 108;

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
    if (hasSprite) spriteAxisDisplay.fillSprite(COLOR_BACKGROUND);
    else           display.fillRect(5, 108, 230, 45, COLOR_BACKGROUND);
    g->setTextColor(COLOR_ORANGE);
    g->setTextSize(2);
    for (int i = 0; i < pendantMachine.numAxes; i++) {
        g->setCursor(ox + ((i % 2) ? 120 : 0), oy + 5 + (i / 2) * 20);
        g->print(axisNames[i]); g->print(":"); g->print(positions[i], 1);
    }
    if (hasSprite) spriteAxisDisplay.pushSprite(5, 108);
}

void updateWorkAreaPos() {
    if (currentPendantScreen != PSCREEN_PROBING_WORK) return;

    const bool hasSprite = spriteValueDisplay.getBuffer() != nullptr;  // pushed at (5, 166)
    LovyanGFX*  g  = hasSprite ? (LovyanGFX*)&spriteValueDisplay : (LovyanGFX*)&display;
    const int   ox = hasSprite ? 0 : 5;
    const int   oy = hasSprite ? 0 : 166;

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
    if (hasSprite) spriteValueDisplay.fillSprite(COLOR_BACKGROUND);
    else           display.fillRect(5, 166, 230, 45, COLOR_BACKGROUND);
    g->setTextColor(COLOR_CYAN);
    g->setTextSize(2);
    for (int i = 0; i < pendantMachine.numAxes; i++) {
        g->setCursor(ox + ((i % 2) ? 120 : 0), oy + 5 + (i / 2) * 20);
        g->print(wAxisNames[i]); g->print(":"); g->print(workPos[i], 1);
    }
    if (hasSprite) spriteValueDisplay.pushSprite(5, 166);
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
