#include "pendant_shared.h"
#include "screen_fluidnc.h"
#include <Preferences.h>

extern Preferences preferences;

void enterFluidNC() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();

    if (ESP.getFreeHeap() > 60000) {
        spriteStatusBar.createSprite(230, 60);   // version / network panel
        spriteAxisDisplay.createSprite(230, 70);  // resources panel
    }
}

void exitFluidNC() {
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();

    // Persist rotation to NVS only on screen exit. Writing on every encoder
    // detent during a fast spin would otherwise hammer flash; coalescing into
    // a single write per visit eliminates that risk.
    if (pendantMachine.rotationDirty) {
        preferences.begin("pendant", false);
        preferences.putInt("rotation", pendantMachine.rotation);
        preferences.end();
        pendantMachine.rotationDirty = false;
    }
}

// Redraws both dynamic panels via sprites — no fillScreen, no flicker
void updateFluidNCDisplay() {
    if (!spriteStatusBar.getBuffer() || !spriteAxisDisplay.getBuffer()) return;

    // Snapshot dynamic values under mutex
    String fluidDialVer, fluidNCVer, connStatus, dispRotation, baudRate, ip, ssid;
    bool connected = pendantConnected;

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        fluidDialVer = pendantMachine.fluidDialVersion;
        fluidNCVer   = pendantMachine.fluidNCVersion;
        connStatus   = pendantMachine.connectionStatus;
        dispRotation = pendantMachine.displayRotation;
        baudRate     = pendantMachine.baudRate;
        ip           = pendantMachine.ipAddress;
        ssid         = pendantMachine.wifiSSID;
        xSemaphoreGive(stateMutex);
    }

    // ── Panel 1: Version / Network ──────────────────────────────────────────
    spriteStatusBar.fillSprite(COLOR_BACKGROUND);
    spriteStatusBar.fillRoundRect(0, 0, 230, 60, 5, COLOR_DARKER_BG);

    spriteStatusBar.setTextColor(COLOR_GRAY_TEXT); spriteStatusBar.setTextSize(1);
    spriteStatusBar.setCursor(5, 5);  spriteStatusBar.print("FLUIDNC-CYD");
    spriteStatusBar.setTextColor(COLOR_GREEN);
    spriteStatusBar.setCursor(5, 17); spriteStatusBar.print(fluidDialVer);

    spriteStatusBar.setTextColor(COLOR_GRAY_TEXT); spriteStatusBar.setTextSize(1);
    spriteStatusBar.setCursor(5, 35);  spriteStatusBar.print("FLUIDNC");
    spriteStatusBar.setTextColor(COLOR_GREEN);
    spriteStatusBar.setCursor(5, 47); spriteStatusBar.print(fluidNCVer);

    spriteStatusBar.setTextColor(COLOR_GRAY_TEXT); spriteStatusBar.setTextSize(1);
    spriteStatusBar.setCursor(115, 5);  spriteStatusBar.print("IP ADDRESS");
    spriteStatusBar.setTextColor(COLOR_CYAN);
    spriteStatusBar.setCursor(115, 17); spriteStatusBar.print(ip.length() ? ip : "---");

    spriteStatusBar.setTextColor(COLOR_GRAY_TEXT); spriteStatusBar.setTextSize(1);
    spriteStatusBar.setCursor(115, 35);  spriteStatusBar.print("WIFI SSID");
    spriteStatusBar.setTextColor(COLOR_CYAN);
    spriteStatusBar.setCursor(115, 47); spriteStatusBar.print(ssid.length() ? ssid : "---");

    spriteStatusBar.pushSprite(5, 40);

    // ── Panel 2: Resources ──────────────────────────────────────────────────
    spriteAxisDisplay.fillSprite(COLOR_BACKGROUND);
    spriteAxisDisplay.fillRoundRect(0, 0, 230, 70, 5, COLOR_DARKER_BG);

    spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT); spriteAxisDisplay.setTextSize(1);
    spriteAxisDisplay.setCursor(5, 5); spriteAxisDisplay.print("FREE HEAP");
    spriteAxisDisplay.setTextColor(COLOR_ORANGE); spriteAxisDisplay.setTextSize(2);
    spriteAxisDisplay.setCursor(5, 20);
    spriteAxisDisplay.print(ESP.getFreeHeap() / 1024);
    spriteAxisDisplay.print(" KB");

    spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT); spriteAxisDisplay.setTextSize(1);
    spriteAxisDisplay.setCursor(115, 5); spriteAxisDisplay.print("STATUS");
    spriteAxisDisplay.setTextColor(connected ? COLOR_GREEN : COLOR_RED); spriteAxisDisplay.setTextSize(1);
    spriteAxisDisplay.setCursor(115, 20); spriteAxisDisplay.print(connStatus);

    spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT); spriteAxisDisplay.setTextSize(1);
    spriteAxisDisplay.setCursor(5, 48); spriteAxisDisplay.print("ROTATION");
    spriteAxisDisplay.setTextColor(COLOR_CYAN); spriteAxisDisplay.setTextSize(1);
    spriteAxisDisplay.setCursor(5, 58); spriteAxisDisplay.print(dispRotation);

    spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT); spriteAxisDisplay.setTextSize(1);
    spriteAxisDisplay.setCursor(115, 48); spriteAxisDisplay.print("Jog Dial");
    spriteAxisDisplay.setTextColor(COLOR_CYAN); spriteAxisDisplay.setTextSize(1);
    spriteAxisDisplay.setCursor(115, 58); spriteAxisDisplay.print("Rotate");

    spriteAxisDisplay.pushSprite(5, 186);
}

void drawFluidNCScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("FLUIDNC");

    // Connection panel — static content, drawn once directly
    display.fillRoundRect(5, 108, 230, 70, 5, COLOR_DARKER_BG);
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(10, 113); display.print("CONNECTION");
    display.setCursor(10, 130); display.print("Baud:");
    display.setTextColor(COLOR_ORANGE); display.setTextSize(2);
    display.setCursor(100, 127); display.print(pendantMachine.baudRate);
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(10, 148); display.print("Port:");
    display.setTextColor(COLOR_CYAN); display.setTextSize(1);
    display.setCursor(10, 160); display.print(pendantMachine.port);

    drawButton(5,   272, 112, 40, "Main Menu", COLOR_BLUE,        COLOR_WHITE, 2);
    drawButton(123, 272, 112, 40, "Status",    COLOR_BLUE,        COLOR_WHITE, 2);

    // Dynamic panels drawn via sprites (no flicker)
    updateFluidNCDisplay();
}

void handleFluidNCTouch(int x, int y) {
    // Just assign — handlePendantTouch() observes the change and runs navigateTo() once.
    // Calling navigateTo() here would cause a double exit/enter cycle.
    if (isTouchInBounds(x, y, 5, 272, 112, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    } else if (isTouchInBounds(x, y, 123, 272, 112, 40)) {
        currentPendantScreen = PSCREEN_STATUS;
    }
}
