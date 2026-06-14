#include "pendant_shared.h"
#include "screen_main_menu.h"

void enterMainMenu() {
    // Status bar uses a TRANSIENT 16-bit panel (see updateMainMenuDisplay) so it
    // renders true colour and only one buffer is live at a time.
    releasePanelSprites();
}

void exitMainMenu() {
    releasePanelSprites();
}

void drawMainMenu() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("MAIN MENU");

    // Draw static background for status display area
    display.fillRoundRect(5, 40, 230, 65, 5, COLOR_DARKER_BG);
    updateMainMenuDisplay();

    int btnY   = 115;
    int btnH   = 47;
    int btnGap = 52;

    drawButton(5, btnY, 112, btnH, "Jog", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(123, btnY, 112, btnH, "Work Area", COLOR_BLUE, COLOR_WHITE, 2);
    drawMultiLineButton(5, btnY + btnGap, 112, btnH, "Feeds &", "Speeds", COLOR_BLUE, COLOR_WHITE, 2);
    drawMultiLineButton(123, btnY + btnGap, 112, btnH, "Spindle", "Control", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(5, btnY + btnGap * 2, 112, btnH, "Macros", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(123, btnY + btnGap * 2, 112, btnH, "SD Card", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(5, btnY + btnGap * 3, 112, btnH, "Probe", COLOR_BLUE, COLOR_WHITE, 2);
    drawButton(123, btnY + btnGap * 3, 112, btnH, "Status", COLOR_BLUE, COLOR_WHITE, 2);
}

void updateMainMenuDisplay() {
    if (currentPendantScreen != PSCREEN_MAIN_MENU) return;

    // Snapshot under the lock first; skip the frame if it's briefly held rather
    // than read the status String unlocked (a concurrent realloc on Core 0 would
    // corrupt the heap).
    String statusStr;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    statusStr = pendantMachine.status;
    xSemaphoreGive(stateMutex);

    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, 65, ox, oy, 5, 40);
    g->fillRect(ox, oy, 230, 65, COLOR_DARKER_BG);

    if (!pendantSynced || statusStr == "N/C" || statusStr.length() == 0) {
        // Power-up / reconnect, two phases so the user knows what's happening:
        //   "Connecting" — link not yet established (WiFi assoc / WS handshake
        //                  or UART not yet responding)
        //   "Syncing"    — link is up; fetching config + waiting for live state
        // Size 3 so the 10-char "Connecting" fits the 230 px bar.  Same for
        // WiFi and wired.
        const char* phase = pendantConnected ? "Syncing" : "Connecting";
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        int16_t lw = g->textWidth("STATUS");
        g->setCursor(ox + 115 - lw / 2, oy + 8);
        g->print("STATUS");
        g->setTextColor(COLOR_ORANGE);
        g->setTextSize(3);
        int16_t cw = g->textWidth(phase);
        g->setCursor(ox + 115 - cw / 2, oy + 30);
        g->print(phase);
    } else if (statusStr.startsWith("Alarm")) {
        // Alarm: description on label line, "ALARM" in red on status line
        String desc = alarmDescription(statusStr);
        g->setTextColor(TFT_RED);
        g->setTextSize(1);
        int16_t dw = g->textWidth(desc.c_str());
        g->setCursor(ox + 115 - dw / 2, oy + 8);
        g->print(desc);
        g->setTextSize(4);
        int16_t sw = g->textWidth("ALARM");
        g->setCursor(ox + 115 - sw / 2, oy + 26);
        g->print("ALARM");
    } else {
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        int16_t labelWidth = g->textWidth("STATUS");
        g->setCursor(ox + 115 - labelWidth / 2, oy + 8);
        g->print("STATUS");
        g->setTextColor(COLOR_CYAN);
        g->setTextSize(4);
        int16_t statusWidth = g->textWidth(statusStr.c_str());
        g->setCursor(ox + 115 - statusWidth / 2, oy + 26);
        g->print(statusStr);
    }

    endPanelSprite(230, 65, 5, 40);
}

void handleMainMenuTouch(int x, int y) {
    int btnY   = 115;
    int btnH   = 47;
    int btnGap = 52;

    if      (isTouchInBounds(x, y, 5,   btnY,              112, btnH)) currentPendantScreen = PSCREEN_JOG_HOMING;
    else if (isTouchInBounds(x, y, 123, btnY,              112, btnH)) currentPendantScreen = PSCREEN_PROBING_WORK;
    else if (isTouchInBounds(x, y, 5,   btnY + btnGap,     112, btnH)) currentPendantScreen = PSCREEN_FEEDS_SPEEDS;
    else if (isTouchInBounds(x, y, 123, btnY + btnGap,     112, btnH)) currentPendantScreen = PSCREEN_SPINDLE_CONTROL;
    else if (isTouchInBounds(x, y, 5,   btnY + btnGap * 2, 112, btnH)) currentPendantScreen = PSCREEN_MACROS;
    else if (isTouchInBounds(x, y, 123, btnY + btnGap * 2, 112, btnH)) currentPendantScreen = PSCREEN_SD_CARD;
    else if (isTouchInBounds(x, y, 5,   btnY + btnGap * 3, 112, btnH)) currentPendantScreen = PSCREEN_PROBE;
    else if (isTouchInBounds(x, y, 123, btnY + btnGap * 3, 112, btnH)) currentPendantScreen = PSCREEN_STATUS;
}
