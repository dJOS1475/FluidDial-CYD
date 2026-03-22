#include "pendant_shared.h"
#include "screen_sd_card.h"

void enterSDCard() {
    // No sprites for SD card screen
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;
}

void exitSDCard() {
    // Nothing to free
}

void drawSDCardScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("SD CARD");

    for (int i = 0; i < 5 && i < pendantSdCard.fileCount; i++) {
        int displayIndex = i + pendantSdCard.scrollOffset;
        if (displayIndex >= pendantSdCard.fileCount) break;

        uint16_t bg = (displayIndex == pendantSdCard.selectedFile) ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON_GRAY;
        display.fillRoundRect(5, 40 + i * 44, 230, 40, 8, bg);
        display.setTextColor(COLOR_WHITE);
        display.setTextSize(1);
        display.setCursor(10, 52 + i * 44);
        display.print(pendantSdCard.files[displayIndex]);
    }

    drawButton(5,   240, 112, 38, "Back",      COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(123, 240, 112, 38, "Next",      COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(5,   282, 230, 38, "Main Menu", COLOR_BLUE,        COLOR_WHITE, 2);
}

void handleSDCardTouch(int x, int y) {
    for (int i = 0; i < 5; i++) {
        if (isTouchInBounds(x, y, 5, 40 + i * 44, 230, 40)) {
            int displayIndex = i + pendantSdCard.scrollOffset;
            if (displayIndex < pendantSdCard.fileCount) {
                pendantSdCard.selectedFile      = displayIndex;
                pendantMachine.currentFile      = pendantSdCard.files[displayIndex];
                // Send SD run command
                if (pendantConnected) {
                    String cmd = "/SD/" + pendantMachine.currentFile;
                    send_line(cmd.c_str());
                    currentPendantScreen = PSCREEN_STATUS;
                }
            }
            return;
        }
    }

    if (isTouchInBounds(x, y, 5, 240, 112, 38)) {
        if (pendantSdCard.scrollOffset > 0) {
            drawButton(5, 240, 112, 38, "Back", COLOR_WHITE, COLOR_BUTTON_GRAY, 2);
            delay_ms(150);
            pendantSdCard.scrollOffset--;
            drawSDCardScreen();
        }
    } else if (isTouchInBounds(x, y, 123, 240, 112, 38)) {
        if (pendantSdCard.scrollOffset + 5 < pendantSdCard.fileCount) {
            drawButton(123, 240, 112, 38, "Next", COLOR_WHITE, COLOR_BUTTON_GRAY, 2);
            delay_ms(150);
            pendantSdCard.scrollOffset++;
            drawSDCardScreen();
        }
    }

    if (isTouchInBounds(x, y, 5, 282, 230, 38)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    }
}
