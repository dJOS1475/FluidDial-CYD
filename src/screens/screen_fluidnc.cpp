#include "pendant_shared.h"
#include "screen_fluidnc.h"
#include "../Comms.h"          // comms_active_mode()
#include <Preferences.h>

extern Preferences preferences;

void enterFluidNC() {
    // Both panels use transient 16-bit sprites (see updateFluidNCDisplay).
    releasePanelSprites();
}

void exitFluidNC() {
    releasePanelSprites();

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

// Redraws both dynamic panels.  Uses sprites for flicker-free updates when
// they're allocated; falls back to drawing directly into the display when
// they're not (heap fragmented).  Previously this function returned early
// on sprite-alloc failure, leaving the entire version/network/resources
// area blank — which made the FluidNC screen look mostly empty.
void updateFluidNCDisplay() {
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

    // ── Panel 1: Version / Network (sprite pushed at 5, 40) ─────────────────
    {
        int ox, oy;
        LovyanGFX* g = beginPanelSprite(230, 60, ox, oy, 5, 40);
        g->fillRect(ox, oy, 230, 60, COLOR_BACKGROUND);        // black corners
        g->fillRoundRect(ox, oy, 230, 60, 5, COLOR_DARKER_BG); // rounded panel

        g->setTextColor(COLOR_GRAY_TEXT); g->setTextSize(1);
        g->setCursor(ox + 5, oy + 5);  g->print("FluidDial-CYD");
        g->setTextColor(COLOR_GREEN);
        g->setCursor(ox + 5, oy + 17); g->print(fluidDialVer);

        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 5, oy + 35);  g->print("FluidNC");
        g->setTextColor(COLOR_GREEN);
        g->setCursor(ox + 5, oy + 47); g->print(fluidNCVer);

        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 115, oy + 5);  g->print("IP ADDRESS");
        g->setTextColor(COLOR_CYAN);
        g->setCursor(ox + 115, oy + 17); g->print(ip.length() ? ip : "---");

        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 115, oy + 35);  g->print("WIFI SSID");
        g->setTextColor(COLOR_CYAN);
        g->setCursor(ox + 115, oy + 47); g->print(ssid.length() ? ssid : "---");

        endPanelSprite(230, 60, 5, 40);
    }

    // ── Panel 2: Resources (sprite pushed at 5, 186) ────────────────────────
    {
        int ox, oy;
        LovyanGFX* g = beginPanelSprite(230, 70, ox, oy, 5, 186);
        g->fillRect(ox, oy, 230, 70, COLOR_BACKGROUND);        // black corners
        g->fillRoundRect(ox, oy, 230, 70, 5, COLOR_DARKER_BG); // rounded panel

        g->setTextColor(COLOR_GRAY_TEXT); g->setTextSize(1);
        g->setCursor(ox + 5, oy + 5); g->print("FREE HEAP");
        g->setTextColor(COLOR_ORANGE); g->setTextSize(2);
        g->setCursor(ox + 5, oy + 20);
        g->print(ESP.getFreeHeap() / 1024);
        g->print(" KB");

        g->setTextColor(COLOR_GRAY_TEXT); g->setTextSize(1);
        g->setCursor(ox + 115, oy + 5); g->print("STATUS");
        g->setTextColor(connected ? COLOR_GREEN : COLOR_RED);
        g->setCursor(ox + 115, oy + 20); g->print(connStatus);

        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 5, oy + 48); g->print("ROTATION");
        g->setTextColor(COLOR_CYAN);
        g->setCursor(ox + 5, oy + 58); g->print(dispRotation);

        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 115, oy + 48); g->print("Jog Dial");
        g->setTextColor(COLOR_CYAN);
        g->setCursor(ox + 115, oy + 58); g->print("Rotate");

        endPanelSprite(230, 70, 5, 186);
    }
}

void drawFluidNCScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("FLUIDNC");

    // Connection panel — static content, drawn once directly.
    // The entire panel is a touch target → PSCREEN_WIFI_SETUP, which is the
    // transport-config / WiFi-status screen.  Tappable on both UART and WiFi
    // pendants because the setup screen is also where the transport override
    // lives (in case autodetect picked the wrong mode for the hardware).
    display.fillRoundRect(5, 108, 230, 70, 5, COLOR_DARKER_BG);
    display.drawRoundRect(5, 108, 230, 70, 5, COLOR_CYAN);  // tappable hint

#ifdef USE_WIFI
    // Affordance text in the top-right reflects what the user gets on tap:
    // "WiFi >" when WiFi is live, "Setup >" when UART is live.
    bool wifiMode = (comms_active_mode() == COMMS_MODE_WIFI);
    const char* hint = wifiMode ? "WiFi >" : "Setup >";
    display.setTextColor(COLOR_CYAN); display.setTextSize(1);
    display.setCursor(240 - 5 - display.textWidth(hint), 113);
    display.print(hint);
#endif

    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(10, 113); display.print("CONNECTION");

    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
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
    } else if (isTouchInBounds(x, y, 5, 108, 230, 70)) {
        // Entire CONNECTION panel → WiFi Setup
        currentPendantScreen = PSCREEN_WIFI_SETUP;
    }
}
