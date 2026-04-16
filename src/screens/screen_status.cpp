#include "pendant_shared.h"
#include "screen_status.h"

void enterStatus() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 50000) {
        dbg_printf("Warning: Low heap (%u bytes), skipping sprite allocation for Status\n", freeHeap);
        return;
    }

    spriteStatusBar.createSprite(230, 50);
    if (!spriteStatusBar.getBuffer()) { spriteStatusBar.deleteSprite(); }
    else spriteStatusBar.setColorDepth(16);

    spriteAxisDisplay.createSprite(230, 65);
    if (!spriteAxisDisplay.getBuffer()) { spriteAxisDisplay.deleteSprite(); }
    else spriteAxisDisplay.setColorDepth(16);

    spriteValueDisplay.createSprite(230, 65);
    if (!spriteValueDisplay.getBuffer()) { spriteValueDisplay.deleteSprite(); }
    else spriteValueDisplay.setColorDepth(16);

    spriteFileDisplay.createSprite(230, 40);
    if (!spriteFileDisplay.getBuffer()) { spriteFileDisplay.deleteSprite(); }
    else spriteFileDisplay.setColorDepth(16);

    spritesInitialized = true;
    dbg_printf("Status sprites allocated. Free heap: %u\n", ESP.getFreeHeap());
}

void exitStatus() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;
}

void drawStatusScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("STATUS");

    updateStatusMachineStatus();
    updateStatusCurrentFile();
    updateStatusAxisPositions();
    updateStatusFeedSpindle();

    drawButton(5, 280, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(123, 280, 112, 40, "FluidNC", COLOR_BLUE, COLOR_WHITE, 2);
}

void updateStatusMachineStatus() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_STATUS) return;
    if (!spriteStatusBar.getBuffer()) return;

    spriteStatusBar.fillSprite(COLOR_DARKER_BG);

    String statusStr;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        statusStr = pendantMachine.status;
        xSemaphoreGive(stateMutex);
    } else {
        statusStr = pendantMachine.status;
    }

    if (statusStr.startsWith("Alarm")) {
        // Alarm: show description on label line, "ALARM" in red on status line
        String desc = alarmDescription(statusStr);
        spriteStatusBar.setTextColor(TFT_RED);
        spriteStatusBar.setTextSize(1);
        int16_t dw = spriteStatusBar.textWidth(desc.c_str());
        spriteStatusBar.setCursor(115 - dw / 2, 5);
        spriteStatusBar.print(desc);
        spriteStatusBar.setTextSize(3);
        int16_t sw = spriteStatusBar.textWidth("ALARM");
        spriteStatusBar.setCursor(115 - sw / 2, 22);
        spriteStatusBar.print("ALARM");
    } else {
        spriteStatusBar.setTextColor(COLOR_GRAY_TEXT);
        spriteStatusBar.setTextSize(1);
        int16_t lw = spriteStatusBar.textWidth("MACHINE STATUS");
        spriteStatusBar.setCursor(115 - lw / 2, 5);
        spriteStatusBar.print("MACHINE STATUS");
        spriteStatusBar.setTextColor(COLOR_CYAN);
        spriteStatusBar.setTextSize(3);
        int16_t sw = spriteStatusBar.textWidth(statusStr.c_str());
        spriteStatusBar.setCursor(115 - sw / 2, 22);
        spriteStatusBar.print(statusStr);
    }

    spriteStatusBar.pushSprite(5, 40);
}

void updateStatusCurrentFile() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_STATUS) return;
    if (!spriteFileDisplay.getBuffer()) return;

    spriteFileDisplay.fillRoundRect(0, 0, 230, 40, 5, COLOR_DARKER_BG);

    String fileStr;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        fileStr = pendantMachine.currentFile;
        xSemaphoreGive(stateMutex);
    } else {
        fileStr = pendantMachine.currentFile;
    }

    if (pendantSdCard.loadedFile.length() > 0 && fileStr.length() == 0) {
        // File queued on pendant, not yet sent to controller
        spriteFileDisplay.setTextColor(COLOR_GREEN);
        spriteFileDisplay.setTextSize(1);
        spriteFileDisplay.setCursor(5, 5);
        spriteFileDisplay.print("READY — press green to run");
        spriteFileDisplay.setTextColor(COLOR_GREEN);
        spriteFileDisplay.setCursor(5, 20);
        spriteFileDisplay.print(pendantSdCard.loadedFile);
    } else {
        spriteFileDisplay.setTextColor(COLOR_GRAY_TEXT);
        spriteFileDisplay.setTextSize(1);
        spriteFileDisplay.setCursor(5, 5);
        spriteFileDisplay.print("CURRENT FILE");
        spriteFileDisplay.setTextColor(COLOR_CYAN);
        spriteFileDisplay.setCursor(5, 20);
        spriteFileDisplay.print(fileStr);
    }

    spriteFileDisplay.pushSprite(5, 95);
}

void updateStatusAxisPositions() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_STATUS) return;
    if (!spriteAxisDisplay.getBuffer()) return;

    float px, py, pz, pa;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        px = pendantMachine.posX;
        py = pendantMachine.posY;
        pz = pendantMachine.posZ;
        pa = pendantMachine.posA;
        xSemaphoreGive(stateMutex);
    } else {
        px = pendantMachine.posX; py = pendantMachine.posY;
        pz = pendantMachine.posZ; pa = pendantMachine.posA;
    }

    spriteAxisDisplay.fillRoundRect(0, 0, 230, 65, 5, COLOR_DARKER_BG);

    spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteAxisDisplay.setTextSize(1);
    spriteAxisDisplay.setCursor(5, 5);
    spriteAxisDisplay.print("AXIS POSITIONS");

    const char* axisNames[] = { "X", "Y", "Z", "A" };
    float       positions[] = { px, py, pz, pa };
    spriteAxisDisplay.setTextColor(COLOR_ORANGE);
    spriteAxisDisplay.setTextSize(2);
    for (int i = 0; i < pendantMachine.numAxes; i++) {
        spriteAxisDisplay.setCursor((i % 2) ? 125 : 5, 20 + (i / 2) * 23);
        spriteAxisDisplay.print(axisNames[i]);
        spriteAxisDisplay.print(":");
        spriteAxisDisplay.print(positions[i], 1);
    }

    spriteAxisDisplay.pushSprite(5, 140);
}

void updateStatusFeedSpindle() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_STATUS) return;
    if (!spriteValueDisplay.getBuffer()) return;

    int feedRate, spindleRPM;
    String spindleDir;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        feedRate   = pendantMachine.feedRate;
        spindleRPM = pendantMachine.spindleRPM;
        spindleDir = pendantMachine.spindleDir;
        xSemaphoreGive(stateMutex);
    } else {
        feedRate   = pendantMachine.feedRate;
        spindleRPM = pendantMachine.spindleRPM;
        spindleDir = pendantMachine.spindleDir;
    }

    spriteValueDisplay.fillSprite(COLOR_BACKGROUND);

    // Feed Rate box
    spriteValueDisplay.fillRoundRect(0, 0, 112, 65, 5, COLOR_DARKER_BG);
    spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteValueDisplay.setTextSize(1);
    spriteValueDisplay.setCursor(5, 3);
    spriteValueDisplay.print("FEED");
    spriteValueDisplay.setTextColor(COLOR_ORANGE);
    spriteValueDisplay.setTextSize(2);
    spriteValueDisplay.setCursor(5, 25);
    spriteValueDisplay.print(feedRate);
    spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteValueDisplay.setTextSize(1);
    int16_t mmW = spriteValueDisplay.textWidth("mm/min");
    spriteValueDisplay.setCursor(112 - 5 - mmW, 50);
    spriteValueDisplay.print("mm/min");

    // Spindle box
    spriteValueDisplay.fillRoundRect(118, 0, 112, 65, 5, COLOR_DARKER_BG);
    spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteValueDisplay.setTextSize(1);
    spriteValueDisplay.setCursor(123, 3);
    spriteValueDisplay.print("SPINDLE");
    int16_t dirW = spriteValueDisplay.textWidth(spindleDir.c_str());
    spriteValueDisplay.setCursor(230 - 5 - dirW, 3);
    spriteValueDisplay.print(spindleDir);
    spriteValueDisplay.setTextColor(COLOR_GREEN);
    spriteValueDisplay.setTextSize(2);
    spriteValueDisplay.setCursor(123, 25);
    spriteValueDisplay.print(spindleRPM);
    spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
    spriteValueDisplay.setTextSize(1);
    int16_t rpmW = spriteValueDisplay.textWidth("RPM");
    spriteValueDisplay.setCursor(230 - 5 - rpmW, 50);
    spriteValueDisplay.print("RPM");

    spriteValueDisplay.pushSprite(5, 210);
}

void handleStatusTouch(int x, int y) {
    if (isTouchInBounds(x, y, 5, 280, 112, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    } else if (isTouchInBounds(x, y, 123, 280, 112, 40)) {
        currentPendantScreen = PSCREEN_FLUIDNC;
    }
}
