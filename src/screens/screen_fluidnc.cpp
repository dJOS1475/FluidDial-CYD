#include "pendant_shared.h"
#include "screen_fluidnc.h"
#include <Preferences.h>

extern Preferences preferences;

void enterFluidNC() {
    // No sprites for FluidNC screen
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;
}

void exitFluidNC() {
    // Nothing to free
}

void drawFluidNCScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("FLUIDNC");

    // Version and Network Info
    display.fillRoundRect(5, 40, 230, 60, 5, COLOR_DARKER_BG);

    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(10, 45); display.print("FLUIDDIAL");
    display.setTextColor(COLOR_GREEN); display.setTextSize(1);
    display.setCursor(10, 57); display.print(pendantMachine.fluidDialVersion);

    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(10, 75); display.print("FLUIDNC");
    display.setTextColor(COLOR_GREEN); display.setTextSize(1);
    display.setCursor(10, 87); display.print(pendantMachine.fluidNCVersion);

    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(120, 45); display.print("IP ADDRESS");
    display.setTextColor(COLOR_CYAN); display.setTextSize(1);
    display.setCursor(120, 57); display.print(pendantMachine.ipAddress);

    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(120, 75); display.print("WIFI SSID");
    display.setTextColor(COLOR_CYAN); display.setTextSize(1);
    display.setCursor(120, 87); display.print(pendantMachine.wifiSSID);

    // Connection Info
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

    // ESP32 Resources
    display.fillRoundRect(5, 186, 230, 70, 5, COLOR_DARKER_BG);
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(10, 191); display.print("FREE HEAP");
    display.setTextColor(COLOR_ORANGE); display.setTextSize(2);
    display.setCursor(10, 208);
    display.print(ESP.getFreeHeap() / 1024);
    display.print(" KB");

    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(120, 191); display.print("STATUS");
    display.setTextColor(COLOR_GREEN); display.setTextSize(1);
    display.setCursor(120, 208); display.print(pendantMachine.connectionStatus);

    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(10, 227); display.print("ROTATION");
    display.setTextColor(COLOR_CYAN); display.setTextSize(1);
    display.setCursor(10, 239); display.print(pendantMachine.displayRotation);

    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(120, 227); display.print("Jog Dial");
    display.setTextColor(COLOR_CYAN); display.setTextSize(1);
    display.setCursor(120, 239); display.print("Rotate");

    drawButton(5, 264, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void handleFluidNCTouch(int x, int y) {
    if (isTouchInBounds(x, y, 5, 264, 230, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    }
}
