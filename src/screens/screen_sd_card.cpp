#include "pendant_shared.h"
#include "screen_sd_card.h"
#include "../FileParser.h"

// Sprite covers the file-list area: x=5..234, y=40..239 (230 x 200 px).
// Same pattern as screen_macros — prevents fillScreen flicker on STATE_UPDATE.

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

    // Allocate sprite for the file list area
    if (ESP.getFreeHeap() >= 50000) {
        spriteFileDisplay.createSprite(230, 200);
        if (spriteFileDisplay.getBuffer()) {
            spriteFileDisplay.setColorDepth(16);
            spritesInitialized = true;
        } else {
            spriteFileDisplay.deleteSprite();
        }
    }
}

void exitSDCard() {
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;
}

// Renders the dynamic file-list area into spriteFileDisplay and pushes it.
void updateSDCardFileList() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_SD_CARD) return;
    if (!spriteFileDisplay.getBuffer()) return;

    spriteFileDisplay.fillSprite(COLOR_BACKGROUND);

    if (pendantSdCard.loading) {
        spriteFileDisplay.setTextColor(COLOR_GRAY_TEXT);
        spriteFileDisplay.setTextSize(2);
        spriteFileDisplay.setCursor(50, 100);   // abs y=140 → rel y=100
        spriteFileDisplay.print("Loading...");
    } else if (pendantSdCard.fileCount == 0) {
        spriteFileDisplay.setTextColor(COLOR_GRAY_TEXT);
        spriteFileDisplay.setTextSize(1);
        spriteFileDisplay.setCursor(15, 90);    // abs y=130 → rel y=90
        spriteFileDisplay.print(pendantConnected ? "No GCode files found." : "Not connected.");
        spriteFileDisplay.setCursor(15, 108);   // abs y=148 → rel y=108
        spriteFileDisplay.print("Press Refresh to retry.");
    } else {
        for (int i = 0; i < 5 && i < pendantSdCard.fileCount; i++) {
            int displayIndex = i + pendantSdCard.scrollOffset;
            if (displayIndex >= pendantSdCard.fileCount) break;

            uint16_t bg;
            if (displayIndex == pendantSdCard.selectedFile && pendantSdCard.pendingRun) {
                bg = COLOR_DARK_GREEN;
            } else if (displayIndex == pendantSdCard.selectedFile) {
                bg = COLOR_BUTTON_ACTIVE;
            } else {
                bg = COLOR_BUTTON_GRAY;
            }
            spriteFileDisplay.fillRoundRect(0, i * 40, 230, 36, 8, bg);
            spriteFileDisplay.setTextColor(COLOR_WHITE);
            spriteFileDisplay.setTextSize(1);
            spriteFileDisplay.setCursor(5, 12 + i * 40);
            spriteFileDisplay.print(pendantSdCard.files[displayIndex]);
        }
    }

    spriteFileDisplay.pushSprite(5, 40);
}

void drawSDCardScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("SD CARD");

    // File list area — sprite render (no flicker on repeated STATE_UPDATE calls)
    updateSDCardFileList();

    // Static bottom rows
    drawButton(5,   242, 72, 36, "<<",      COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(83,  242, 72, 36, "Refresh", COLOR_DARK_GREEN,  COLOR_WHITE, 1);
    drawButton(161, 242, 72, 36, ">>",      COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

    if (pendantSdCard.pendingRun) {
        drawButton(5,   282, 110, 36, "Load", COLOR_BLUE,       COLOR_WHITE, 2);
        drawButton(121, 282, 114, 36, "Run",  COLOR_DARK_GREEN, COLOR_WHITE, 2);
    } else {
        drawButton(5, 282, 230, 36, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
    }
}

void handleSDCardTouch(int x, int y) {
    // File row taps — first tap selects & arms confirmation
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
            updateSDCardFileList();
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
            updateSDCardFileList();
            request_file_list("/sd");
        }
        return;
    }

    // >> scroll forward
    if (isTouchInBounds(x, y, 161, 242, 72, 36)) {
        if (pendantSdCard.scrollOffset + 5 < pendantSdCard.fileCount) {
            pendantSdCard.scrollOffset++;
            updateSDCardFileList();
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
