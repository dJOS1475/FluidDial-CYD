#include "pendant_shared.h"
#include "screen_macros.h"

// Sprite covers the file-list area: x=5..234, y=40..239 (230 x 200 px).
// Rendering into it and pushing atomically prevents the fillScreen flicker that
// would otherwise occur on every DRO STATE_UPDATE (~200 ms).

void enterMacros() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;

    pendantMacros.scrollOffset = 0;
    pendantMacros.selected     = -1;
    pendantMacros.pendingRun   = false;

    if (pendantMacros.cacheValid) {
        // Cached list is still good — show it immediately without a UART fetch.
        pendantMacros.loading = false;
    } else {
        // First visit, or cache explicitly invalidated (Refresh button / disconnect).
        pendantMacros.loading = true;
        pendantMacros.count   = 0;
        if (pendantConnected) requestMacros();
    }

    // Allocate sprite for the file list area.  Check heap first to avoid crash.
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

void exitMacros() {
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;
}

// Truncate macro name to fit one button-width line at textSize 1 (~36 chars)
static String macroLabel(int displayIndex) {
    String label = pendantMacros.content[displayIndex];
    if (label.length() > 36) label = label.substring(0, 33) + "...";
    return label;
}

// Renders the dynamic file-list area into spriteFileDisplay and pushes it.
// Coordinates are relative to the sprite origin (sprite is pushed at x=5, y=40).
void updateMacrosFileList() {
    if (!spritesInitialized || currentPendantScreen != PSCREEN_MACROS) return;
    if (!spriteFileDisplay.getBuffer()) return;

    spriteFileDisplay.fillSprite(COLOR_BACKGROUND);

    if (pendantMacros.loading) {
        spriteFileDisplay.setTextColor(COLOR_GRAY_TEXT);
        spriteFileDisplay.setTextSize(2);
        spriteFileDisplay.setCursor(45, 100);   // abs y=140 → rel y=100
        spriteFileDisplay.print(pendantConnected ? "Loading..." : "Not connected.");
    } else if (pendantMacros.count == 0) {
        spriteFileDisplay.setTextColor(COLOR_GRAY_TEXT);
        spriteFileDisplay.setTextSize(1);
        spriteFileDisplay.setCursor(15, 90);    // abs y=130 → rel y=90
        spriteFileDisplay.print("No macros found.");
        spriteFileDisplay.setCursor(15, 108);   // abs y=148 → rel y=108
        spriteFileDisplay.print("Add macros in FluidNC preferences.");
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
            // Row at sprite-relative y = i*40 (abs y = 40 + i*40, minus push offset 40)
            spriteFileDisplay.fillRoundRect(0, i * 40, 230, 36, 8, bg);
            spriteFileDisplay.setTextColor(COLOR_WHITE);
            spriteFileDisplay.setTextSize(1);
            spriteFileDisplay.setCursor(5, 12 + i * 40);   // abs y=52+i*40 → rel y=12+i*40
            spriteFileDisplay.print(macroLabel(displayIndex));
        }
    }

    spriteFileDisplay.pushSprite(5, 40);
}

void drawMacrosScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("MACROS");

    // File list area — sprite render (no flicker on repeated STATE_UPDATE calls)
    updateMacrosFileList();

    // Static bottom rows — drawn once; only redrawn when touch changes pendingRun state
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
            updateMacrosFileList();
        }
        return;
    }

    // Refresh — bust the cache and re-query all macros from controller
    if (isTouchInBounds(x, y, 83, 242, 72, 36)) {
        if (pendantConnected) {
            pendantMacros.cacheValid = false;  // force a fresh UART fetch
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
            updateMacrosFileList();
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
        // Run — dispatch based on filename prefix set by FileParser
        if (isTouchInBounds(x, y, 121, 282, 114, 36)) {
            if (pendantConnected && pendantMacros.selected >= 0) {
                String fn = pendantMacros.filename[pendantMacros.selected];
                char   cmd[128];
                if (fn.startsWith("/sd/")) {
                    // SD file: strip /sd/ prefix for $SD/Run
                    snprintf(cmd, sizeof(cmd), "$SD/Run=%s", fn.c_str() + 4);
                } else if (fn.startsWith("/localfs/")) {
                    // Local flash file
                    snprintf(cmd, sizeof(cmd), "$Localfs/Run=%s", fn.c_str() + 9);
                } else if (fn.startsWith("cmd:")) {
                    // Raw UART command
                    snprintf(cmd, sizeof(cmd), "%s", fn.c_str() + 4);
                } else {
                    // Unknown — send as-is
                    snprintf(cmd, sizeof(cmd), "%s", fn.c_str());
                }
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
