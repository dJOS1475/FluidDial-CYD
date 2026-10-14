#include "pendant_shared.h"
#include "screen_sd_card.h"
#include "../FileParser.h"

// Sprite covers the file-list area: x=5..234, y=40..239 (230 x 200 px).
// Same pattern as screen_macros — prevents fillScreen flicker on STATE_UPDATE.

// Forward declarations for the dirty-state tracker (defined below).
static void invalidateSDRender();

// Row label — folders get a trailing slash, ".." is spelled out, and anything
// too long is truncated so it cannot overflow the 230 px row (6 px per char at
// textSize 1, minus the 5 px indent).
#define SD_LABEL_MAX 35
static String sdRowLabel(int i) {
    if (pendantSdCard.isDir[i] && strcmp(pendantSdCard.files[i], "..") == 0)
        return "..  (up a folder)";
    String label = String(pendantSdCard.files[i]) + (pendantSdCard.isDir[i] ? "/" : "");
    if (label.length() > SD_LABEL_MAX) label = label.substring(0, SD_LABEL_MAX - 3) + "...";
    return label;
}

// Absolute path of a listed entry.  "$SD/Run=" needs the full path once files can
// live in subfolders — the same form the old FileSelectScene/FilePreviewScene pair
// has always sent (e.g. $SD/Run=/sd/jobs/x.nc).
static String sdFullPath(int i) {
    String base = pendantSdCard.curPath;
    if (base == "/") base = "";
    return base + "/" + pendantSdCard.files[i];
}

// Ask the controller for a directory listing and park the screen in its loading
// state.  The generation counter tags the request so a reply that lands after we
// have navigated elsewhere is dropped rather than filling the wrong folder.
static void sdRequestDir(const char* path) {
    strlcpy(pendantSdCard.curPath, path, SD_PATH_MAX);
    int level = -1;
    for (const char* p = pendantSdCard.curPath; *p; p++) if (*p == '/') level++;
    pendantSdCard.dirLevel     = level < 0 ? 0 : level;
    pendantSdCard.listGen++;
    pendantSdCard.scrollOffset = 0;
    pendantSdCard.selectedFile = 0;
    pendantSdCard.pendingRun   = false;
    pendantSdCard.fileCount    = 0;
    pendantSdCard.loading      = true;
    pendantSdCard.loadFailed   = false;
    pendantSdCard.loadStartMs  = millis();
    if (pendantConnected) request_file_list(pendantSdCard.curPath);
    drawSDCardScreen();          // the bottom row changes with pendingRun
    // The list under the finger has just been replaced — don't let a held tap
    // re-fire onto the new rows (notably ".." at row 0 of the folder we opened).
    suppressTouchBounce();
}

static void sdDescend(int i) { sdRequestDir(sdFullPath(i).c_str()); }

static void sdAscend() {
    if (pendantSdCard.dirLevel <= 0) return;
    char up[SD_PATH_MAX];
    strlcpy(up, pendantSdCard.curPath, sizeof(up));
    char* slash = strrchr(up, '/');
    if (!slash || slash == up) strlcpy(up, "/sd", sizeof(up));
    else                       *slash = '\0';
    sdRequestDir(up);
}

// Repaint after a page change.  Only the list panel and the footer move, and the
// footer sits below the list sprite, so painting both directly avoids the
// full-screen fillScreen() that drawSDCardScreen() would do — no flicker on a
// fast spin.
static void drawSDFooter(bool force = false);
void updateSDCardFileList();
// updateSDCardFileList() now refreshes the footer itself (each has its own dirty
// check), so a page change only needs the one call.
static void sdRepaintPaged() { updateSDCardFileList(); }

// Dial — one detent pages the list by one screen, clamped at both ends.  Does NOT
// move the selection: Load/Run stay armed on the file you picked, so you can page
// away and still load it.
void handleSDCardDial(int delta) {
    int next = listPageStep(delta, pendantSdCard.fileCount, pendantSdCard.scrollOffset);
    if (next < 0) return;              // already at the first/last page
    pendantSdCard.scrollOffset = next;
    sdRepaintPaged();
}

void enterSDCard() {
    releasePanelSprites();

    pendantSdCard.pendingRun = false;
    invalidateSDRender();  // force the first paint after entry / full redraw

    g_expecting_json = false;  // clear any stuck request state from a prior screen

    // Request a fresh file list from the controller
    // Always re-enter at the card root rather than wherever we were last time.
    strlcpy(pendantSdCard.curPath, "/sd", SD_PATH_MAX);
    pendantSdCard.dirLevel     = 0;
    pendantSdCard.totalFiles   = 0;
    if (pendantConnected) {
        pendantSdCard.listGen++;
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

    // Footer first, with its own dirty check: it lives outside the list sprite and
    // must track fileCount changes (a listing arriving) even when the list area
    // itself short-circuits below.
    drawSDFooter();

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

    drawListCrumb(g, ox, oy, pendantSdCard.curPath);

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
        for (int i = 0; i < LIST_ROWS && i < pendantSdCard.fileCount; i++) {
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
            int ry = oy + LIST_ROW_Y0 + i * LIST_ROW_PITCH;
            g->fillRoundRect(ox, ry, 230, LIST_ROW_H, 8, bg);
            // Folders read as navigation, not jobs: cyan, with a trailing slash.
            g->setTextColor(pendantSdCard.isDir[displayIndex] ? COLOR_CYAN : COLOR_WHITE);
            g->setTextSize(1);
            g->setCursor(ox + 5, ry + 11);
            g->print(sdRowLabel(displayIndex));
        }
    }

    if (hasSprite) spriteFileDisplay.pushSprite(5, 40);
}

// The footer sits BELOW the list sprite, so updateSDCardFileList() never touches
// it — the list arriving asynchronously would otherwise leave the page count and
// the jog hint stuck in the empty-list state until a dial turn or tap repainted
// them.  It gets its own dirty check so the 100 ms tick can call it for free and
// it repaints the moment the listing lands.  force=true on a full redraw, where
// fillScreen() has already wiped it.
static void drawSDFooter(bool force) {
    int  pages = listPageCount(pendantSdCard.fileCount);
    int  page  = listPageIndex(pendantSdCard.scrollOffset) + 1;
    int  shown = pendantSdCard.fileCount == 0 ? 0 : pages;
    bool trunc = pendantSdCard.totalFiles > pendantSdCard.fileCount;
    bool live  = pendantSdCard.fileCount > 0 && pages > 1;

    static bool valid = false;
    static int  lastPage = -1, lastShown = -1;
    static bool lastTrunc = false, lastLive = false;
    if (!force && valid && lastPage == page && lastShown == shown &&
        lastTrunc == trunc && lastLive == live) {
        return;
    }
    drawListFooter(LIST_FOOTER_Y, page, shown, trunc, live);
    valid = true;
    lastPage = page; lastShown = shown; lastTrunc = trunc; lastLive = live;
}

void drawSDCardScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("SD CARD");

    // Full redraw — invalidate the dirty cache so the file-list area
    // gets painted (fillScreen above just cleared it back to background).
    invalidateSDRender();
    updateSDCardFileList();

    drawSDFooter(true);

    if (pendantSdCard.pendingRun) {
        drawButton(5,   282, 110, 36, "Load", COLOR_BLUE,       COLOR_WHITE, 2);
        drawButton(121, 282, 114, 36, "Run",  COLOR_DARK_GREEN, COLOR_WHITE, 2);
    } else {
        drawButton(5, 282, 230, 36, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
    }
}

void handleSDCardTouch(int x, int y) {
    // File row taps — first tap selects & arms confirmation
    for (int i = 0; i < LIST_ROWS; i++) {
        if (isTouchInBounds(x, y, 5, listRowHitY(i), 230, listRowHitH())) {
            int displayIndex = i + pendantSdCard.scrollOffset;
            if (displayIndex < pendantSdCard.fileCount) {
                if (pendantSdCard.isDir[displayIndex]) {
                    // ".." walks back up; any other folder is descended into.
                    if (strcmp(pendantSdCard.files[displayIndex], "..") == 0) sdAscend();
                    else                                                      sdDescend(displayIndex);
                } else {
                    pendantSdCard.selectedFile = displayIndex;
                    pendantSdCard.pendingRun   = true;
                    drawSDCardScreen();
                }
            }
            return;
        }
    }

    // JOG hint and PAGE readout both advance one page, wrapping back to the first.
    if (listFooterPageTap(x, y, LIST_FOOTER_Y)) {
        int next = listPageAdvanceWrap(pendantSdCard.fileCount, pendantSdCard.scrollOffset);
        if (next >= 0) { pendantSdCard.scrollOffset = next; sdRepaintPaged(); }
        return;
    }

    // Refresh — re-lists the CURRENT folder, not always the root.
    if (isTouchInBounds(x, y, rowBtnX(3, 2), LIST_FOOTER_Y, rowBtnWAt(3, 2), 36)) {
        if (pendantConnected) {
            sdRequestDir(pendantSdCard.curPath);
        }
        return;
    }

    // Bottom row — depends on pendingRun state
    if (pendantSdCard.pendingRun) {
        // LOAD — store filename, navigate to Status; green button will send run command
        if (isTouchInBounds(x, y, 5, 282, 110, 36)) {
            pendantSdCard.loadedFile = sdFullPath(pendantSdCard.selectedFile);
            pendantSdCard.pendingRun = false;
            currentPendantScreen = PSCREEN_STATUS;
            return;
        }
        // RUN — send command immediately
        if (isTouchInBounds(x, y, 121, 282, 114, 36)) {
            if (pendantConnected) {
                String cmd = "$SD/Run=" + sdFullPath(pendantSdCard.selectedFile);
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
