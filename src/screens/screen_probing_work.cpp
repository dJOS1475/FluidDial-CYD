#include "pendant_shared.h"
#include "screen_probing_work.h"

// Machine Pos and Work Pos share one framed panel (y 98..212), split by a rule,
// like the MPos/WPos panel on Jog & Homing.  Each half: caption + up to two
// value rows.
static const int WA_PANEL_Y = 98;
static const int WA_PANEL_H = 115;
static const int WA_RULE_Y  = 155;
static const int WA_MPOS_CAP_Y = 102;
static const int WA_WPOS_CAP_Y = 159;

void enterProbingWork() {
    // The value blocks draw into the shared 16-bit panel scratch (navy fill —
    // the 8-bit sprites these used to have can't hold it without a tint).
    releasePanelSprites();
}

void exitProbingWork() {
    releasePanelSprites();
}

// SET WORK ZERO "ALL": with 5-6 axes the slot is only 36 / 30 px — too narrow
// for 9pt bold — so it drops to bold Font2.
static void drawAllButton(int x, int w, bool pressed) {
    uint16_t bg = pressed ? COLOR_WHITE : COLOR_DARK_GREEN;
    uint16_t fg = pressed ? COLOR_DARK_GREEN : COLOR_WHITE;
    if (pendantMachine.numAxes >= 5) uiDrawNarrowButton(x, 230, w, 38, "ALL", bg, fg);
    else                             drawButton(x, 230, w, 38, "ALL", bg, fg, 2);
}

void drawProbingWorkScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("WORK AREA");

    display.setTextColor(COLOR_GRAY_TEXT);
    uiText(&display, 1);
    display.setCursor(5, 43);
    display.print("COORDINATE SYSTEM");

    redrawWorkCoordButtons();

    uiPanel(&display, 5, WA_PANEL_Y, 230, WA_PANEL_H, 6);
    display.drawFastHLine(10, WA_RULE_Y, 220, UI_EDGE);
    display.setTextColor(COLOR_GRAY_TEXT);
    uiText(&display, 1);
    display.setCursor(10, WA_MPOS_CAP_Y);
    display.print("MACHINE POS");
    display.setCursor(10, WA_WPOS_CAP_Y);
    display.print("WORK POS");
    updateWorkMachinePos();
    updateWorkAreaPos();

    display.setTextColor(COLOR_GRAY_TEXT);
    uiText(&display, 1);
    display.setCursor(5, 218);
    display.print("SET WORK ZERO");

    {
        int numAx     = pendantMachine.numAxes;
        int totalBtns = numAx + 1;
        int btnW      = 230 / totalBtns;
        for (int i = 0; i < numAx; i++)
            drawButton(5 + i * btnW, 230, btnW - 2, 38, AXIS_NAMES[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
        drawAllButton(5 + numAx * btnW, btnW - 2, false);
    }

    drawButton(5,   277, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(123, 277, 112, 40, "Jog",       COLOR_BLUE, COLOR_WHITE, 2);
}

// One readout block inside the panel: 226 x 42 at (7, capY + 10), navy, values
// in up to three columns (three once there are more than 4 axes — see
// screen_status).  Shared 16-bit scratch, direct-draw fallback.
static void drawPosBlock(const float* pos, int numAxes, uint16_t color, int capY) {
    const int px = 7, py = capY + 10, w = 226, h = 42;
    int ox, oy;
    LovyanGFX* g = beginPanelSprite(w, h, ox, oy, px, py);
    g->fillRect(ox, oy, w, h, UI_PANEL_BG);
    const int nCols = (numAxes > 4) ? 3 : 2;
    g->setTextColor(color);
    uiText(g, 2);
    for (int i = 0; i < numAxes; i++) {
        g->setCursor(ox + 3 + (i % nCols) * (220 / nCols), oy + 1 + (i / nCols) * 20);
        g->print(AXIS_NAMES[i]); g->print(":"); g->print(pos[i], 1);
    }
    uiText(g, 1);
    endPanelSprite(w, h, px, py);
}

void updateWorkMachinePos() {
    if (currentPendantScreen != PSCREEN_PROBING_WORK) return;
    // Snapshot under the lock.  Sized PENDANT_MAX_AXES; numAxes is clamped to
    // that in onDROChange.
    float machinePos[PENDANT_MAX_AXES];
    int   numAxes;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    for (int i = 0; i < PENDANT_MAX_AXES; i++) machinePos[i] = pendantMachine.mpos[i];
    numAxes = pendantMachine.numAxes;
    xSemaphoreGive(stateMutex);
    drawPosBlock(machinePos, numAxes, COLOR_ORANGE, WA_MPOS_CAP_Y);
}

void updateWorkAreaPos() {
    if (currentPendantScreen != PSCREEN_PROBING_WORK) return;
    float workPos[PENDANT_MAX_AXES];
    int   numAxes;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    for (int i = 0; i < PENDANT_MAX_AXES; i++) workPos[i] = pendantMachine.wpos[i];
    numAxes = pendantMachine.numAxes;
    xSemaphoreGive(stateMutex);
    drawPosBlock(workPos, numAxes, COLOR_CYAN, WA_WPOS_CAP_Y);
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
            drawAllButton(allX, btnW - 2, true);    // brief press flash
            delay_ms(150);
            drawAllButton(allX, btnW - 2, false);
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
