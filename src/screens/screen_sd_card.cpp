#include "pendant_shared.h"
#include "screen_sd_card.h"
#include "../FileParser.h"

void enterSDCard() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

    pendantSdCard.pendingRun = false;

    // Request a fresh file list from the controller
    if (pendantConnected) {
        pendantSdCard.loading      = true;
        pendantSdCard.fileCount    = 0;
        pendantSdCard.scrollOffset = 0;
        pendantSdCard.selectedFile = 0;
        request_file_list("/sd");
    }
}

void exitSDCard() {}

void drawSDCardScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("SD CARD");

    if (pendantSdCard.loading) {
        display.setTextColor(COLOR_GRAY_TEXT);
        display.setTextSize(2);
        display.setCursor(50, 140);
        display.print("Loading...");
    } else if (pendantSdCard.fileCount == 0) {
        display.setTextColor(COLOR_GRAY_TEXT);
        display.setTextSize(1);
        display.setCursor(20, 130);
        display.print(pendantConnected ? "No GCode files found." : "Not connected.");
        display.setCursor(20, 148);
        display.print("Press Refresh to retry.");
    } else {
        for (int i = 0; i < 5 && i < pendantSdCard.fileCount; i++) {
            int displayIndex = i + pendantSdCard.scrollOffset;
            if (displayIndex >= pendantSdCard.fileCount) break;

            uint16_t bg;
            if (displayIndex == pendantSdCard.selectedFile && pendantSdCard.pendingRun) {
                bg = COLOR_DARK_GREEN;  // pending run = green highlight
            } else if (displayIndex == pendantSdCard.selectedFile) {
                bg = COLOR_BUTTON_ACTIVE;
            } else {
                bg = COLOR_BUTTON_GRAY;
            }
            display.fillRoundRect(5, 40 + i * 40, 230, 36, 8, bg);
            display.setTextColor(COLOR_WHITE);
            display.setTextSize(1);
            display.setCursor(10, 52 + i * 40);
            display.print(pendantSdCard.files[displayIndex]);
        }
    }

    // Scroll + Refresh row
    drawButton(5,   242, 72, 36, "<<",      COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(83,  242, 72, 36, "Refresh", COLOR_DARK_GREEN,  COLOR_WHITE, 1);
    drawButton(161, 242, 72, 36, ">>",      COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

    if (pendantSdCard.pendingRun) {
        // Confirmation row: LOAD + RUN
        drawButton(5,   282, 110, 36, "Load", COLOR_BLUE,       COLOR_WHITE, 2);
        drawButton(121, 282, 114, 36, "Run",  COLOR_DARK_GREEN, COLOR_WHITE, 2);
    } else {
        drawButton(5, 282, 230, 36, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
    }
}

void handleSDCardTouch(int x, int y) {
    // File row taps — first tap selects & arms confirmation; second tap on same file does nothing extra
    for (int i = 0; i < 5; i++) {
        if (isTouchInBounds(x, y, 5, 40 + i * 40, 230, 36)) {
            int displayIndex = i + pendantSdCard.scrollOffset;
            if (displayIndex < pendantSdCard.fileCount) {
                pendantSdCard.selectedFile = displayIndex;
                pendantSdCard.pendingRun   = true;
                drawSDCardScreen();
            }
            return;
        }
    }

    // << scroll back
    if (isTouchInBounds(x, y, 5, 242, 72, 36)) {
        if (pendantSdCard.scrollOffset > 0) {
            pendantSdCard.scrollOffset--;
            drawSDCardScreen();
        }
        return;
    }

    // Refresh
    if (isTouchInBounds(x, y, 83, 242, 72, 36)) {
        if (pendantConnected) {
            pendantSdCard.loading      = true;
            pendantSdCard.fileCount    = 0;
            pendantSdCard.scrollOffset = 0;
            pendantSdCard.selectedFile = 0;
            pendantSdCard.pendingRun   = false;
            drawSDCardScreen();
            request_file_list("/sd");
        }
        return;
    }

    // >> scroll forward
    if (isTouchInBounds(x, y, 161, 242, 72, 36)) {
        if (pendantSdCard.scrollOffset + 5 < pendantSdCard.fileCount) {
            pendantSdCard.scrollOffset++;
            drawSDCardScreen();
        }
        return;
    }

    // Bottom row — depends on pendingRun state
    if (pendantSdCard.pendingRun) {
        // LOAD — store filename, navigate to Status; green button will send run command
        if (isTouchInBounds(x, y, 5, 282, 110, 36)) {
            pendantSdCard.loadedFile = pendantSdCard.files[pendantSdCard.selectedFile];
            pendantSdCard.pendingRun = false;
            navigateTo(PSCREEN_STATUS);
            return;
        }
        // RUN — send command immediately
        if (isTouchInBounds(x, y, 121, 282, 114, 36)) {
            if (pendantConnected) {
                String cmd = "$SD/Run=" + pendantSdCard.files[pendantSdCard.selectedFile];
                send_line(cmd.c_str());
                pendantSdCard.loadedFile = "";
                pendantSdCard.pendingRun = false;
                navigateTo(PSCREEN_STATUS);
            }
            return;
        }
    } else {
        if (isTouchInBounds(x, y, 5, 282, 230, 36)) {
            navigateTo(PSCREEN_MAIN_MENU);
        }
    }
}
