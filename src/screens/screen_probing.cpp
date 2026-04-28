#include "pendant_shared.h"
#include "screen_probing.h"

void enterProbing() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();

    if (ESP.getFreeHeap() < 50000) return;

    spriteAxisDisplay.createSprite(230, 50);
    spriteAxisDisplay.setColorDepth(16);
    spriteValueDisplay.createSprite(230, 40);
    spriteValueDisplay.setColorDepth(16);
}

void exitProbing() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
}

void drawProbingScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("PROBE");

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 43);
    display.print("CURRENT POSITION");
    updateProbePositionDisplay();

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 104);
    display.print("PROBE TYPE");

    String probeTypes[] = { "Z Surface", "Tool Height" };
    uint16_t probeColors[] = { COLOR_DARK_GREEN, COLOR_ORANGE };
    for (int i = 0; i < 2; i++) {
        drawButton(5, 116 + i * 43, 230, 38, probeTypes[i], probeColors[i], COLOR_WHITE, 2);
    }

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 214);
    display.print("PROBE SETTINGS");
    updateProbeSettingsDisplay();

    drawButton(5, 280, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void updateProbePositionDisplay() {
    if (currentPendantScreen != PSCREEN_PROBING) return;
    if (!spriteAxisDisplay.getBuffer()) return;

    float px, py, pz, pa;
    int   numAx;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        px = pendantMachine.posX; py = pendantMachine.posY;
        pz = pendantMachine.posZ; pa = pendantMachine.posA;
        numAx = pendantMachine.numAxes;
        xSemaphoreGive(stateMutex);
    } else {
        px = pendantMachine.posX; py = pendantMachine.posY;
        pz = pendantMachine.posZ; pa = pendantMachine.posA;
        numAx = pendantMachine.numAxes;
    }

    spriteAxisDisplay.fillSprite(COLOR_BACKGROUND);
    spriteAxisDisplay.setTextColor(COLOR_ORANGE);
    spriteAxisDisplay.setTextSize(2);
    spriteAxisDisplay.setCursor(0,  7); spriteAxisDisplay.print("X "); spriteAxisDisplay.print(px, 1);
    if (numAx > 1) { spriteAxisDisplay.setCursor(85, 7);  spriteAxisDisplay.print("Y "); spriteAxisDisplay.print(py, 1); }
    if (numAx > 2) { spriteAxisDisplay.setCursor(0,  27); spriteAxisDisplay.print("Z "); spriteAxisDisplay.print(pz, 1); }
    if (numAx > 3) { spriteAxisDisplay.setCursor(85, 27); spriteAxisDisplay.print("A "); spriteAxisDisplay.print(pa, 1); }
    spriteAxisDisplay.pushSprite(5, 57);
}

void updateProbeSettingsDisplay() {
    if (currentPendantScreen != PSCREEN_PROBING) return;
    if (!spriteValueDisplay.getBuffer()) return;

    spriteValueDisplay.fillSprite(COLOR_BACKGROUND);
    spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteValueDisplay.setTextSize(1);
    spriteValueDisplay.setCursor(0, 4);
    spriteValueDisplay.print("Feed Rate:");
    spriteValueDisplay.setTextColor(COLOR_ORANGE);
    spriteValueDisplay.setCursor(165, 4);
    spriteValueDisplay.print(pendantProbe.feedRate, 0);
    spriteValueDisplay.print(" mm/min");

    spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteValueDisplay.setCursor(0, 19);
    spriteValueDisplay.print("Max Travel:");
    spriteValueDisplay.setTextColor(COLOR_ORANGE);
    spriteValueDisplay.setCursor(165, 19);
    spriteValueDisplay.print(pendantProbe.maxTravel, 1);
    spriteValueDisplay.print(" mm");

    spriteValueDisplay.pushSprite(5, 228);
}

// Draw a status message in the area below probe settings
static void showProbeStatus(const char* msg, uint16_t color) {
    display.fillRect(5, 270, 230, 14, COLOR_BACKGROUND);
    display.setTextColor(color);
    display.setTextSize(1);
    display.setCursor(5, 270);
    display.print(msg);
}

void handleProbingTouch(int x, int y) {
    // Macro filenames stored on the FluidNC SD card / local filesystem
    const char* macroFiles[]  = { "probe_work_z.nc", "probe_tool_height.nc" };
    String      probeTypes[]  = { "Z Surface", "Tool Height" };
    uint16_t    probeColors[] = { COLOR_DARK_GREEN, COLOR_ORANGE };

    for (int i = 0; i < 2; i++) {
        int by = 116 + i * 43;
        if (isTouchInBounds(x, y, 5, by, 230, 38)) {
            // Visual press feedback
            drawButton(5, by, 230, 38, probeTypes[i], COLOR_WHITE, probeColors[i], 2);
            delay_ms(150);
            drawButton(5, by, 230, 38, probeTypes[i], probeColors[i], COLOR_WHITE, 2);
            pendantProbe.selectedProbeType = i;

            if (!pendantConnected) {
                showProbeStatus("Not connected", COLOR_RED);
                return;
            }
            // Run the macro file from the controller's local filesystem
            char cmd[48];
            snprintf(cmd, sizeof(cmd), "$Localfs/Run=%s", macroFiles[i]);
            send_line(cmd);
            showProbeStatus("Running...", COLOR_CYAN);
            return;
        }
    }

    if (isTouchInBounds(x, y, 5, 280, 230, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    }
}
