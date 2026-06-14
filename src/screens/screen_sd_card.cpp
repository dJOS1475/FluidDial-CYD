#include "pendant_shared.h"
#include "screen_sd_card.h"
#include "../FileParser.h"

// Sprite covers the file-list area: x=5..234, y=40..239 (230 x 200 px).
// Same pattern as screen_macros — prevents fillScreen flicker on STATE_UPDATE.

// Forward declarations for the dirty-state tracker (defined below).
static void invalidateSDRender();

void enterSDCard() {
    releasePanelSprites();

    pendantSdCard.pendingRun = false;
    invalidateSDRender();  // force the first paint after entry / full redraw

    g_expecting_json = false;  // clear any stuck request state from a prior screen

    // Request a fresh file list from the controller
    if (pendantConnected) {
        pendantSdCard.loading      = true;
        pendantSdCard.loadFailed   = false;
        pendantSdCard.loadStartMs  = millis();   // arm the UI loading deadline
        pendantSdCard.fileCount    = 0;
        pendantSdCard.scrollOffset = 0;
        pendantSdCard.selectedFile = 0;
        request_file_list("/sd");
    }

    // Flicker-free list sprite, now 8-bit (230 x 200 x 1 = ~46 KB, was ~92 KB at
    // 16-bit) so it allocates far more often.  Falls back to direct draw (slight
    // flicker, never blank) when heap is too tight.
    allocPanelSprite(spriteFileDisplay, 230, 200, 60000);
}

void exitSDCard() {
    spriteFileDisplay.deleteSprite();
}

// Snapshot of last-rendered state.  In direct-draw mode (no sprite) we
// compare against this every 100 ms tick and skip the repaint when nothing
// has changed — otherwise the periodic clear+redraw cycle is visible as
// flicker.  In sprite mode the comparison is harmless: pushSprite is atomic
// and would be flicker-free regardless, but skipping the work saves cycles.
struct SdRenderState {
    bool   valid;          // false = force a paint
    bool   connected;
    bool   loading;
    bool   loadFailed;
    bool   pendingRun;
    int    fileCount;
    int    scrollOffset;
    int    selectedFile;
    int    listGeneration; // bumped externally when file names change
};
static SdRenderState _lastRender = {};
static int _sdListGeneration = 0;

// Force the next updateSDCardFileList() to repaint regardless of state
// comparison.  Used by enterSDCard() and drawSDCardScreen() (full redraws),
// since after a fillScreen() the dynamic area is blank and must be filled in.
static void invalidateSDRender() { _lastRender.valid = false; }

// Called by the onFilesList()-equivalent paths if file *names* change but
// the count / indices don't (rare — typically refresh that returns the same
// number of files but different ones).  Currently nothing bumps this; the
// hook is here so a future "live update" path can poke it.
void sdCardListChanged() { _sdListGeneration++; }

// Renders the dynamic file-list area.  Uses the sprite for flicker-free
// updates when it's been allocated; falls back to drawing directly into the
// display otherwise.  Either way the area is ALWAYS painted — bailing out
// silently (the previous behaviour) made the screen look blank on builds
// where the sprite couldn't allocate.
void updateSDCardFileList() {
    if (currentPendantScreen != PSCREEN_SD_CARD) return;

    // UI safety deadline.  The list request is fire-once (no auto-retry), so if
    // its reply is ever dropped, "Loading…" would otherwise sit forever.  After
    // 10 s, fail it cleanly so the screen invites a manual Refresh instead.
    if (pendantSdCard.loading && pendantSdCard.loadStartMs != 0 &&
        (millis() - pendantSdCard.loadStartMs) > 10000) {
        pendantSdCard.loading    = false;
        pendantSdCard.loadFailed = true;
    }

    // Skip the paint if nothing visible has changed since the last call.
    // Eliminates the 100 ms flicker tick in direct-draw mode.
    SdRenderState cur = {
        /*valid*/          true,
        /*connected*/      pendantConnected,
        /*loading*/        pendantSdCard.loading,
        /*loadFailed*/     pendantSdCard.loadFailed,
        /*pendingRun*/     pendantSdCard.pendingRun,
        /*fileCount*/      pendantSdCard.fileCount,
        /*scrollOffset*/   pendantSdCard.scrollOffset,
        /*selectedFile*/   pendantSdCard.selectedFile,
        /*listGeneration*/ _sdListGeneration,
    };
    if (_lastRender.valid &&
        _lastRender.connected      == cur.connected &&
        _lastRender.loading        == cur.loading &&
        _lastRender.loadFailed     == cur.loadFailed &&
        _lastRender.pendingRun     == cur.pendingRun &&
        _lastRender.fileCount      == cur.fileCount &&
        _lastRender.scrollOffset   == cur.scrollOffset &&
        _lastRender.selectedFile   == cur.selectedFile &&
        _lastRender.listGeneration == cur.listGeneration) {
        return;
    }
    _lastRender = cur;

    const bool hasSprite = spriteFileDisplay.getBuffer() != nullptr;
    // Pick the target canvas.  LGFX_Sprite and LGFX_Device share the
    // LovyanGFX base class, so most drawing calls work on either.
    LovyanGFX* g = hasSprite ? (LovyanGFX*)&spriteFileDisplay
                             : (LovyanGFX*)&display;
    // Sprite is pushed at (5, 40); direct-draw uses absolute screen coords.
    const int ox = hasSprite ? 0 : 5;
    const int oy = hasSprite ? 0 : 40;

    // Clear the area
    if (hasSprite) {
        spriteFileDisplay.fillSprite(COLOR_BACKGROUND);
    } else {
        display.fillRect(5, 40, 230, 200, COLOR_BACKGROUND);
    }

    if (pendantSdCard.loading) {
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(2);
        g->setCursor(ox + 50, oy + 100);
        g->print("Loading...");
    } else if (pendantSdCard.loadFailed) {
        g->setTextColor(COLOR_ORANGE);
        g->setTextSize(1);
        g->setCursor(ox + 15, oy + 90);
        g->print("Couldn't load file list.");
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 15, oy + 108);
        g->print("Tap Refresh to try again.");
    } else if (pendantSdCard.fileCount == 0) {
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setTextSize(1);
        g->setCursor(ox + 15, oy + 90);
        g->print(pendantConnected ? "No GCode files found." : "Not connected.");
        g->setCursor(ox + 15, oy + 108);
        g->print("Press Refresh to retry.");
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
            g->fillRoundRect(ox, oy + i * 40, 230, 36, 8, bg);
            g->setTextColor(COLOR_WHITE);
            g->setTextSize(1);
            g->setCursor(ox + 5, oy + 12 + i * 40);
            g->print(pendantSdCard.files[displayIndex]);
        }
    }

    if (hasSprite) spriteFileDisplay.pushSprite(5, 40);
}

void drawSDCardScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("SD CARD");

    // Full redraw — invalidate the dirty cache so the file-list area
    // gets painted (fillScreen above just cleared it back to background).
    invalidateSDRender();
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
            pendantSdCard.loadFailed   = false;
            pendantSdCard.loadStartMs  = millis();   // re-arm the deadline
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
            currentPendantScreen = PSCREEN_STATUS;
            return;
        }
        // RUN — send command immediately
        if (isTouchInBounds(x, y, 121, 282, 114, 36)) {
            if (pendantConnected) {
                String cmd = "$SD/Run=" + pendantSdCard.files[pendantSdCard.selectedFile];
                send_line(cmd.c_str());
                pendantSdCard.loadedFile = "";
                pendantSdCard.pendingRun = false;
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
