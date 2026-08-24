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
        int numAx     = pendantMachine.numAxes;
        int totalBtns = numAx + 1;
        int btnW      = 230 / totalBtns;
        for (int i = 0; i < numAx; i++)
            drawButton(5 + i * btnW, 230, btnW - 2, 38, AXIS_NAMES[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
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

    // MACHINE POS panel.  Snapshot under the lock — the previous version took the
    // mutex only to fill four locals that nothing read, then did the real array
    // read outside it, which is the opposite of the intent.
    // Sized PENDANT_MAX_AXES; numAxes is clamped to that in onDROChange.
    float machinePos[PENDANT_MAX_AXES];
    int   numAxes;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    for (int i = 0; i < PENDANT_MAX_AXES; i++) machinePos[i] = pendantMachine.mpos[i];
    numAxes = pendantMachine.numAxes;
    xSemaphoreGive(stateMutex);

    // Three columns once there are more than 4 axes — see screen_status.
    const int nCols = (numAxes > 4) ? 3 : 2;
    if (hasSprite) spriteAxisDisplay.fillSprite(COLOR_BACKGROUND);
    else           display.fillRect(5, 108, 230, 45, COLOR_BACKGROUND);
    g->setTextColor(COLOR_ORANGE);
    g->setTextSize(2);
    for (int i = 0; i < numAxes; i++) {
        g->setCursor(ox + (i % nCols) * (230 / nCols), oy + 5 + (i / nCols) * 20);
        g->print(AXIS_NAMES[i]); g->print(":"); g->print(machinePos[i], 1);
    }
    if (hasSprite) spriteAxisDisplay.pushSprite(5, 108);
}

void updateWorkAreaPos() {
    if (currentPendantScreen != PSCREEN_PROBING_WORK) return;

    const bool hasSprite = spriteValueDisplay.getBuffer() != nullptr;  // pushed at (5, 166)
    LovyanGFX*  g  = hasSprite ? (LovyanGFX*)&spriteValueDisplay : (LovyanGFX*)&display;
    const int   ox = hasSprite ? 0 : 5;
    const int   oy = hasSprite ? 0 : 166;

    // WORK POS panel — snapshot under the lock, as above.
    float workPos[PENDANT_MAX_AXES];
    int   numAxes;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    for (int i = 0; i < PENDANT_MAX_AXES; i++) workPos[i] = pendantMachine.wpos[i];
    numAxes = pendantMachine.numAxes;
    xSemaphoreGive(stateMutex);

    const int nCols = (numAxes > 4) ? 3 : 2;
    if (hasSprite) spriteValueDisplay.fillSprite(COLOR_BACKGROUND);
    else           display.fillRect(5, 166, 230, 45, COLOR_BACKGROUND);
    g->setTextColor(COLOR_CYAN);
    g->setTextSize(2);
    for (int i = 0; i < numAxes; i++) {
        g->setCursor(ox + (i % nCols) * (230 / nCols), oy + 5 + (i / nCols) * 20);
        g->print(AXIS_NAMES[i]); g->print(":"); g->print(workPos[i], 1);
    }
    if (hasSprite) spriteValueDisplay.pushSprite(5, 166);
}

// Coordinate-system row — on the shared 5..235 grid (was pitch 56 / w 52, which
// left 5 px on the left but 15 px on the right).
static const int WCS_Y = 55;
static const int WCS_H = 38;

void redrawWorkCoordButtons() {
    String coordSystems[] = { "G54", "G55", "G56", "G57" };
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (i == pendantProbing.selectedCoordIndex) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(rowBtnX(4, i), WCS_Y, rowBtnWAt(4, i), WCS_H, coordSystems[i], bg, COLOR_WHITE, 2);
    }
}

void handleProbingWorkTouch(int x, int y) {
    // Coordinate system selection
    String coords[] = { "G54", "G55", "G56", "G57" };
    for (int i = 0; i < 4; i++) {
        if (isTouchInBounds(x, y, rowBtnX(4, i), WCS_Y, rowBtnWAt(4, i), WCS_H)) {
            pendantProbing.selectedCoordIndex  = i;
            pendantProbing.selectedCoordSystem = coords[i];
            redrawWorkCoordButtons();
            if (pendantConnected) send_line(coords[i].c_str());
            return;
        }
    }

    // Set Work Zero buttons — P number matches selected coord system (G54=P1 … G57=P4)
    {
        int pNum      = pendantProbing.selectedCoordIndex + 1;  // G54→1, G55→2, G56→3, G57→4
        int numAx     = pendantMachine.numAxes;
        int totalBtns = numAx + 1;
        int btnW      = 230 / totalBtns;
        for (int i = 0; i < numAx; i++) {
            if (isTouchInBounds(x, y, 5 + i * btnW, 230, btnW - 2, 38)) {
                drawButton(5 + i * btnW, 230, btnW - 2, 38, AXIS_NAMES[i], COLOR_WHITE, COLOR_DARK_GREEN, 3);
                delay_ms(150);
                drawButton(5 + i * btnW, 230, btnW - 2, 38, AXIS_NAMES[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
                if (pendantConnected) {
                    char cmd[32];
                    snprintf(cmd, sizeof(cmd), "G10 L20 P%d %s0", pNum, AXIS_NAMES[i]);
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
