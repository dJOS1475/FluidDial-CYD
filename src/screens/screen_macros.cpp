#include "pendant_shared.h"
#include "screen_macros.h"

void enterMacros() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

    pendantMacros.loading    = true;
    pendantMacros.count      = 0;
    pendantMacros.scrollOffset = 0;
    pendantMacros.selected   = -1;
    pendantMacros.pendingRun = false;

    if (pendantConnected) requestMacros();
}

void exitMacros() {}

// Truncate a macro string to fit in one button-width line at textSize 1 (~36 chars)
static String macroLabel(int displayIndex) {
    int macroNum = pendantMacros.indices[displayIndex];
    String label = "[" + String(macroNum) + "] " + pendantMacros.content[displayIndex];
    if (label.length() > 36) label = label.substring(0, 33) + "...";
    return label;
}

void drawMacrosScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("MACROS");

    if (pendantMacros.loading) {
        display.setTextColor(COLOR_GRAY_TEXT);
        display.setTextSize(2);
        display.setCursor(50, 140);
        display.print(pendantConnected ? "Loading..." : "Not connected.");
    } else if (pendantMacros.count == 0) {
        display.setTextColor(COLOR_GRAY_TEXT);
        display.setTextSize(1);
        display.setCursor(20, 130);
        display.print("No macros defined in config.");
        display.setCursor(20, 148);
        display.print("Add macro0..macro9 to config.yaml");
    } else {
        for (int i = 0; i < 5 && i < pendantMacros.count; i++) {
            int displayIndex = i + pendantMacros.scrollOffset;
            if (displayIndex >= pendantMacros.count) break;

            uint16_t bg;
            if (displayIndex == pendantMacros.selected && pendantMacros.pendingRun) {
                bg = COLOR_DARK_GREEN;
            } else if (displayIndex == pendantMacros.selected) {
                bg = COLOR_BUTTON_ACTIVE;
            } else {
                bg = COLOR_BUTTON_GRAY;
            }
            display.fillRoundRect(5, 40 + i * 40, 230, 36, 8, bg);
            display.setTextColor(COLOR_WHITE);
            display.setTextSize(1);
            display.setCursor(10, 52 + i * 40);
            display.print(macroLabel(displayIndex));
        }
    }

    // Scroll + Refresh row
    drawButton(5,   242, 72, 36, "<<",      COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(83,  242, 72, 36, "Refresh", COLOR_DARK_GREEN,  COLOR_WHITE, 1);
    drawButton(161, 242, 72, 36, ">>",      COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

    if (pendantMacros.pendingRun) {
        drawButton(5,   282, 110, 36, "Cancel", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
        drawButton(121, 282, 114, 36, "Run",    COLOR_DARK_GREEN,  COLOR_WHITE, 2);
    } else {
        drawButton(5, 282, 230, 36, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
    }
}

void handleMacrosTouch(int x, int y) {
    // File row taps
    for (int i = 0; i < 5; i++) {
        if (isTouchInBounds(x, y, 5, 40 + i * 40, 230, 36)) {
            int displayIndex = i + pendantMacros.scrollOffset;
            if (displayIndex < pendantMacros.count) {
                pendantMacros.selected   = displayIndex;
                pendantMacros.pendingRun = true;
                drawMacrosScreen();
            }
            return;
        }
    }

    // << scroll
    if (isTouchInBounds(x, y, 5, 242, 72, 36)) {
        if (pendantMacros.scrollOffset > 0) {
            pendantMacros.scrollOffset--;
            drawMacrosScreen();
        }
        return;
    }

    // Refresh — re-query all macros from controller
    if (isTouchInBounds(x, y, 83, 242, 72, 36)) {
        if (pendantConnected) {
            pendantMacros.selected   = -1;
            pendantMacros.pendingRun = false;
            enterMacros();
            drawMacrosScreen();
        }
        return;
    }

    // >> scroll
    if (isTouchInBounds(x, y, 161, 242, 72, 36)) {
        if (pendantMacros.scrollOffset + 5 < pendantMacros.count) {
            pendantMacros.scrollOffset++;
            drawMacrosScreen();
        }
        return;
    }

    // Bottom row
    if (pendantMacros.pendingRun) {
        // Cancel
        if (isTouchInBounds(x, y, 5, 282, 110, 36)) {
            pendantMacros.pendingRun = false;
            drawMacrosScreen();
            return;
        }
        // Run — $Macro=N sends the macro defined in config.yaml
        if (isTouchInBounds(x, y, 121, 282, 114, 36)) {
            if (pendantConnected && pendantMacros.selected >= 0) {
                char cmd[16];
                snprintf(cmd, sizeof(cmd), "$Macro=%d", pendantMacros.indices[pendantMacros.selected]);
                send_line(cmd);
                pendantMacros.pendingRun = false;
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
