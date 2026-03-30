#include "pendant_shared.h"
#include "screen_macros.h"
#include "../FileParser.h"

void enterMacros() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

    // Request a fresh file list from the controller's local filesystem
    if (pendantConnected) {
        pendantMacros.loading      = true;
        pendantMacros.fileCount    = 0;
        pendantMacros.scrollOffset = 0;
        pendantMacros.selectedFile = 0;
        request_file_list("/localfs");
    }
}

void exitMacros() {}

void drawMacrosScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("MACROS");

    if (pendantMacros.loading) {
        display.setTextColor(COLOR_GRAY_TEXT);
        display.setTextSize(2);
        display.setCursor(50, 140);
        display.print("Loading...");
    } else if (pendantMacros.fileCount == 0) {
        display.setTextColor(COLOR_GRAY_TEXT);
        display.setTextSize(1);
        display.setCursor(20, 130);
        display.print(pendantConnected ? "No macro files found." : "Not connected.");
        display.setCursor(20, 148);
        display.print("Press Refresh to retry.");
    } else {
        for (int i = 0; i < 5 && i < pendantMacros.fileCount; i++) {
            int displayIndex = i + pendantMacros.scrollOffset;
            if (displayIndex >= pendantMacros.fileCount) break;

            uint16_t bg = (displayIndex == pendantMacros.selectedFile)
                          ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON_GRAY;
            display.fillRoundRect(5, 40 + i * 40, 230, 36, 8, bg);
            display.setTextColor(COLOR_WHITE);
            display.setTextSize(1);
            display.setCursor(10, 52 + i * 40);
            display.print(pendantMacros.files[displayIndex]);
        }
    }

    // Scroll + Refresh row
    drawButton(5,   242, 72, 36, "<<",      COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(83,  242, 72, 36, "Refresh", COLOR_DARK_GREEN,  COLOR_WHITE, 1);
    drawButton(161, 242, 72, 36, ">>",      COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

    drawButton(5, 282, 230, 36, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void handleMacrosTouch(int x, int y) {
    // File row taps
    for (int i = 0; i < 5; i++) {
        if (isTouchInBounds(x, y, 5, 40 + i * 40, 230, 36)) {
            int displayIndex = i + pendantMacros.scrollOffset;
            if (displayIndex < pendantMacros.fileCount) {
                pendantMacros.selectedFile = displayIndex;
                if (pendantConnected) {
                    String cmd = "$Localfs/Run=" + pendantMacros.files[displayIndex];
                    send_line(cmd.c_str());
                    navigateTo(PSCREEN_STATUS);
                } else {
                    drawMacrosScreen();
                }
            }
            return;
        }
    }

    // << scroll back
    if (isTouchInBounds(x, y, 5, 242, 72, 36)) {
        if (pendantMacros.scrollOffset > 0) {
            pendantMacros.scrollOffset--;
            drawMacrosScreen();
        }
        return;
    }

    // Refresh
    if (isTouchInBounds(x, y, 83, 242, 72, 36)) {
        if (pendantConnected) {
            pendantMacros.loading      = true;
            pendantMacros.fileCount    = 0;
            pendantMacros.scrollOffset = 0;
            pendantMacros.selectedFile = 0;
            drawMacrosScreen();
            request_file_list("/localfs");
        }
        return;
    }

    // >> scroll forward
    if (isTouchInBounds(x, y, 161, 242, 72, 36)) {
        if (pendantMacros.scrollOffset + 5 < pendantMacros.fileCount) {
            pendantMacros.scrollOffset++;
            drawMacrosScreen();
        }
        return;
    }

    if (isTouchInBounds(x, y, 5, 282, 230, 36)) {
        navigateTo(PSCREEN_MAIN_MENU);
    }
}
