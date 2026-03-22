#include "pendant_shared.h"
#include "screen_macros.h"

void enterMacros() {
    // No sprites needed for macros screen
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;
}

void exitMacros() {
    // Nothing to free
}

void drawMacrosScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("MACROS");

    for (int i = 0; i < 10; i++) {
        int bx = 5 + (i % 2) * 118;
        int by = 40 + (i / 2) * 48;
        String label = "Macro " + String(i + 1);
        drawButton(bx, by, 112, 43, label, COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    }

    drawButton(5, 280, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void handleMacrosTouch(int x, int y) {
    for (int i = 0; i < 10; i++) {
        int bx = 5 + (i % 2) * 118;
        int by = 40 + (i / 2) * 48;
        if (isTouchInBounds(x, y, bx, by, 112, 43)) {
            String label = "Macro " + String(i + 1);
            drawButton(bx, by, 112, 43, label, COLOR_WHITE, COLOR_BUTTON_GRAY, 2);
            delay_ms(150);
            drawButton(bx, by, 112, 43, label, COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
            if (!pendantConnected) return;
            char cmd[16];
            snprintf(cmd, sizeof(cmd), "$macro/%d", i);
            send_line(cmd);
            return;
        }
    }
    if (isTouchInBounds(x, y, 5, 280, 230, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    }
}
