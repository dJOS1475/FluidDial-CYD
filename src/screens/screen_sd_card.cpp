#include "pendant_shared.h"
#include "screen_sd_card.h"
#include "../FileParser.h"

void enterSDCard() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

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

            uint16_t bg = (displayIndex == pendantSdCard.selectedFile)
                          ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON_GRAY;
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

    drawButton(5, 282, 230, 36, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void handleSDCardTouch(int x, int y) {
    // File row taps
    for (int i = 0; i < 5; i++) {
        if (isTouchInBounds(x, y, 5, 40 + i * 40, 230, 36)) {
            int displayIndex = i + pendantSdCard.scrollOffset;
            if (displayIndex < pendantSdCard.fileCount) {
                pendantSdCard.selectedFile = displayIndex;
                if (pendantConnected) {
                    String cmd = "/sd/" + pendantSdCard.files[displayIndex];
                    send_line(cmd.c_str());
                    navigateTo(PSCREEN_STATUS);
                } else {
                    drawSDCardScreen();
                }
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

    if (isTouchInBounds(x, y, 5, 282, 230, 36)) {
        navigateTo(PSCREEN_MAIN_MENU);
    }
}
