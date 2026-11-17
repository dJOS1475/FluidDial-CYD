#include "pendant_shared.h"
#include "screen_main_menu.h"
#include "../Comms.h"
#ifdef USE_ESPNOW
#include "../PeerLink.h"
#endif

void enterMainMenu() {
    // Status bar uses a TRANSIENT 16-bit panel (see updateMainMenuDisplay) so it
    // renders true colour and only one buffer is live at a time.
    releasePanelSprites();
}

void exitMainMenu() {
    releasePanelSprites();
}

void drawMainMenu() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("MAIN MENU");

    // Framed status panel; updateMainMenuDisplay() fills it.
    uiPanel(&display, 5, 40, 230, 65, 6);
    updateMainMenuDisplay();

    int btnY   = 115;
    int btnH   = 47;
    int btnGap = 52;

    // One button blue throughout; each destination carries its icon.
    const uint16_t B = COLOR_BLUE, W = COLOR_WHITE;
    drawButton(5, btnY, 112, btnH, "Jog", B, W, 2, UI_ICON_JOG);
    drawMultiLineButton(123, btnY, 112, btnH, "Work", "Area", B, W, 2, UI_ICON_WORK);
    drawMultiLineButton(5, btnY + btnGap, 112, btnH, "Feeds &", "Speeds", B, W, 2, UI_ICON_GAUGE);
    drawMultiLineButton(123, btnY + btnGap, 112, btnH, "Spindle", "Control", B, W, 2, UI_ICON_SPINDLE);
    drawButton(5, btnY + btnGap * 2, 112, btnH, "Macros", B, W, 2, UI_ICON_MACRO);
    drawButton(123, btnY + btnGap * 2, 112, btnH, "SD Card", B, W, 2, UI_ICON_SD);
    drawButton(5, btnY + btnGap * 3, 112, btnH, "Probe", B, W, 2, UI_ICON_PROBE);
    drawButton(123, btnY + btnGap * 3, 112, btnH, "Status", B, W, 2, UI_ICON_STATUS);
}

// Wireless link problems are reported in the STATUS panel rather than as an
// overlay from the Connection screen — an overlay drew on top of the menu
// buttons and was unreadable.  Returns null when the radio is fine (or not in
// use) so the panel falls through to the normal machine status; UART has no
// link to report and always returns null.
struct LinkFault { const char* big; uint16_t col; const char* sub; };

static bool espNowLinkFault(LinkFault& out) {
#ifdef USE_ESPNOW
    static char subBuf[40];
    if (comms_active_mode() != COMMS_MODE_ESPNOW) return false;
    if (!espnow_radio_ready()) {
        out = { "Radio down", COLOR_RED, "ESP-NOW failed to start" };
        return true;
    }
    if (!espnow_has_saved_pairing()) {
        out = { "Not paired", COLOR_RED, "Connection > Pair New" };
        return true;
    }
    if (!espnow_is_connected()) {
        ESPNowProfileInfo info;
        const int idx = espnow_active_profile_index();
        const char* name = (idx >= 0 && espnow_get_profile((size_t)idx, info) && info.hostname[0])
                           ? info.hostname : "machine";
        snprintf(subBuf, sizeof(subBuf), "Reconnecting to %s", name);
        out = { "No link", COLOR_ORANGE, subBuf };
        return true;
    }
#else
    (void)out;
#endif
    return false;
}

void updateMainMenuDisplay() {
    if (currentPendantScreen != PSCREEN_MAIN_MENU) return;

    // Snapshot under the lock first; skip the frame if it's briefly held rather
    // than read the status String unlocked (a concurrent realloc on Core 0 would
    // corrupt the heap).
    String statusStr;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    statusStr = pendantMachine.status;
    xSemaphoreGive(stateMutex);

    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, 65, ox, oy, 5, 40);

    // Caption / big value / optional sub-line, chosen by state:
    //   ESP-NOW link fault -> "ESP-NOW" caption, so a dead radio link never looks
    //                         like a machine sitting in Alarm
    //   not yet synced     -> "Connecting" (link down) / "Syncing" (fetching config)
    //   Alarm              -> the alarm's description over a red ALARM
    //   otherwise          -> STATUS over the machine state
    const char* caption;
    const char* big;
    uint16_t    bigCol;
    const char* sub = nullptr;
    String      desc;
    LinkFault   lf;
    if (espNowLinkFault(lf)) {
        caption = "ESP-NOW"; big = lf.big; bigCol = lf.col; sub = lf.sub;
    } else if (!pendantSynced || statusStr == "N/C" || statusStr.length() == 0) {
        caption = "STATUS"; big = pendantConnected ? "Syncing" : "Connecting"; bigCol = COLOR_ORANGE;
    } else if (statusStr.startsWith("Alarm")) {
        desc = alarmDescription(statusStr);
        caption = desc.c_str(); big = "ALARM"; bigCol = TFT_RED;
    } else {
        caption = "STATUS"; big = statusStr.c_str(); bigCol = COLOR_CYAN;
    }

    uiPanel(g, ox, oy, 230, 65, 6);
    // Caption (GLCD, section-label style) + big FreeSansBold value (+ sub line)
    // centred as one block, caps to caps.
    const int GAP = 7, cx = ox + 115;
    int ca, cb, ba, bb;
    uiText(g, 1);
    uiCapBox(g, ca, cb);
    uiText(g, 4);
    if (g->textWidth(big) > 220) uiText(g, 3);   // "Connecting" etc. at 12pt if 18pt won't fit
    const lgfx::IFont* bigFont = g->getFont();
    uiCapBox(g, ba, bb);
    int blockH = (cb - ca) + GAP + (bb - ba) + (sub ? 7 + 5 : 0);
    int top    = (int)lroundf(oy + 65 / 2.0f - blockH / 2.0f);

    uiText(g, 1);
    g->setTextColor(COLOR_GRAY_TEXT);
    g->setCursor(cx - g->textWidth(caption) / 2, top - ca);
    g->print(caption);
    top += (cb - ca) + GAP;

    g->setFont(bigFont);
    g->setTextColor(bigCol);
    g->setCursor(cx - g->textWidth(big) / 2, top - ba);
    g->print(big);
    top += (bb - ba) + 5;

    uiText(g, 1);
    if (sub) {
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(cx - g->textWidth(sub) / 2, top);
        g->print(sub);
    }

    endPanelSprite(230, 65, 5, 40);
}

void handleMainMenuTouch(int x, int y) {
    int btnY   = 115;
    int btnH   = 47;
    int btnGap = 52;

    if      (isTouchInBounds(x, y, 5,   btnY,              112, btnH)) currentPendantScreen = PSCREEN_JOG_HOMING;
    else if (isTouchInBounds(x, y, 123, btnY,              112, btnH)) currentPendantScreen = PSCREEN_PROBING_WORK;
    else if (isTouchInBounds(x, y, 5,   btnY + btnGap,     112, btnH)) currentPendantScreen = PSCREEN_FEEDS_SPEEDS;
    else if (isTouchInBounds(x, y, 123, btnY + btnGap,     112, btnH)) currentPendantScreen = PSCREEN_SPINDLE_CONTROL;
    else if (isTouchInBounds(x, y, 5,   btnY + btnGap * 2, 112, btnH)) currentPendantScreen = PSCREEN_MACROS;
    else if (isTouchInBounds(x, y, 123, btnY + btnGap * 2, 112, btnH)) currentPendantScreen = PSCREEN_SD_CARD;
    else if (isTouchInBounds(x, y, 5,   btnY + btnGap * 3, 112, btnH)) currentPendantScreen = PSCREEN_PROBE;
    else if (isTouchInBounds(x, y, 123, btnY + btnGap * 3, 112, btnH)) currentPendantScreen = PSCREEN_STATUS;
}
