#include "pendant_shared.h"
#include "screen_main_menu.h"

void enterMainMenu() {
    // Allocate sprite for status display
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

    if (ESP.getFreeHeap() >= 50000) {
        spriteStatusBar.createSprite(230, 65);
        spriteStatusBar.setColorDepth(16);
        spritesInitialized = true;
    }
}

void exitMainMenu() {
    spriteStatusBar.deleteSprite();
    spritesInitialized = false;
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
    if (!spritesInitialized || currentPendantScreen != PSCREEN_MAIN_MENU) return;
    if (!spriteStatusBar.getBuffer()) return;

    spriteStatusBar.fillSprite(COLOR_DARKER_BG);

    String statusStr;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        statusStr = pendantMachine.status;
        xSemaphoreGive(stateMutex);
    } else {
        statusStr = pendantMachine.status;  // best-effort read
    }

    if (statusStr.startsWith("Alarm")) {
        // Alarm: show description on label line, "ALARM" in red on status line
        String desc = alarmDescription(statusStr);
        spriteStatusBar.setTextColor(TFT_RED);
        spriteStatusBar.setTextSize(1);
        int16_t dw = spriteStatusBar.textWidth(desc.c_str());
        spriteStatusBar.setCursor(115 - dw / 2, 8);
        spriteStatusBar.print(desc);
        spriteStatusBar.setTextSize(4);
        int16_t sw = spriteStatusBar.textWidth("ALARM");
        spriteStatusBar.setCursor(115 - sw / 2, 26);
        spriteStatusBar.print("ALARM");
    } else {
        spriteStatusBar.setTextColor(COLOR_GRAY_TEXT);
        spriteStatusBar.setTextSize(1);
        int16_t labelWidth = spriteStatusBar.textWidth("STATUS");
        spriteStatusBar.setCursor(115 - labelWidth / 2, 8);
        spriteStatusBar.print("STATUS");
        spriteStatusBar.setTextColor(COLOR_CYAN);
        spriteStatusBar.setTextSize(4);
        int16_t statusWidth = spriteStatusBar.textWidth(statusStr.c_str());
        spriteStatusBar.setCursor(115 - statusWidth / 2, 26);
        spriteStatusBar.print(statusStr);
    }

    spriteStatusBar.pushSprite(5, 40);
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
    else if (isTouchInBounds(x, y, 5,   btnY + btnGap * 3, 112, btnH)) currentPendantScreen = PSCREEN_PROBING;
    else if (isTouchInBounds(x, y, 123, btnY + btnGap * 3, 112, btnH)) currentPendantScreen = PSCREEN_STATUS;
}
