#include "pendant_shared.h"
#include "screen_macros.h"

// Sprite covers the file-list area: x=5..234, y=40..239 (230 x 200 px).
// Rendering into it and pushing atomically prevents the fillScreen flicker that
// would otherwise occur on every DRO STATE_UPDATE (~200 ms).

// Forward declaration for the dirty-state tracker (defined below).
static void invalidateMacrosRender();

void enterMacros() {
    releasePanelSprites();

    pendantMacros.scrollOffset = 0;
    pendantMacros.selected     = -1;
    pendantMacros.pendingRun   = false;
    invalidateMacrosRender();   // force the first paint after entry

    if (pendantMacros.cacheValid) {
        // Cached list is still good — show it immediately without a fetch.
        pendantMacros.loading    = false;
        pendantMacros.loadFailed = false;
    } else {
        // First visit, or cache explicitly invalidated (Refresh button / disconnect).
        pendantMacros.loading    = true;
        pendantMacros.loadFailed = false;
        pendantMacros.count      = 0;
        if (pendantConnected) requestMacros();
    }

    // Flicker-free list sprite, now 8-bit (~46 KB, was ~92 KB) so it allocates
    // far more often; direct-draw fallback when heap is tight.
    allocPanelSprite(spriteFileDisplay, 230, 200, 60000);
}

void exitMacros() {
    spriteFileDisplay.deleteSprite();
}

// Re-fetch the macro list from the controller without re-allocating sprites.
// Called by the Refresh button — enterMacros() would needlessly delete and
// re-create the file-list sprite on every refresh.
static void refreshMacros() {
    pendantMacros.cacheValid   = false;  // force a fresh UART fetch
    pendantMacros.scrollOffset = 0;
    pendantMacros.selected     = -1;
    pendantMacros.pendingRun   = false;
    pendantMacros.loading      = true;
    pendantMacros.count        = 0;
    invalidateMacrosRender();   // force repaint into the loading state
    if (pendantConnected) requestMacros();
}

// Truncate macro name to fit one button-width line at textSize 1 (~36 chars)
static String macroLabel(int displayIndex) {
    String label = pendantMacros.content[displayIndex];
    if (label.length() > 36) label = label.substring(0, 33) + "...";
    return label;
}

// Snapshot of last-rendered state.  See screen_sd_card.cpp for rationale —
// in direct-draw mode the 100 ms tick would otherwise flicker; we skip the
// paint when nothing visible has changed.
struct MacrosRenderState {
    bool valid;
    bool connected;
    bool loading;
    bool loadFailed;
    bool pendingRun;
    int  count;
    int  scrollOffset;
    int  selected;
};
static MacrosRenderState _lastMacrosRender = {};

static void invalidateMacrosRender() { _lastMacrosRender.valid = false; }

// Renders the dynamic file-list area.  Uses the sprite for flicker-free
// updates when it's been allocated; falls back to drawing directly into the
// display otherwise.  The area is ALWAYS painted — bailing out silently
// (the previous behaviour) made the screen look blank on WiFi-enabled
// builds where the sprite couldn't allocate.
void updateMacrosFileList() {
    if (currentPendantScreen != PSCREEN_MACROS) return;

    // UI safety deadline: the HTTP fetch task always delivers a terminal
    // onFilesList()/onError() callback, but if it ever hangs (or its task
    // dies), this guarantees "Loading…" can't spin forever.  35 s comfortably
    // exceeds the worst-case retry budget in fetch_macros_http_task
    // (3 tries × 2 files × 5 s ≈ 30 s).
    if (pendantMacros.loading && pendantMacros.loadStartMs != 0 &&
        (millis() - pendantMacros.loadStartMs) > 35000) {
        pendantMacros.loading    = false;
        pendantMacros.loadFailed = true;
    }

    MacrosRenderState cur = {
        /*valid*/        true,
        /*connected*/    pendantConnected,
        /*loading*/      pendantMacros.loading,
        /*loadFailed*/   pendantMacros.loadFailed,
        /*pendingRun*/   pendantMacros.pendingRun,
        /*count*/        pendantMacros.count,
        /*scrollOffset*/ pendantMacros.scrollOffset,
        /*selected*/     pendantMacros.selected,
    };
    if (_lastMacrosRender.valid &&
        _lastMacrosRender.connected    == cur.connected &&
        _lastMacrosRender.loading      == cur.loading &&
        _lastMacrosRender.loadFailed   == cur.loadFailed &&
        _lastMacrosRender.pendingRun   == cur.pendingRun &&
        _lastMacrosRender.count        == cur.count &&
        _lastMacrosRender.scrollOffset == cur.scrollOffset &&
        _lastMacrosRender.selected     == cur.selected) {
        return;
    }
    _lastMacrosRender = cur;

    const bool hasSprite = spriteFileDisplay.getBuffer() != nullptr;
    LovyanGFX* g = hasSprite ? (LovyanGFX*)&spriteFileDisplay
                             : (LovyanGFX*)&display;
    // Sprite is pushed at (5, 40); direct-draw uses absolute screen coords.
    const int ox = hasSprite ? 0 : 5;
    const int oy = hasSprite ? 0 : 40;

    if (hasSprite) {
        spriteFileDisplay.fillSprite(COLOR_BACKGROUND);
    } else {
        display.fillRect(5, 40, 230, 200, COLOR_BACKGROUND);
    }

    if (pendantMacros.loading) {
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(2);
        g->setCursor(ox + 45, oy + 100);
        g->print(pendantConnected ? "Loading..." : "Not connected.");
    } else if (pendantMacros.loadFailed) {
        g->setTextColor(COLOR_ORANGE);
        g->setTextSize(1);
        g->setCursor(ox + 15, oy + 90);
        g->print("Couldn't load macros.");
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 15, oy + 108);
        g->print("Tap Refresh to try again.");
    } else if (pendantMacros.count == 0) {
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        g->setCursor(ox + 15, oy + 90);
        g->print("No macros found.");
        g->setCursor(ox + 15, oy + 108);
        g->print("Add macros in FluidNC preferences.");
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
            g->fillRoundRect(ox, oy + i * 40, 230, 36, 8, bg);
            g->setTextColor(COLOR_WHITE);
            g->setTextSize(1);
            g->setCursor(ox + 5, oy + 12 + i * 40);
            g->print(macroLabel(displayIndex));
        }
    }

    if (hasSprite) spriteFileDisplay.pushSprite(5, 40);
}

void drawMacrosScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("MACROS");

    // Full redraw — invalidate the dirty cache so the file-list area paints.
    invalidateMacrosRender();
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
            refreshMacros();
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
                currentPendantScreen = PSCREEN_STATUS;
            }
            return;
        }
    } else {
        if (isTouchInBounds(x, y, 5, 282, 230, 36)) {
            currentPendantScreen = PSCREEN_MAIN_MENU;
        }
    }
}
