#include "pendant_shared.h"
#include "screen_status.h"

void enterStatus() {
    // The four status panels all render through the shared 16-bit scratch sprite
    // (begin/endPanelSprite) — one buffer reused for every panel, grown to the
    // largest and then held with no per-frame churn.  Holding four separate
    // persistent buffers (~50 KB) failed on the WiFi build; the shared scratch
    // keeps only ~one panel's RAM live.  Just clear any inherited buffer here.
    releasePanelSprites();
}

void exitStatus() {
    releasePanelSprites();
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
    if (currentPendantScreen != PSCREEN_STATUS) return;

    // Snapshot shared state under the lock FIRST.  Skip this frame if the lock
    // is briefly unavailable rather than reading Strings unlocked (Core 0 may be
    // mid-write, and a String realloc during the copy would corrupt the heap).
    String statusStr;
    String fileStr;
    int    pct = 0;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    statusStr = pendantMachine.status;
    fileStr   = pendantMachine.currentFile;
    pct       = pendantMachine.jobPercent;
    xSemaphoreGive(stateMutex);

    bool jobRunning = fileStr.length() > 0;

    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, 50, ox, oy, 5, 40);
    g->fillRect(ox, oy, 230, 50, COLOR_DARKER_BG);

    if (!pendantSynced || statusStr == "N/C" || statusStr.length() == 0) {
        // Two-phase power-up / reconnect indicator (matches the main menu):
        //   "Connecting" — link not yet established
        //   "Syncing"    — link up; fetching config + waiting for live state
        const char* phase = pendantConnected ? "Syncing" : "Connecting";
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        int16_t lw = g->textWidth("MACHINE STATUS");
        g->setCursor(ox + 115 - lw / 2, oy + 5);
        g->print("MACHINE STATUS");
        g->setTextColor(COLOR_ORANGE);
        g->setTextSize(3);
        int16_t cw = g->textWidth(phase);
        g->setCursor(ox + 115 - cw / 2, oy + 22);
        g->print(phase);

    } else if (statusStr.startsWith("Alarm")) {
        String desc = alarmDescription(statusStr);
        g->setTextColor(TFT_RED);
        g->setTextSize(1);
        int16_t dw = g->textWidth(desc.c_str());
        g->setCursor(ox + 115 - dw / 2, oy + 5);
        g->print(desc);
        g->setTextSize(3);
        int16_t sw = g->textWidth("ALARM");
        g->setCursor(ox + 115 - sw / 2, oy + 22);
        g->print("ALARM");

    } else if (jobRunning) {
        // Two-column layout
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        g->setCursor(ox + 5, oy + 5);
        g->print("MACHINE STATUS");
        g->setTextColor(COLOR_CYAN);
        g->setTextSize(3);
        int16_t sw = g->textWidth(statusStr.c_str());
        int16_t cx = 56 - sw / 2;
        if (cx < 0) cx = 0;
        g->setCursor(ox + cx, oy + 22);
        g->print(statusStr);

        g->drawLine(ox + 115, oy + 2, ox + 115, oy + 47, COLOR_BUTTON_GRAY);

        String pctStr = String(pct) + "%";
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        int16_t lw = g->textWidth("PROGRESS");
        g->setCursor(ox + 174 - lw / 2, oy + 5);
        g->print("PROGRESS");
        g->setTextColor(COLOR_GREEN);
        g->setTextSize(3);
        int16_t pw = g->textWidth(pctStr.c_str());
        g->setCursor(ox + 174 - pw / 2, oy + 22);
        g->print(pctStr);

    } else {
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        int16_t lw = g->textWidth("MACHINE STATUS");
        g->setCursor(ox + 115 - lw / 2, oy + 5);
        g->print("MACHINE STATUS");
        g->setTextColor(COLOR_CYAN);
        g->setTextSize(3);
        int16_t sw = g->textWidth(statusStr.c_str());
        g->setCursor(ox + 115 - sw / 2, oy + 22);
        g->print(statusStr);
    }

    endPanelSprite(230, 50, 5, 40);
}

void updateStatusCurrentFile() {
    if (currentPendantScreen != PSCREEN_STATUS) return;

    String fileStr;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    fileStr = pendantMachine.currentFile;
    xSemaphoreGive(stateMutex);

    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, 40, ox, oy, 5, 95);
    g->fillRoundRect(ox, oy, 230, 40, 5, COLOR_DARKER_BG);

    if (pendantSdCard.loadedFile.length() > 0 && fileStr.length() == 0) {
        g->setTextColor(COLOR_GREEN);
        g->setTextSize(1);
        g->setCursor(ox + 5, oy + 5);
        g->print("READY — press green to run");
        g->setCursor(ox + 5, oy + 20);
        g->print(pendantSdCard.loadedFile);
    } else {
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        g->setCursor(ox + 5, oy + 5);
        g->print("CURRENT FILE");
        g->setTextColor(COLOR_CYAN);
        g->setCursor(ox + 5, oy + 20);
        g->print(fileStr);
    }

    endPanelSprite(230, 40, 5, 95);
}

void updateStatusAxisPositions() {
    if (currentPendantScreen != PSCREEN_STATUS) return;

    float px, py, pz, pa;
    int   numAxes;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    px = pendantMachine.posX;  py = pendantMachine.posY;
    pz = pendantMachine.posZ;  pa = pendantMachine.posA;
    numAxes = pendantMachine.numAxes;
    xSemaphoreGive(stateMutex);

    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, 65, ox, oy, 5, 140);

    g->fillRoundRect(ox, oy, 230, 65, 5, COLOR_DARKER_BG);

    g->setTextColor(COLOR_GRAY_TEXT);
    g->setTextSize(1);
    g->setCursor(ox + 5, oy + 5);
    g->print("AXIS POSITIONS");

    const char* axisNames[] = { "X", "Y", "Z", "A" };
    float       positions[] = { px, py, pz, pa };
    g->setTextColor(COLOR_ORANGE);
    g->setTextSize(2);
    for (int i = 0; i < numAxes; i++) {
        g->setCursor(ox + ((i % 2) ? 125 : 5), oy + 20 + (i / 2) * 23);
        g->print(axisNames[i]);
        g->print(":");
        g->print(positions[i], 1);
    }

    endPanelSprite(230, 65, 5, 140);
}

void updateStatusFeedSpindle() {
    if (currentPendantScreen != PSCREEN_STATUS) return;

    int feedRate, spindleRPM;
    String spindleDir;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    feedRate   = pendantMachine.feedRate;
    spindleRPM = pendantMachine.spindleRPM;
    spindleDir = pendantMachine.spindleDir;
    xSemaphoreGive(stateMutex);

    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, 65, ox, oy, 5, 210);

    g->fillRect(ox, oy, 230, 65, COLOR_BACKGROUND);

    // Feed Rate box
    g->fillRoundRect(ox + 0, oy, 112, 65, 5, COLOR_DARKER_BG);
    g->setTextColor(COLOR_GRAY_TEXT);
    g->setTextSize(1);
    g->setCursor(ox + 5, oy + 3);
    g->print("FEED");
    g->setTextColor(COLOR_ORANGE);
    g->setTextSize(2);
    g->setCursor(ox + 5, oy + 25);
    g->print(feedRate);
    g->setTextColor(COLOR_GRAY_TEXT);
    g->setTextSize(1);
    int16_t mmW = g->textWidth("mm/min");
    g->setCursor(ox + 112 - 5 - mmW, oy + 50);
    g->print("mm/min");

    // Spindle box
    g->fillRoundRect(ox + 118, oy, 112, 65, 5, COLOR_DARKER_BG);
    g->setTextColor(COLOR_GRAY_TEXT);
    g->setTextSize(1);
    g->setCursor(ox + 123, oy + 3);
    g->print("SPINDLE");
    int16_t dirW = g->textWidth(spindleDir.c_str());
    g->setCursor(ox + 230 - 5 - dirW, oy + 3);
    g->print(spindleDir);
    g->setTextColor(COLOR_GREEN);
    g->setTextSize(2);
    g->setCursor(ox + 123, oy + 25);
    g->print(spindleRPM);
    g->setTextColor(COLOR_GRAY_TEXT);
    g->setTextSize(1);
    int16_t rpmW = g->textWidth("RPM");
    g->setCursor(ox + 230 - 5 - rpmW, oy + 50);
    g->print("RPM");

    endPanelSprite(230, 65, 5, 210);
}

void handleStatusTouch(int x, int y) {
    if (isTouchInBounds(x, y, 5, 280, 112, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    } else if (isTouchInBounds(x, y, 123, 280, 112, 40)) {
        currentPendantScreen = PSCREEN_FLUIDNC;
    }
}
