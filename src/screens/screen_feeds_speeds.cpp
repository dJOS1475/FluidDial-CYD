#include "pendant_shared.h"
#include "screen_feeds_speeds.h"
#include "screen_probe.h"   // PROBE_* colours — shared adjustable-field style

// Adjustable-field style matching probeDrawKVTouch(), rendered into a panel
// sprite `g` (these readouts update live, so they composite off-screen to stay
// flicker-free).  Label on top, large value + unit below; the border and value
// highlight (yellow) while the dial is active.
static void drawDialField(LovyanGFX* g, int ox, int oy, int w, int h,
                          int value, uint16_t valColor, bool active) {
    uint16_t bg  = active ? PROBE_SEL_BG   : PROBE_BG_SCREEN;
    uint16_t bdr = active ? PROBE_C_YELLOW : PROBE_C_DIMBLUE;
    g->fillRect(ox, oy, w, h, COLOR_BACKGROUND);   // clear corners to screen bg
    g->fillRoundRect(ox, oy, w, h, 2, bg);
    g->drawRoundRect(ox, oy, w, h, 2, bdr);

    g->setTextSize(1);
    g->setTextColor(active ? COLOR_WHITE : PROBE_C_LBLUE);
    g->setCursor(ox + 3, oy + 2);
    g->print("DIAL");

    char vbuf[8];
    snprintf(vbuf, sizeof(vbuf), "%d", value);
    g->setTextSize(2);
    g->setTextColor(active ? PROBE_C_YELLOW : valColor);
    g->setCursor(ox + 3, oy + 11);
    g->print(vbuf);

    g->setTextSize(1);
    g->setTextColor(PROBE_C_DIMBLUE);
    int16_t vw = g->textWidth(vbuf) * 2;
    g->setCursor(ox + 3 + vw + 1, oy + 14);
    g->print("%");
}

void enterFeedsSpeeds() {
    // Each panel uses a transient 16-bit sprite (see the update*() functions).
    releasePanelSprites();
}

void exitFeedsSpeeds() {
    pendantFeeds.dialMode = 0;
    releasePanelSprites();
}

void drawFeedsSpeedsScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("FEEDS & SPEEDS");

    updateFeedsSpeedsTopDisplay();

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 83);
    display.print("FEED OVERRIDE");

    String pcts[] = { "50%", "75%", "100%", "125%", "150%" };
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 78, 95, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
    }
    uint16_t bg3 = (3 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5, 137, 72, 37, pcts[3], bg3, COLOR_WHITE, 2);
    display.fillRoundRect(83, 137, 72, 37, 2, COLOR_BACKGROUND);
    updateFeedOverrideDisplay();
    uint16_t bg4 = (4 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(161, 137, 72, 37, pcts[4], bg4, COLOR_WHITE, 2);

    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(5, 182);
    display.print("SPINDLE OVERRIDE");

    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 78, 194, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
    }
    uint16_t bg3s = (3 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5, 236, 72, 37, pcts[3], bg3s, COLOR_WHITE, 2);
    display.fillRoundRect(83, 236, 72, 37, 2, COLOR_BACKGROUND);
    updateSpindleOverrideDisplay();
    uint16_t bg4s = (4 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(161, 236, 72, 37, pcts[4], bg4s, COLOR_WHITE, 2);

    drawButton(5, 280, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void updateFeedsSpeedsTopDisplay() {
    if (currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;

    int feedRate, spindleRPM;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    feedRate   = pendantMachine.feedRate;
    spindleRPM = pendantMachine.spindleRPM;
    xSemaphoreGive(stateMutex);

    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, 35, ox, oy, 5, 40);
    g->fillRect(ox, oy, 230, 35, COLOR_BACKGROUND);

    // Feed box
    g->fillRoundRect(ox + 0, oy + 0, 112, 35, 5, COLOR_DARKER_BG);
    g->setTextColor(COLOR_GRAY_TEXT);
    g->setTextSize(1);
    g->setCursor(ox + 5, oy + 3);
    g->print("FEED");
    g->setTextColor(COLOR_ORANGE);
    g->setTextSize(2);
    g->setCursor(ox + 5, oy + 13);
    g->print(feedRate);

    // Spindle box
    g->fillRoundRect(ox + 118, oy + 0, 112, 35, 5, COLOR_DARKER_BG);
    g->setTextColor(COLOR_GRAY_TEXT);
    g->setTextSize(1);
    g->setCursor(ox + 123, oy + 3);
    g->print("SPINDLE");
    g->setTextColor(COLOR_GREEN);
    g->setTextSize(2);
    g->setCursor(ox + 123, oy + 13);
    g->print(spindleRPM);

    endPanelSprite(230, 35, 5, 40);
}

void updateFeedOverrideDisplay() {
    if (currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;

    int fro;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    fro = pendantMachine.feedOverride;
    xSemaphoreGive(stateMutex);

    int ox, oy;
    bool active = (pendantFeeds.dialMode == 1);
    LovyanGFX* g = beginPanelSprite(72, 37, ox, oy, 83, 137);
    drawDialField(g, ox, oy, 72, 37, fro, COLOR_ORANGE, active);
    endPanelSprite(72, 37, 83, 137);
}

void updateSpindleOverrideDisplay() {
    if (currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;

    int sro;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    sro = pendantMachine.spindleOverride;
    xSemaphoreGive(stateMutex);

    int ox, oy;
    bool active = (pendantFeeds.dialMode == 2);
    LovyanGFX* g = beginPanelSprite(72, 37, ox, oy, 83, 236);
    drawDialField(g, ox, oy, 72, 37, sro, COLOR_GREEN, active);
    endPanelSprite(72, 37, 83, 236);
}

void redrawFeedOverrideButtons() {
    if (currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;
    String pcts[] = { "50%", "75%", "100%", "125%", "150%" };
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 78, 95, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
    }
    uint16_t bg3 = (3 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5, 137, 72, 37, pcts[3], bg3, COLOR_WHITE, 2);
    uint16_t bg4 = (4 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(161, 137, 72, 37, pcts[4], bg4, COLOR_WHITE, 2);
    updateFeedOverrideDisplay();
}

void redrawSpindleOverrideButtons() {
    if (currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;
    String pcts[] = { "50%", "75%", "100%", "125%", "150%" };
    for (int i = 0; i < 3; i++) {
        uint16_t bg = (i == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
        drawButton(5 + i * 78, 194, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
    }
    uint16_t bg3s = (3 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5, 236, 72, 37, pcts[3], bg3s, COLOR_WHITE, 2);
    uint16_t bg4s = (4 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(161, 236, 72, 37, pcts[4], bg4s, COLOR_WHITE, 2);
    updateSpindleOverrideDisplay();
}

// Send realtime commands to reach a target feed override %.
// Resets to 100% first, then applies fine ±1% steps.
static void applyFeedOverride(int targetPct) {
    if (!pendantConnected) return;
    fnc_realtime(FeedOvrReset);
    int delta = targetPct - 100;
    for (int i = 0; i < abs(delta); i++)
        fnc_realtime(delta > 0 ? FeedOvrFinePlus : FeedOvrFineMinus);
}

static void applySpindleOverride(int targetPct) {
    if (!pendantConnected) return;
    fnc_realtime(SpindleOvrReset);
    int delta = targetPct - 100;
    for (int i = 0; i < abs(delta); i++)
        fnc_realtime(delta > 0 ? SpindleOvrFinePlus : SpindleOvrFineMinus);
}

void handleFeedsSpeedsTouch(int x, int y) {
    int pcts[] = { 50, 75, 100, 125, 150 };

    // ── Feed override preset buttons ─────────────────────────────────────
    for (int i = 0; i < 3; i++) {
        if (isTouchInBounds(x, y, 5 + i * 78, 95, 72, 37)) {
            pendantFeeds.dialMode             = 0;
            pendantFeeds.selectedFeedOverride = i;
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                pendantMachine.feedOverride = pcts[i];
                xSemaphoreGive(stateMutex);
            }
            applyFeedOverride(pcts[i]);
            redrawFeedOverrideButtons();
            updateSpindleOverrideDisplay();  // deactivate spindle dial visual
            return;
        }
    }
    if (isTouchInBounds(x, y, 5, 137, 72, 37)) {
        pendantFeeds.dialMode             = 0;
        pendantFeeds.selectedFeedOverride = 3;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            pendantMachine.feedOverride = 125;
            xSemaphoreGive(stateMutex);
        }
        applyFeedOverride(125);
        redrawFeedOverrideButtons();
        updateSpindleOverrideDisplay();
        return;
    }

    // ── Feed override readout button (centre of row 2) ───────────────────
    if (isTouchInBounds(x, y, 83, 137, 72, 37)) {
        pendantFeeds.dialMode = (pendantFeeds.dialMode == 1) ? 0 : 1;  // toggle; deselects spindle
        updateFeedOverrideDisplay();
        updateSpindleOverrideDisplay();
        return;
    }

    if (isTouchInBounds(x, y, 161, 137, 72, 37)) {
        pendantFeeds.dialMode             = 0;
        pendantFeeds.selectedFeedOverride = 4;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            pendantMachine.feedOverride = 150;
            xSemaphoreGive(stateMutex);
        }
        applyFeedOverride(150);
        redrawFeedOverrideButtons();
        updateSpindleOverrideDisplay();
        return;
    }

    // ── Spindle override preset buttons ──────────────────────────────────
    for (int i = 0; i < 3; i++) {
        if (isTouchInBounds(x, y, 5 + i * 78, 194, 72, 37)) {
            pendantFeeds.dialMode                = 0;
            pendantFeeds.selectedSpindleOverride = i;
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                pendantMachine.spindleOverride = pcts[i];
                xSemaphoreGive(stateMutex);
            }
            applySpindleOverride(pcts[i]);
            redrawSpindleOverrideButtons();
            updateFeedOverrideDisplay();  // deactivate feed dial visual
            return;
        }
    }
    if (isTouchInBounds(x, y, 5, 236, 72, 37)) {
        pendantFeeds.dialMode                = 0;
        pendantFeeds.selectedSpindleOverride = 3;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            pendantMachine.spindleOverride = 125;
            xSemaphoreGive(stateMutex);
        }
        applySpindleOverride(125);
        redrawSpindleOverrideButtons();
        updateFeedOverrideDisplay();
        return;
    }

    // ── Spindle override readout button (centre of row 2) ────────────────
    if (isTouchInBounds(x, y, 83, 236, 72, 37)) {
        pendantFeeds.dialMode = (pendantFeeds.dialMode == 2) ? 0 : 2;  // toggle; deselects feed
        updateSpindleOverrideDisplay();
        updateFeedOverrideDisplay();
        return;
    }

    if (isTouchInBounds(x, y, 161, 236, 72, 37)) {
        pendantFeeds.dialMode                = 0;
        pendantFeeds.selectedSpindleOverride = 4;
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            pendantMachine.spindleOverride = 150;
            xSemaphoreGive(stateMutex);
        }
        applySpindleOverride(150);
        redrawSpindleOverrideButtons();
        updateFeedOverrideDisplay();
        return;
    }

    if (isTouchInBounds(x, y, 5, 280, 230, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    }
}
