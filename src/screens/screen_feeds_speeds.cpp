#include "pendant_shared.h"
#include "screen_feeds_speeds.h"

void enterFeedsSpeeds() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

    if (ESP.getFreeHeap() < 50000) return;

    spriteStatusBar.createSprite(230, 35);
    spriteStatusBar.setColorDepth(16);
    spriteAxisDisplay.createSprite(72, 37);
    spriteAxisDisplay.setColorDepth(16);
    spriteValueDisplay.createSprite(72, 37);
    spriteValueDisplay.setColorDepth(16);
    spritesInitialized = true;
}

void exitFeedsSpeeds() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spritesInitialized = false;
}

void drawFeedsSpeedsScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("FEEDS & SPEEDS");

    updateFeedsSpeedsTopDisplay();

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 83);
    display.print("FEED OVERRIDE");

    String pcts[] = { "50%", "75%", "100%", "125%", "150%" };
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 78, 95, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
    }
    uint16_t bg3 = (3 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5, 137, 72, 37, pcts[3], bg3, COLOR_WHITE, 2);
    display.fillRoundRect(83, 137, 72, 37, 5, COLOR_DARKER_BG);
    updateFeedOverrideDisplay();
    uint16_t bg4 = (4 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(161, 137, 72, 37, pcts[4], bg4, COLOR_WHITE, 2);

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 182);
    display.print("SPINDLE OVERRIDE");

    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 78, 194, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
    }
    uint16_t bg3s = (3 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5, 236, 72, 37, pcts[3], bg3s, COLOR_WHITE, 2);
    display.fillRoundRect(83, 236, 72, 37, 5, COLOR_DARKER_BG);
    updateSpindleOverrideDisplay();
    uint16_t bg4s = (4 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(161, 236, 72, 37, pcts[4], bg4s, COLOR_WHITE, 2);

    drawButton(5, 280, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void updateFeedsSpeedsTopDisplay() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;
    if (!spriteStatusBar.getBuffer()) return;

    int feedRate, spindleRPM;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        feedRate   = pendantMachine.feedRate;
        spindleRPM = pendantMachine.spindleRPM;
        xSemaphoreGive(stateMutex);
    } else {
        feedRate   = pendantMachine.feedRate;
        spindleRPM = pendantMachine.spindleRPM;
    }

    spriteStatusBar.fillSprite(COLOR_BACKGROUND);

    // Feed box
    spriteStatusBar.fillRoundRect(0, 0, 112, 35, 5, COLOR_DARKER_BG);
    spriteStatusBar.setTextColor(COLOR_GRAY_TEXT);
    spriteStatusBar.setTextSize(1);
    spriteStatusBar.setCursor(5, 3);
    spriteStatusBar.print("FEED");
    spriteStatusBar.setTextColor(COLOR_ORANGE);
    spriteStatusBar.setTextSize(2);
    spriteStatusBar.setCursor(5, 13);
    spriteStatusBar.print(feedRate);

    // Spindle box
    spriteStatusBar.fillRoundRect(118, 0, 112, 35, 5, COLOR_DARKER_BG);
    spriteStatusBar.setTextColor(COLOR_GRAY_TEXT);
    spriteStatusBar.setTextSize(1);
    spriteStatusBar.setCursor(123, 3);
    spriteStatusBar.print("SPINDLE");
    spriteStatusBar.setTextColor(COLOR_GREEN);
    spriteStatusBar.setTextSize(2);
    spriteStatusBar.setCursor(123, 13);
    spriteStatusBar.print(spindleRPM);

    spriteStatusBar.pushSprite(5, 40);
}

void updateFeedOverrideDisplay() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;
    if (!spriteAxisDisplay.getBuffer()) return;

    int fro;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        fro = pendantMachine.feedOverride;
        xSemaphoreGive(stateMutex);
    } else {
        fro = pendantMachine.feedOverride;
    }

    spriteAxisDisplay.fillSprite(COLOR_DARKER_BG);
    spriteAxisDisplay.setTextColor(COLOR_ORANGE);
    spriteAxisDisplay.setTextSize(2);
    String txt = String(fro) + "%";
    int16_t tw = spriteAxisDisplay.textWidth(txt.c_str());
    spriteAxisDisplay.setCursor(36 - tw / 2, 11);
    spriteAxisDisplay.print(txt);
    spriteAxisDisplay.pushSprite(83, 137);
}

void updateSpindleOverrideDisplay() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;
    if (!spriteValueDisplay.getBuffer()) return;

    int sro;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        sro = pendantMachine.spindleOverride;
        xSemaphoreGive(stateMutex);
    } else {
        sro = pendantMachine.spindleOverride;
    }

    spriteValueDisplay.fillSprite(COLOR_DARKER_BG);
    spriteValueDisplay.setTextColor(COLOR_GREEN);
    spriteValueDisplay.setTextSize(2);
    String txt = String(sro) + "%";
    int16_t tw = spriteValueDisplay.textWidth(txt.c_str());
    spriteValueDisplay.setCursor(36 - tw / 2, 11);
    spriteValueDisplay.print(txt);
    spriteValueDisplay.pushSprite(83, 236);
}

void redrawFeedOverrideButtons() {
    if (currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;
    String pcts[] = { "50%", "75%", "100%", "125%", "150%" };
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 78, 95, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
    }
    uint16_t bg3 = (3 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5, 137, 72, 37, pcts[3], bg3, COLOR_WHITE, 2);
    uint16_t bg4 = (4 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(161, 137, 72, 37, pcts[4], bg4, COLOR_WHITE, 2);
    updateFeedOverrideDisplay();
}

void redrawSpindleOverrideButtons() {
    if (currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;
    String pcts[] = { "50%", "75%", "100%", "125%", "150%" };
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 78, 194, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
    }
    uint16_t bg3s = (3 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5, 236, 72, 37, pcts[3], bg3s, COLOR_WHITE, 2);
    uint16_t bg4s = (4 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(161, 236, 72, 37, pcts[4], bg4s, COLOR_WHITE, 2);
    updateSpindleOverrideDisplay();
}

void handleFeedsSpeedsTouch(int x, int y) {
    int pcts[] = { 50, 75, 100, 125, 150 };

    // Feed override - row 1
    for (int i = 0; i < 3; i++) {
        if (isTouchInBounds(x, y, 5 + i * 78, 95, 72, 37)) {
            pendantFeeds.selectedFeedOverride = i;
            pendantMachine.feedOverride = pcts[i];
            redrawFeedOverrideButtons();
            return;
        }
    }
    if (isTouchInBounds(x, y, 5, 137, 72, 37)) {
        pendantFeeds.selectedFeedOverride = 3;
        pendantMachine.feedOverride = 125;
        redrawFeedOverrideButtons();
        return;
    }
    if (isTouchInBounds(x, y, 161, 137, 72, 37)) {
        pendantFeeds.selectedFeedOverride = 4;
        pendantMachine.feedOverride = 150;
        redrawFeedOverrideButtons();
        return;
    }

    // Spindle override - row 1
    for (int i = 0; i < 3; i++) {
        if (isTouchInBounds(x, y, 5 + i * 78, 194, 72, 37)) {
            pendantFeeds.selectedSpindleOverride = i;
            pendantMachine.spindleOverride = pcts[i];
            redrawSpindleOverrideButtons();
            return;
        }
    }
    if (isTouchInBounds(x, y, 5, 236, 72, 37)) {
        pendantFeeds.selectedSpindleOverride = 3;
        pendantMachine.spindleOverride = 125;
        redrawSpindleOverrideButtons();
        return;
    }
    if (isTouchInBounds(x, y, 161, 236, 72, 37)) {
        pendantFeeds.selectedSpindleOverride = 4;
        pendantMachine.spindleOverride = 150;
        redrawSpindleOverrideButtons();
        return;
    }

    if (isTouchInBounds(x, y, 5, 280, 230, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    }
}
