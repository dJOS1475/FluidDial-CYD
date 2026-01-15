/*
 * CNC Pendant UI for ESP32 with ST7789 320x240 Display
 * Using LovyanGFX Graphics Library
 * Integrated with FluidDial-CYD project
 *
 * Hardware:
 * - ESP32
 * - JC2432W328C CYD LCD ST7789 (320x240)
 * - 3 Physical Buttons: Red (E-Stop), Yellow (Pause), Green (Cycle Start)
 * - Physical Jog Dial (for jogging in Jog & Homing screen, rotate display in FluidNC screen)
 */

#include "cnc_pendant_config.h"
#include <Preferences.h>

// Include from existing FluidDial project
#include "System.h"
#include "Scene.h"

// Preferences object for persistent storage
Preferences preferences;

// External references from Hardware2432.cpp
extern LGFX_Device& display;
extern LGFX_Sprite canvas;
extern m5::Touch_Class& touch;
extern int red_button_pin;
extern int dial_button_pin;
extern int green_button_pin;

// ===== Screen States =====
enum PendantScreen {
  PSCREEN_MAIN_MENU,
  PSCREEN_STATUS,
  PSCREEN_JOG_HOMING,
  PSCREEN_PROBING_WORK,
  PSCREEN_PROBING,
  PSCREEN_FEEDS_SPEEDS,
  PSCREEN_SPINDLE_CONTROL,
  PSCREEN_MACROS,
  PSCREEN_SD_CARD,
  PSCREEN_FLUIDNC
};

PendantScreen currentPendantScreen = PSCREEN_MAIN_MENU;
PendantScreen previousPendantScreen = PSCREEN_MAIN_MENU;

// ===== Sprite Buffers for Optimization =====
// These sprites hold frequently updated UI elements to avoid full screen redraws
LGFX_Sprite spriteAxisDisplay(&display);    // For Jog screen axis position display
LGFX_Sprite spriteValueDisplay(&display);   // For small value updates (RPM, feed rate, etc.)
LGFX_Sprite spriteStatusBar(&display);      // For status indicators
LGFX_Sprite spriteFileDisplay(&display);    // For current file display on Status screen

// Track which screen sprites are allocated for
PendantScreen spritesAllocatedFor = PSCREEN_MAIN_MENU;
bool spritesInitialized = false;

// ===== State Variables =====
struct MachineState {
  String status = "IDLE";
  String currentFile = "No file loaded";
  float posX = 0.0;
  float posY = 0.0;
  float posZ = 0.0;
  float posA = 0.0;
  float workX = 0.0;
  float workY = 0.0;
  float workZ = 0.0;
  float workA = 0.0;
  int feedRate = 1500;
  int spindleRPM = 12000;
  String spindleDir = "Fwd";
  bool spindleRunning = false;
  int feedOverride = 100;
  int spindleOverride = 100;
  String fluidDialVersion = "v3.7.17";
  String fluidNCVersion = "v3.7.16";
  String baudRate = "115200";
  String port = "/dev/ttyUSB0";
  String connectionStatus = "Connected";
  int freeHeap = 187;
  String workCoordSystem = "G54";
  String ipAddress = "192.168.1.100";
  String wifiSSID = "MyNetwork";
  String displayRotation = "Normal";
  int rotation = 2; // 2 = normal, 0 = upside down
} pendantMachine;

struct JogState {
  int selectedAxis = 0;  // 0=X, 1=Y, 2=Z, 3=A
  float increment = 1.0; // 0.1, 1, 10, 100
  int selectedIncrement = 1; // 0=0.1, 1=1, 2=10, 3=100
} pendantJog;

struct SDCardState {
  int selectedFile = 0;
  int scrollOffset = 0;
  String files[20] = {"project1.gcode", "test_cut.nc", "enclosure.gcode"};
  int fileCount = 3;
} pendantSdCard;

struct SpindleState {
  int selectedPreset = 2; // 0=1200, 1=6000, 2=12000, 3=16000, 4=20000, 5=24000
  bool directionFwd = true;
} pendantSpindle;

struct FeedsState {
  int selectedFeedOverride = 2;  // 0=50%, 1=75%, 2=100%, 3=125%, 4=150%
  int selectedSpindleOverride = 2;
} pendantFeeds;

struct ProbingState {
  String selectedCoordSystem = "G54"; // G54, G55, G56, G57
  int selectedCoordIndex = 0;
} pendantProbing;

struct ProbeState {
  int selectedProbeType = -1; // -1=none, 0=Z Surface, 1=Tool Height
  float feedRate = 100.0;
  float maxTravel = 25.0;
  float toolDia = 6.0;
  String status = "Ready";
  float lastZ = -15.234;
} pendantProbe;

int displayRotation = 2; // 0, 1, 2, 3 for 0°, 90°, 180°, 270° - default is 180°

// ===== Helper Functions =====
void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
  display.fillRoundRect(x, y, w, h, r, color);
}

void drawButton(int x, int y, int w, int h, String text, uint16_t bgColor, uint16_t textColor, int textSize = 2) {
  drawRoundRect(x, y, w, h, 8, bgColor);
  display.setTextColor(textColor);
  display.setTextSize(textSize);
  int16_t textW = display.textWidth(text.c_str());
  int16_t textH = display.fontHeight();
  display.setCursor(x + (w - textW) / 2, y + (h - textH) / 2);
  display.print(text);
}

// Draw a multi-line button with wrapped text
void drawMultiLineButton(int x, int y, int w, int h, String line1, String line2, uint16_t bgColor, uint16_t textColor, int textSize = 1) {
  drawRoundRect(x, y, w, h, 8, bgColor);
  display.setTextColor(textColor);
  display.setTextSize(textSize);

  int16_t fontH = display.fontHeight();
  int16_t totalTextHeight = fontH * 2 + 4; // Two lines with 4px spacing
  int16_t startY = y + (h - totalTextHeight) / 2;

  // First line
  int16_t textW1 = display.textWidth(line1.c_str());
  display.setCursor(x + (w - textW1) / 2, startY);
  display.print(line1);

  // Second line
  int16_t textW2 = display.textWidth(line2.c_str());
  display.setCursor(x + (w - textW2) / 2, startY + fontH + 4);
  display.print(line2);
}

void drawTitle(String title) {
  display.fillRect(0, 0, 240, 35, COLOR_DARKER_BG);  // Darker title bar
  display.setTextColor(COLOR_TITLE);
  display.setTextSize(2);
  int16_t textW = display.textWidth(title.c_str());
  display.setCursor((240 - textW) / 2, 10);
  display.print(title);
}

void drawInfoBox(int x, int y, int w, int h, String label, String value, uint16_t valueColor = COLOR_ORANGE) {
  display.fillRoundRect(x, y, w, h, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(x + 5, y + 5);
  display.print(label);

  display.setTextColor(valueColor);
  display.setTextSize(2);
  display.setCursor(x + 5, y + 20);
  display.print(value);
}

// ===== Sprite Management Functions =====

// Initialize sprites for a specific screen
void initSpritesForScreen(PendantScreen screen) {
  // Free existing sprites if they exist
  if (spritesInitialized) {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritesInitialized = false;
  }

  // Check available heap before allocating sprites
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 50000) { // Less than 50KB free
    dbg_printf("Warning: Low heap memory (%d bytes), skipping sprite allocation\n", freeHeap);
    return;
  }

  // Allocate sprites based on screen needs
  switch (screen) {
    case PSCREEN_JOG_HOMING:
      // Large sprite for axis position display (230x55)
      spriteAxisDisplay.createSprite(230, 55);
      spriteAxisDisplay.setColorDepth(16);

      // Small sprite for increment indicator updates
      spriteValueDisplay.createSprite(230, 40);
      spriteValueDisplay.setColorDepth(16);
      spritesInitialized = true;
      break;

    case PSCREEN_PROBING_WORK:
      // Sprite for machine position display (230x45)
      spriteAxisDisplay.createSprite(230, 45);
      spriteAxisDisplay.setColorDepth(16);

      // Sprite for work position display (230x45)
      spriteValueDisplay.createSprite(230, 45);
      spriteValueDisplay.setColorDepth(16);
      spritesInitialized = true;
      break;

    case PSCREEN_MAIN_MENU:
      // Sprite for centered status display only
      spriteStatusBar.createSprite(230, 65);
      spriteStatusBar.setColorDepth(16);
      spritesInitialized = true;
      break;

    case PSCREEN_STATUS:
      // Allocate sprites for all dynamic Status screen elements to prevent flickering
      // spriteStatusBar: Machine status (230x50 at position 5, 40)
      spriteStatusBar.createSprite(230, 50);
      if (spriteStatusBar.getBuffer()) {
        spriteStatusBar.setColorDepth(16);
      } else {
        dbg_printf("Warning: Failed to allocate spriteStatusBar for Status screen\n");
        spriteStatusBar.deleteSprite();
      }

      // spriteAxisDisplay: Axis positions (230x65 at position 5, 140)
      spriteAxisDisplay.createSprite(230, 65);
      if (spriteAxisDisplay.getBuffer()) {
        spriteAxisDisplay.setColorDepth(16);
      } else {
        dbg_printf("Warning: Failed to allocate spriteAxisDisplay for Status screen\n");
        spriteAxisDisplay.deleteSprite();
      }

      // spriteValueDisplay: Feed/Spindle info (230x65 at position 5, 210)
      spriteValueDisplay.createSprite(230, 65);
      if (spriteValueDisplay.getBuffer()) {
        spriteValueDisplay.setColorDepth(16);
      } else {
        dbg_printf("Warning: Failed to allocate spriteValueDisplay for Status screen\n");
        spriteValueDisplay.deleteSprite();
      }

      // spriteFileDisplay: Current file display (230x40 at position 5, 95)
      spriteFileDisplay.createSprite(230, 40);
      if (spriteFileDisplay.getBuffer()) {
        spriteFileDisplay.setColorDepth(16);
      } else {
        dbg_printf("Warning: Failed to allocate spriteFileDisplay for Status screen\n");
        spriteFileDisplay.deleteSprite();
      }

      spritesInitialized = true;
      break;

    case PSCREEN_SPINDLE_CONTROL:
      // Sprite for RPM display
      spriteValueDisplay.createSprite(230, 60);
      spriteValueDisplay.setColorDepth(16);
      spritesInitialized = true;
      break;

    case PSCREEN_FEEDS_SPEEDS:
      // Sprite for current feed/spindle values at top
      spriteStatusBar.createSprite(230, 35);
      spriteStatusBar.setColorDepth(16);

      // Sprite for feed override readout (middle area)
      spriteAxisDisplay.createSprite(72, 37);
      spriteAxisDisplay.setColorDepth(16);

      // Sprite for spindle override readout (bottom area)
      spriteValueDisplay.createSprite(72, 37);
      spriteValueDisplay.setColorDepth(16);
      spritesInitialized = true;
      break;

    case PSCREEN_PROBING:
      // Sprite for current position display
      spriteAxisDisplay.createSprite(230, 50);
      spriteAxisDisplay.setColorDepth(16);

      // Sprite for probe settings display
      spriteValueDisplay.createSprite(230, 40);
      spriteValueDisplay.setColorDepth(16);
      spritesInitialized = true;
      break;

    default:
      // Other screens don't need sprites yet
      break;
  }

  spritesAllocatedFor = screen;

  if (spritesInitialized) {
    dbg_printf("Sprites allocated successfully. Free heap after: %d bytes\n", ESP.getFreeHeap());
  }
}

// Update axis position display on Jog screen using sprite
void updateJogAxisDisplay() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_JOG_HOMING) return;

  // Draw to sprite instead of screen
  spriteAxisDisplay.fillSprite(COLOR_DARKER_BG);

  // Current axis indicator on left side
  String axisNames[] = {"X", "Y", "Z", "A"};
  spriteAxisDisplay.setTextColor(COLOR_GREEN);
  spriteAxisDisplay.setTextSize(3);
  spriteAxisDisplay.setCursor(5, 7);
  spriteAxisDisplay.print(axisNames[pendantJog.selectedAxis]);

  // Current position for selected axis
  float positions[] = {pendantMachine.posX, pendantMachine.posY, pendantMachine.posZ, pendantMachine.posA};
  spriteAxisDisplay.setTextSize(3);
  spriteAxisDisplay.setCursor(50, 7);

  // Get position text to calculate width
  char posBuffer[16];
  dtostrf(positions[pendantJog.selectedAxis], 1, 2, posBuffer);
  spriteAxisDisplay.print(posBuffer);

  // Calculate width of position text
  int16_t posTextWidth = spriteAxisDisplay.textWidth(posBuffer);

  // Measurement unit below selected axis position, right-aligned to position
  spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteAxisDisplay.setTextSize(2);
  int16_t unitTextWidth = spriteAxisDisplay.textWidth("mm");
  spriteAxisDisplay.setCursor(50 + posTextWidth - unitTextWidth, 33);
  spriteAxisDisplay.print("mm");

  // All axis positions in 2x2 grid on right side
  spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteAxisDisplay.setTextSize(1);
  // X (top left)
  spriteAxisDisplay.setCursor(150, 3);
  spriteAxisDisplay.print("X:");
  spriteAxisDisplay.print(pendantMachine.posX, 1);
  // Y (top right)
  spriteAxisDisplay.setCursor(150, 15);
  spriteAxisDisplay.print("Y:");
  spriteAxisDisplay.print(pendantMachine.posY, 1);
  // Z (bottom left)
  spriteAxisDisplay.setCursor(150, 27);
  spriteAxisDisplay.print("Z:");
  spriteAxisDisplay.print(pendantMachine.posZ, 1);
  // A (bottom right)
  spriteAxisDisplay.setCursor(150, 39);
  spriteAxisDisplay.print("A:");
  spriteAxisDisplay.print(pendantMachine.posA, 1);

  // Push sprite to display at position (5, 40)
  spriteAxisDisplay.pushSprite(5, 40);
}

// Update machine position display on Work Area screen using sprite
void updateWorkMachinePos() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_PROBING_WORK) return;

  // Draw to sprite instead of screen
  spriteAxisDisplay.fillSprite(COLOR_BACKGROUND);

  spriteAxisDisplay.setTextColor(COLOR_ORANGE);
  spriteAxisDisplay.setTextSize(2);
  spriteAxisDisplay.setCursor(0, 5);
  spriteAxisDisplay.print("X:");
  spriteAxisDisplay.print(pendantMachine.posX, 1);
  spriteAxisDisplay.setCursor(120, 5);
  spriteAxisDisplay.print("Y:");
  spriteAxisDisplay.print(pendantMachine.posY, 1);
  spriteAxisDisplay.setCursor(0, 25);
  spriteAxisDisplay.print("Z:");
  spriteAxisDisplay.print(pendantMachine.posZ, 1);
  spriteAxisDisplay.setCursor(120, 25);
  spriteAxisDisplay.print("A:");
  spriteAxisDisplay.print(pendantMachine.posA, 1);

  // Push sprite to display at position (5, 108)
  spriteAxisDisplay.pushSprite(5, 108);
}

// Update work position display on Work Area screen using sprite
void updateWorkAreaPos() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_PROBING_WORK) return;

  // Draw to sprite instead of screen
  spriteValueDisplay.fillSprite(COLOR_BACKGROUND);

  spriteValueDisplay.setTextColor(COLOR_CYAN);
  spriteValueDisplay.setTextSize(2);
  spriteValueDisplay.setCursor(0, 5);
  spriteValueDisplay.print("X:");
  spriteValueDisplay.print(pendantMachine.workX, 1);
  spriteValueDisplay.setCursor(120, 5);
  spriteValueDisplay.print("Y:");
  spriteValueDisplay.print(pendantMachine.workY, 1);
  spriteValueDisplay.setCursor(0, 25);
  spriteValueDisplay.print("Z:");
  spriteValueDisplay.print(pendantMachine.workZ, 1);
  spriteValueDisplay.setCursor(120, 25);
  spriteValueDisplay.print("A:");
  spriteValueDisplay.print(pendantMachine.workA, 1);

  // Push sprite to display at position (5, 166)
  spriteValueDisplay.pushSprite(5, 166);
}

// Redraw only axis selection buttons (no full screen redraw)
void redrawJogAxisButtons() {
  if (currentPendantScreen != PSCREEN_JOG_HOMING) return;

  String axisNames[] = {"X", "Y", "Z", "A"};
  for (int i = 0; i < 4; i++) {
    uint16_t bgColor = (i == pendantJog.selectedAxis) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 56, 115, 52, 38, axisNames[i], bgColor, COLOR_WHITE, 3);
  }

  // Also update the axis display sprite to show the newly selected axis
  updateJogAxisDisplay();
}

// Redraw only increment selection buttons (no full screen redraw)
void redrawJogIncrementButtons() {
  if (currentPendantScreen != PSCREEN_JOG_HOMING) return;

  String increments[] = {"0.1", "1", "10", "100"};
  for (int i = 0; i < 4; i++) {
    uint16_t bgColor = (i == pendantJog.selectedIncrement) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 56, 231, 52, 38, increments[i], bgColor, COLOR_WHITE, 2);
  }
}

// Redraw only coordinate system selection buttons (no full screen redraw)
void redrawWorkCoordButtons() {
  if (currentPendantScreen != PSCREEN_PROBING_WORK) return;

  String coordSystems[] = {"G54", "G55", "G56", "G57"};
  for (int i = 0; i < 4; i++) {
    uint16_t bgColor = (i == pendantProbing.selectedCoordIndex) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 56, 55, 52, 38, coordSystems[i], bgColor, COLOR_WHITE, 2);
  }
}

// Update main menu status display using sprite (centered)
void updateMainMenuDisplay() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_MAIN_MENU) return;

  // Draw to sprite instead of screen
  spriteStatusBar.fillSprite(COLOR_DARKER_BG);

  // STATUS label centered
  spriteStatusBar.setTextColor(COLOR_GRAY_TEXT);
  spriteStatusBar.setTextSize(1);
  int16_t labelWidth = spriteStatusBar.textWidth("STATUS");
  spriteStatusBar.setCursor(115 - labelWidth/2, 8);
  spriteStatusBar.print("STATUS");

  // Status value centered below label
  spriteStatusBar.setTextColor(COLOR_CYAN);
  spriteStatusBar.setTextSize(4);
  int16_t statusWidth = spriteStatusBar.textWidth(pendantMachine.status.c_str());
  spriteStatusBar.setCursor(115 - statusWidth/2, 26);
  spriteStatusBar.print(pendantMachine.status);

  // Push sprite to display at position (5, 40)
  spriteStatusBar.pushSprite(5, 40);
}

// Update current feed/spindle values at top of Feeds & Speeds screen using sprite
void updateFeedsSpeedsTopDisplay() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;

  // Draw to sprite instead of screen (sprite covers full 230x35 area with both boxes)
  spriteStatusBar.fillSprite(COLOR_BACKGROUND);

  // Left box - Feed rate (0, 0, 112, 35) in sprite coordinates
  spriteStatusBar.fillRoundRect(0, 0, 112, 35, 5, COLOR_DARKER_BG);
  spriteStatusBar.setTextColor(COLOR_GRAY_TEXT);
  spriteStatusBar.setTextSize(1);
  spriteStatusBar.setCursor(5, 3);
  spriteStatusBar.print("FEED");
  spriteStatusBar.setTextColor(COLOR_ORANGE);
  spriteStatusBar.setTextSize(2);
  spriteStatusBar.setCursor(5, 13);
  spriteStatusBar.print(pendantMachine.feedRate);
  // Unit inline
  spriteStatusBar.setTextColor(COLOR_GRAY_TEXT);
  spriteStatusBar.setTextSize(1);
  spriteStatusBar.setCursor(5 + spriteStatusBar.textWidth(String(pendantMachine.feedRate).c_str()) * 2 + 4, 17);
  spriteStatusBar.print("mm/min");

  // Right box - Spindle RPM (118, 0, 112, 35) in sprite coordinates (gap of 6px)
  spriteStatusBar.fillRoundRect(118, 0, 112, 35, 5, COLOR_DARKER_BG);
  spriteStatusBar.setTextColor(COLOR_GRAY_TEXT);
  spriteStatusBar.setTextSize(1);
  spriteStatusBar.setCursor(123, 3);
  spriteStatusBar.print("SPINDLE");
  spriteStatusBar.setTextColor(COLOR_GREEN);
  spriteStatusBar.setTextSize(2);
  spriteStatusBar.setCursor(123, 13);
  spriteStatusBar.print(pendantMachine.spindleRPM);
  // Unit inline
  spriteStatusBar.setTextColor(COLOR_GRAY_TEXT);
  spriteStatusBar.setTextSize(1);
  spriteStatusBar.setCursor(123 + spriteStatusBar.textWidth(String(pendantMachine.spindleRPM).c_str()) * 2 + 4, 17);
  spriteStatusBar.print("RPM");

  // Push sprite to display at position (5, 40)
  spriteStatusBar.pushSprite(5, 40);
}

// Update feed override readout using sprite
void updateFeedOverrideDisplay() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;

  // Draw to sprite instead of screen
  spriteAxisDisplay.fillSprite(COLOR_DARKER_BG);

  spriteAxisDisplay.setTextColor(COLOR_ORANGE);
  spriteAxisDisplay.setTextSize(2);
  int16_t textW = spriteAxisDisplay.textWidth((String(pendantMachine.feedOverride) + "%").c_str());
  spriteAxisDisplay.setCursor(36 - textW/2, 11);
  spriteAxisDisplay.print(pendantMachine.feedOverride);
  spriteAxisDisplay.print("%");

  // Push sprite to display at position (83, 137)
  spriteAxisDisplay.pushSprite(83, 137);
}

// Update spindle override readout using sprite
void updateSpindleOverrideDisplay() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;

  // Draw to sprite instead of screen
  spriteValueDisplay.fillSprite(COLOR_DARKER_BG);

  spriteValueDisplay.setTextColor(COLOR_GREEN);
  spriteValueDisplay.setTextSize(2);
  int16_t textW = spriteValueDisplay.textWidth((String(pendantMachine.spindleOverride) + "%").c_str());
  spriteValueDisplay.setCursor(36 - textW/2, 11);
  spriteValueDisplay.print(pendantMachine.spindleOverride);
  spriteValueDisplay.print("%");

  // Push sprite to display at position (83, 236)
  spriteValueDisplay.pushSprite(83, 236);
}

// Redraw only feed override buttons (no full screen redraw)
void redrawFeedOverrideButtons() {
  if (currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;

  String percentages[] = {"50%", "75%", "100%", "125%", "150%"};

  // Row 1 - 3 buttons
  for (int i = 0; i < 3; i++) {
    uint16_t bgColor = (i == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    int x = 5 + i * 78;
    drawButton(x, 95, 72, 37, percentages[i], bgColor, COLOR_WHITE, 2);
  }

  // Row 2 - 2 buttons
  uint16_t bgColor3 = (3 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
  drawButton(5, 137, 72, 37, percentages[3], bgColor3, COLOR_WHITE, 2);

  uint16_t bgColor4 = (4 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
  drawButton(161, 137, 72, 37, percentages[4], bgColor4, COLOR_WHITE, 2);

  // Also update the feed override readout
  updateFeedOverrideDisplay();
}

// Redraw only spindle override buttons (no full screen redraw)
void redrawSpindleOverrideButtons() {
  if (currentPendantScreen != PSCREEN_FEEDS_SPEEDS) return;

  String percentages[] = {"50%", "75%", "100%", "125%", "150%"};

  // Row 1 - 3 buttons
  for (int i = 0; i < 3; i++) {
    uint16_t bgColor = (i == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    int x = 5 + i * 78;
    drawButton(x, 194, 72, 37, percentages[i], bgColor, COLOR_WHITE, 2);
  }

  // Row 2 - 2 buttons
  uint16_t bgColor3s = (3 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
  drawButton(5, 236, 72, 37, percentages[3], bgColor3s, COLOR_WHITE, 2);

  uint16_t bgColor4s = (4 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
  drawButton(161, 236, 72, 37, percentages[4], bgColor4s, COLOR_WHITE, 2);

  // Also update the spindle override readout
  updateSpindleOverrideDisplay();
}

// Update RPM display on Spindle Control screen using sprite
void updateSpindleRPMDisplay() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_SPINDLE_CONTROL) return;

  // Draw to sprite instead of screen
  spriteValueDisplay.fillSprite(COLOR_DARKER_BG);

  spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteValueDisplay.setTextSize(1);
  spriteValueDisplay.setCursor(5, 5);
  spriteValueDisplay.print("RPM");

  spriteValueDisplay.setTextColor(COLOR_ORANGE);
  spriteValueDisplay.setTextSize(4);
  spriteValueDisplay.setCursor(5, 20);
  spriteValueDisplay.print(pendantMachine.spindleRPM);

  spriteValueDisplay.setTextColor(COLOR_CYAN);
  spriteValueDisplay.setTextSize(2);
  spriteValueDisplay.setCursor(155, 30);
  spriteValueDisplay.print(pendantMachine.spindleDir);

  // Push sprite to display at position (5, 40)
  spriteValueDisplay.pushSprite(5, 40);
}

// Redraw only direction buttons (no full screen redraw)
void redrawSpindleDirectionButtons() {
  if (currentPendantScreen != PSCREEN_SPINDLE_CONTROL) return;

  drawButton(5, 118, 112, 38, "Fwd", pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  drawButton(123, 118, 112, 38, "Rev", !pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

  // Also update the RPM display to reflect direction change
  updateSpindleRPMDisplay();
}

// Redraw only RPM preset buttons (no full screen redraw)
void redrawSpindlePresetButtons() {
  if (currentPendantScreen != PSCREEN_SPINDLE_CONTROL) return;

  int presets[] = {6000, 12000, 24000};
  String presetLabels[] = {"6000", "12000", "24000"};

  for (int i = 0; i < 3; i++) {
    uint16_t bgColor = (i == pendantSpindle.selectedPreset) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    int x = 5 + i * 75;
    int y = 178;
    drawButton(x, y, 70, 37, presetLabels[i], bgColor, COLOR_WHITE, 2);
  }

  // Also update the RPM display to show new value
  updateSpindleRPMDisplay();
}

// Update current position display on Probe screen using sprite
void updateProbePositionDisplay() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_PROBING) return;

  // Draw to sprite instead of screen
  spriteAxisDisplay.fillSprite(COLOR_BACKGROUND);

  spriteAxisDisplay.setTextColor(COLOR_ORANGE);
  spriteAxisDisplay.setTextSize(2);
  spriteAxisDisplay.setCursor(0, 7);
  spriteAxisDisplay.print("X ");
  spriteAxisDisplay.print(pendantMachine.posX, 1);
  spriteAxisDisplay.setCursor(85, 7);
  spriteAxisDisplay.print("Y ");
  spriteAxisDisplay.print(pendantMachine.posY, 1);
  spriteAxisDisplay.setCursor(0, 27);
  spriteAxisDisplay.print("Z ");
  spriteAxisDisplay.print(pendantMachine.posZ, 1);
  spriteAxisDisplay.setCursor(85, 27);
  spriteAxisDisplay.print("A ");
  spriteAxisDisplay.print(pendantMachine.posA, 1);

  // Push sprite to display at position (5, 57)
  spriteAxisDisplay.pushSprite(5, 57);
}

// Update probe settings display using sprite
void updateProbeSettingsDisplay() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_PROBING) return;

  // Draw to sprite instead of screen
  spriteValueDisplay.fillSprite(COLOR_BACKGROUND);

  spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteValueDisplay.setTextSize(1);
  spriteValueDisplay.setCursor(0, 4);
  spriteValueDisplay.print("Feed Rate:");
  spriteValueDisplay.setTextColor(COLOR_ORANGE);
  spriteValueDisplay.setCursor(165, 4);
  spriteValueDisplay.print(pendantProbe.feedRate, 0);
  spriteValueDisplay.print(" mm/min");

  spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteValueDisplay.setCursor(0, 19);
  spriteValueDisplay.print("Max Travel:");
  spriteValueDisplay.setTextColor(COLOR_ORANGE);
  spriteValueDisplay.setCursor(165, 19);
  spriteValueDisplay.print(pendantProbe.maxTravel, 1);
  spriteValueDisplay.print(" mm");

  // Push sprite to display at position (5, 228)
  spriteValueDisplay.pushSprite(5, 228);
}

// Update current file display on Status screen using sprite
void updateStatusCurrentFile() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_STATUS) return;

  // Draw to sprite instead of screen to prevent flickering
  spriteFileDisplay.fillRoundRect(0, 0, 230, 40, 5, COLOR_DARKER_BG);

  spriteFileDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteFileDisplay.setTextSize(1);
  spriteFileDisplay.setCursor(5, 5);
  spriteFileDisplay.print("CURRENT FILE");

  spriteFileDisplay.setTextColor(COLOR_CYAN);
  spriteFileDisplay.setTextSize(1);
  spriteFileDisplay.setCursor(5, 20);
  spriteFileDisplay.print(pendantMachine.currentFile);

  // Push sprite to display at position (5, 95)
  spriteFileDisplay.pushSprite(5, 95);
}

// Update machine status display on Status screen using sprite (centered)
void updateStatusMachineStatus() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_STATUS) return;

  // Draw to sprite instead of screen
  spriteStatusBar.fillSprite(COLOR_DARKER_BG);

  // MACHINE STATUS label centered
  spriteStatusBar.setTextColor(COLOR_GRAY_TEXT);
  spriteStatusBar.setTextSize(1);
  int16_t labelWidth = spriteStatusBar.textWidth("MACHINE STATUS");
  spriteStatusBar.setCursor(115 - labelWidth/2, 5);
  spriteStatusBar.print("MACHINE STATUS");

  // Status value centered below label
  spriteStatusBar.setTextColor(COLOR_CYAN);
  spriteStatusBar.setTextSize(3);
  int16_t statusWidth = spriteStatusBar.textWidth(pendantMachine.status.c_str());
  spriteStatusBar.setCursor(115 - statusWidth/2, 22);
  spriteStatusBar.print(pendantMachine.status);

  // Push sprite to display at position (5, 40)
  spriteStatusBar.pushSprite(5, 40);
}

// Update axis positions display on Status screen using sprite
void updateStatusAxisPositions() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_STATUS) return;

  // Draw to sprite instead of screen to prevent flickering
  spriteAxisDisplay.fillRoundRect(0, 0, 230, 65, 5, COLOR_DARKER_BG);

  spriteAxisDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteAxisDisplay.setTextSize(1);
  spriteAxisDisplay.setCursor(5, 5);
  spriteAxisDisplay.print("AXIS POSITIONS");

  spriteAxisDisplay.setTextColor(COLOR_ORANGE);
  spriteAxisDisplay.setTextSize(2);
  spriteAxisDisplay.setCursor(5, 20);
  spriteAxisDisplay.print("X:");
  spriteAxisDisplay.print(pendantMachine.posX, 1);
  spriteAxisDisplay.setCursor(125, 20);
  spriteAxisDisplay.print("Y:");
  spriteAxisDisplay.print(pendantMachine.posY, 1);
  spriteAxisDisplay.setCursor(5, 43);
  spriteAxisDisplay.print("Z:");
  spriteAxisDisplay.print(pendantMachine.posZ, 1);
  spriteAxisDisplay.setCursor(125, 43);
  spriteAxisDisplay.print("A:");
  spriteAxisDisplay.print(pendantMachine.posA, 1);

  // Push sprite to display at position (5, 140)
  spriteAxisDisplay.pushSprite(5, 140);
}

// Update feed/spindle info on Status screen using sprite
void updateStatusFeedSpindle() {
  if (!spritesInitialized || currentPendantScreen != PSCREEN_STATUS) return;

  // Draw to sprite instead of screen to prevent flickering
  // Clear sprite with background color
  spriteValueDisplay.fillSprite(COLOR_BACKGROUND);

  // Feed Rate box (left half of sprite: 0-112)
  spriteValueDisplay.fillRoundRect(0, 0, 112, 65, 5, COLOR_DARKER_BG);
  spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteValueDisplay.setTextSize(1);
  spriteValueDisplay.setCursor(5, 3);
  spriteValueDisplay.print("FEED");
  spriteValueDisplay.setTextColor(COLOR_ORANGE);
  spriteValueDisplay.setTextSize(2);
  spriteValueDisplay.setCursor(5, 25);
  spriteValueDisplay.print(pendantMachine.feedRate);
  spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteValueDisplay.setTextSize(1);
  int16_t mmMinWidth = spriteValueDisplay.textWidth("mm/min");
  spriteValueDisplay.setCursor(112 - 5 - mmMinWidth, 50);
  spriteValueDisplay.print("mm/min");

  // Spindle box (right half of sprite: 118-230)
  spriteValueDisplay.fillRoundRect(118, 0, 112, 65, 5, COLOR_DARKER_BG);
  spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteValueDisplay.setTextSize(1);
  spriteValueDisplay.setCursor(123, 3);
  spriteValueDisplay.print("SPINDLE");
  spriteValueDisplay.setTextColor(COLOR_ORANGE);
  spriteValueDisplay.setTextSize(1);
  int16_t dirWidth = spriteValueDisplay.textWidth(pendantMachine.spindleDir.c_str());
  spriteValueDisplay.setCursor(230 - 5 - dirWidth, 3);
  spriteValueDisplay.print(pendantMachine.spindleDir);

  spriteValueDisplay.setTextColor(COLOR_GREEN);
  spriteValueDisplay.setTextSize(2);
  spriteValueDisplay.setCursor(123, 25);
  spriteValueDisplay.print(pendantMachine.spindleRPM);
  spriteValueDisplay.setTextColor(COLOR_GRAY_TEXT);
  spriteValueDisplay.setTextSize(1);
  int16_t rpmWidth = spriteValueDisplay.textWidth("RPM");
  spriteValueDisplay.setCursor(230 - 5 - rpmWidth, 50);
  spriteValueDisplay.print("RPM");

  // Push sprite to display at position (5, 210)
  spriteValueDisplay.pushSprite(5, 210);
}

// ===== Screen Drawing Functions =====

void drawMainMenu() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("MAIN MENU");

  // Initialize sprites for this screen if not already done
  if (!spritesInitialized || spritesAllocatedFor != PSCREEN_MAIN_MENU) {
    initSpritesForScreen(PSCREEN_MAIN_MENU);
  }

  // Draw static background for status/axis display area (sprite will be drawn on top)
  display.fillRoundRect(5, 40, 230, 65, 5, COLOR_DARKER_BG);

  // Update status and axis display using sprite
  updateMainMenuDisplay();

  // Menu buttons - arranged in a 2x4 grid with smaller, wrapped text (adjusted positions)
  int btnY = 115;
  int btnH = 47;
  int btnGap = 52;

  // Row 1
  drawButton(5, btnY, 112, btnH, "Jog", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, btnY, 112, btnH, "Work Area", COLOR_BLUE, COLOR_WHITE, 2);

  // Row 2
  drawMultiLineButton(5, btnY + btnGap, 112, btnH, "Feeds &", "Speeds", COLOR_BLUE, COLOR_WHITE, 2);
  drawMultiLineButton(123, btnY + btnGap, 112, btnH, "Spindle", "Control", COLOR_BLUE, COLOR_WHITE, 2);

  // Row 3
  drawButton(5, btnY + btnGap*2, 112, btnH, "Macros", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, btnY + btnGap*2, 112, btnH, "SD Card", COLOR_BLUE, COLOR_WHITE, 2);

  // Row 4
  drawButton(5, btnY + btnGap*3, 112, btnH, "Probe", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, btnY + btnGap*3, 112, btnH, "Status", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawStatusScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("STATUS");

  // Initialize sprites for this screen if not already done
  if (!spritesInitialized || spritesAllocatedFor != PSCREEN_STATUS) {
    initSpritesForScreen(PSCREEN_STATUS);
  }

  // Machine Status - use sprite
  updateStatusMachineStatus();

  // Current File - use sprite
  updateStatusCurrentFile();

  // Axis Positions - use sprite
  updateStatusAxisPositions();

  // Feed Rate and Spindle - use update function
  updateStatusFeedSpindle();

  // Main Menu and FluidNC buttons - with 5px gap from panels above
  drawButton(5, 280, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, 280, 112, 40, "FluidNC", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawJogHomingScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("JOG & HOMING");

  // Initialize sprites for this screen if not already done
  if (!spritesInitialized || spritesAllocatedFor != PSCREEN_JOG_HOMING) {
    initSpritesForScreen(PSCREEN_JOG_HOMING);
  }

  // Draw static background for axis display area (sprite will be drawn on top)
  display.fillRoundRect(5, 40, 230, 55, 5, COLOR_DARKER_BG);

  // Draw axis display using sprite (this includes all position info)
  updateJogAxisDisplay();

  // Axis labels needed for buttons
  String axisNames[] = {"X", "Y", "Z", "A"};

  // Axis selection buttons
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 103);
  display.print("JOG AXIS");

  for (int i = 0; i < 4; i++) {
    uint16_t bgColor = (i == pendantJog.selectedAxis) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 56, 115, 52, 38, axisNames[i], bgColor, COLOR_WHITE, 3);
  }

  // Home buttons
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 161);
  display.print("HOME");

  for (int i = 0; i < 4; i++) {
    drawButton(5 + i * 56, 173, 52, 38, axisNames[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
  }

  // Increment selection
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 219);
  display.print("JOG INCREMENT");

  String increments[] = {"0.1", "1", "10", "100"};
  for (int i = 0; i < 4; i++) {
    uint16_t bgColor = (i == pendantJog.selectedIncrement) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 56, 231, 52, 38, increments[i], bgColor, COLOR_WHITE, 2);
  }

  // Bottom navigation buttons
  drawButton(5, 277, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, 277, 112, 40, "Work Area", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawProbingWorkScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("WORK AREA");

  // Initialize sprites for this screen if not already done
  if (!spritesInitialized || spritesAllocatedFor != PSCREEN_PROBING_WORK) {
    initSpritesForScreen(PSCREEN_PROBING_WORK);
  }

  // Work coordinate system selection
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 43);
  display.print("COORDINATE SYSTEM");

  String coordSystems[] = {"G54", "G55", "G56", "G57"};
  for (int i = 0; i < 4; i++) {
    uint16_t bgColor = (i == pendantProbing.selectedCoordIndex) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 56, 55, 52, 38, coordSystems[i], bgColor, COLOR_WHITE, 2);
  }

  // Machine Position label
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 100);
  display.print("MACHINE POS");

  // Update machine position using sprite (230x45 at position 5, 108)
  updateWorkMachinePos();

  // Work Position label
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 158);
  display.print("WORK POS");

  // Update work position using sprite (230x45 at position 5, 166)
  updateWorkAreaPos();

  // Set Work Zero buttons
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 218);
  display.print("SET WORK ZERO");

  drawButton(5, 230, 46, 38, "X", COLOR_DARK_GREEN, COLOR_WHITE, 3);
  drawButton(52, 230, 46, 38, "Y", COLOR_DARK_GREEN, COLOR_WHITE, 3);
  drawButton(99, 230, 46, 38, "Z", COLOR_DARK_GREEN, COLOR_WHITE, 3);
  drawButton(146, 230, 46, 38, "A", COLOR_DARK_GREEN, COLOR_WHITE, 3);
  drawButton(193, 230, 46, 38, "ALL", COLOR_DARK_GREEN, COLOR_WHITE, 2);

  // Bottom navigation
  drawButton(5, 277, 112, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, 277, 112, 40, "Jog", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawProbingScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("PROBE");

  // Initialize sprites for this screen if not already done
  if (!spritesInitialized || spritesAllocatedFor != PSCREEN_PROBING) {
    initSpritesForScreen(PSCREEN_PROBING);
  }

  // Current Position label
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 43);
  display.print("CURRENT POSITION");

  // Update current position using sprite
  updateProbePositionDisplay();

  // Probe Type
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 104);
  display.print("PROBE TYPE");

  String probeTypes[] = {"Z Surface", "Tool Height"};
  uint16_t probeColors[] = {COLOR_DARK_GREEN, COLOR_ORANGE};

  // 2 buttons stacked vertically - colored buttons
  for (int i = 0; i < 2; i++) {
    int y = 116 + i * 43;
    drawButton(5, y, 230, 38, probeTypes[i], probeColors[i], COLOR_WHITE, 2);
  }

  // Probe Settings label
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 214);
  display.print("PROBE SETTINGS");

  // Update probe settings using sprite
  updateProbeSettingsDisplay();

  // Main Menu button
  drawButton(5, 280, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawFeedsSpeedsScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("FEEDS & SPEEDS");

  // Initialize sprites for this screen if not already done
  if (!spritesInitialized || spritesAllocatedFor != PSCREEN_FEEDS_SPEEDS) {
    initSpritesForScreen(PSCREEN_FEEDS_SPEEDS);
  }

  // Update current feed/spindle values at top
  updateFeedsSpeedsTopDisplay();

  // Feed Override section - moved up from 93 to 83
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 83);
  display.print("FEED OVERRIDE");

  // Feed Override buttons and readout
  String percentages[] = {"50%", "75%", "100%", "125%", "150%"};

  // Row 1 - 3 buttons aligned in columns - moved up from 105 to 95
  for (int i = 0; i < 3; i++) {
    uint16_t bgColor = (i == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    int x = 5 + i * 78;
    drawButton(x, 95, 72, 37, percentages[i], bgColor, COLOR_WHITE, 2);
  }

  // Row 2 - 2 buttons + readout - moved up from 147 to 137
  uint16_t bgColor3 = (3 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
  drawButton(5, 137, 72, 37, percentages[3], bgColor3, COLOR_WHITE, 2);

  // Readout inline - aligned to middle column (using sprite)
  display.fillRoundRect(83, 137, 72, 37, 5, COLOR_DARKER_BG);
  updateFeedOverrideDisplay();

  uint16_t bgColor4 = (4 == pendantFeeds.selectedFeedOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
  drawButton(161, 137, 72, 37, percentages[4], bgColor4, COLOR_WHITE, 2);

  // Spindle Override section - moved up from 192 to 182
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 182);
  display.print("SPINDLE OVERRIDE");

  // Row 1 - 3 buttons aligned in columns - moved up from 204 to 194
  for (int i = 0; i < 3; i++) {
    uint16_t bgColor = (i == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    int x = 5 + i * 78;
    drawButton(x, 194, 72, 37, percentages[i], bgColor, COLOR_WHITE, 2);
  }

  // Row 2 - 2 buttons + readout - moved up from 246 to 236
  uint16_t bgColor3s = (3 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
  drawButton(5, 236, 72, 37, percentages[3], bgColor3s, COLOR_WHITE, 2);

  // Readout inline - aligned to middle column (using sprite)
  display.fillRoundRect(83, 236, 72, 37, 5, COLOR_DARKER_BG);
  updateSpindleOverrideDisplay();

  uint16_t bgColor4s = (4 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
  drawButton(161, 236, 72, 37, percentages[4], bgColor4s, COLOR_WHITE, 2);

  // Main Menu button - moved up from 290 to 280, height from 30 to 40
  drawButton(5, 280, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawSpindleControlScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("SPINDLE CONTROL");

  // Initialize sprites for this screen if not already done
  if (!spritesInitialized || spritesAllocatedFor != PSCREEN_SPINDLE_CONTROL) {
    initSpritesForScreen(PSCREEN_SPINDLE_CONTROL);
  }

  // Draw static background for RPM display area (sprite will be drawn on top)
  display.fillRoundRect(5, 40, 230, 60, 5, COLOR_DARKER_BG);

  // Update RPM display using sprite
  updateSpindleRPMDisplay();

  // Direction Toggle
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 106);
  display.print("DIRECTION");

  drawButton(5, 118, 112, 38, "Fwd", pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  drawButton(123, 118, 112, 38, "Rev", !pendantSpindle.directionFwd ? COLOR_DARK_GREEN : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

  // RPM Presets
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 166);
  display.print("RPM PRESETS");

  int presets[] = {6000, 12000, 24000};
  String presetLabels[] = {"6000", "12000", "24000"};

  for (int i = 0; i < 3; i++) {
    uint16_t bgColor = (i == pendantSpindle.selectedPreset) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    int x = 5 + i * 75;
    int y = 178;
    drawButton(x, y, 70, 37, presetLabels[i], bgColor, COLOR_WHITE, 2);
  }

  // Start/Stop buttons
  drawButton(5, 230, 112, 40, "Start", COLOR_DARK_GREEN, COLOR_WHITE, 2);
  drawButton(123, 230, 112, 40, "Stop", COLOR_RED, COLOR_WHITE, 2);

  // Main Menu button
  drawButton(5, 280, 230, 37, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawMacrosScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("MACROS");

  // Macro buttons - 2 columns, 5 rows
  for (int i = 0; i < 10; i++) {
    int x = 5 + (i % 2) * 118;
    int y = 40 + (i / 2) * 48;
    String label = "Macro " + String(i);
    drawButton(x, y, 112, 43, label, COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  }

  // Main Menu button
  drawButton(5, 280, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawSDCardScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("SD CARD");

  // File list - increased to 5 files visible with more space
  for (int i = 0; i < 5 && i < pendantSdCard.fileCount; i++) {
    int displayIndex = i + pendantSdCard.scrollOffset;
    if (displayIndex >= pendantSdCard.fileCount) break;

    // No default selection - only highlight if user has selected
    uint16_t bgColor = COLOR_BUTTON_GRAY;
    display.fillRoundRect(5, 40 + i * 44, 230, 40, 8, bgColor);
    display.setTextColor(COLOR_WHITE);
    display.setTextSize(1);
    display.setCursor(10, 52 + i * 44);
    display.print(pendantSdCard.files[displayIndex]);
  }

  // Navigation buttons stacked above Main Menu
  drawButton(5, 240, 112, 38, "Back", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  drawButton(123, 240, 112, 38, "Next", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

  // Main Menu button
  drawButton(5, 282, 230, 38, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawFluidNCScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("FLUIDNC");

  // Version and Network Info Panel - split into two columns, increased height
  display.fillRoundRect(5, 40, 230, 60, 5, COLOR_DARKER_BG);

  // Left column - Version info
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 45);
  display.print("FLUIDDIAL");
  display.setTextColor(COLOR_GREEN);
  display.setTextSize(1);
  display.setCursor(10, 57);
  display.print(pendantMachine.fluidDialVersion);

  // Space row

  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 75);
  display.print("FLUIDNC");
  display.setTextColor(COLOR_GREEN);
  display.setTextSize(1);
  display.setCursor(10, 87);
  display.print(pendantMachine.fluidNCVersion);

  // Right column - Network info
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(120, 45);
  display.print("IP ADDRESS");
  display.setTextColor(COLOR_CYAN);
  display.setTextSize(1);
  display.setCursor(120, 57);
  display.print(pendantMachine.ipAddress);

  // Space row

  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(120, 75);
  display.print("WIFI SSID");
  display.setTextColor(COLOR_CYAN);
  display.setTextSize(1);
  display.setCursor(120, 87);
  display.print(pendantMachine.wifiSSID);

  // Connection Info
  display.fillRoundRect(5, 108, 230, 70, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 113);
  display.print("CONNECTION");

  display.setCursor(10, 130);
  display.print("Baud:");
  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(2);
  display.setCursor(100, 127);
  display.print(pendantMachine.baudRate);

  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 148);
  display.print("Port:");
  display.setTextColor(COLOR_CYAN);
  display.setTextSize(1);
  display.setCursor(10, 160);
  display.print(pendantMachine.port);

  // CYD ESP32 Resources - increased height to 70px for 3 rows
  display.fillRoundRect(5, 186, 230, 70, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 191);
  display.print("FREE HEAP");

  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(2);
  display.setCursor(10, 208);
  display.print(pendantMachine.freeHeap);
  display.print(" KB");

  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(120, 191);
  display.print("STATUS");
  display.setTextColor(COLOR_GREEN);
  display.setTextSize(1);
  display.setCursor(120, 208);
  display.print(pendantMachine.connectionStatus);

  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 227);
  display.print("ROTATION");
  display.setTextColor(COLOR_CYAN);
  display.setTextSize(1);
  display.setCursor(10, 239);
  display.print(pendantMachine.displayRotation);

  // Jog Dial touch area - invisible clickable area for rotation toggle
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(120, 227);
  display.print("Jog Dial");
  display.setTextColor(COLOR_CYAN);
  display.setTextSize(1);
  display.setCursor(120, 239);
  display.print("Rotate");

  // Main Menu button
  drawButton(5, 264, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

// ===== Main Drawing Router =====
void drawCurrentPendantScreen() {
  switch (currentPendantScreen) {
    case PSCREEN_MAIN_MENU:
      drawMainMenu();
      break;
    case PSCREEN_STATUS:
      drawStatusScreen();
      break;
    case PSCREEN_JOG_HOMING:
      drawJogHomingScreen();
      break;
    case PSCREEN_PROBING_WORK:
      drawProbingWorkScreen();
      break;
    case PSCREEN_PROBING:
      drawProbingScreen();
      break;
    case PSCREEN_FEEDS_SPEEDS:
      drawFeedsSpeedsScreen();
      break;
    case PSCREEN_SPINDLE_CONTROL:
      drawSpindleControlScreen();
      break;
    case PSCREEN_MACROS:
      drawMacrosScreen();
      break;
    case PSCREEN_SD_CARD:
      drawSDCardScreen();
      break;
    case PSCREEN_FLUIDNC:
      drawFluidNCScreen();
      break;
  }
}

// ===== Touch Handling =====
bool isTouchInBounds(int tx, int ty, int x, int y, int w, int h) {
  return (tx >= x && tx <= x + w && ty >= y && ty <= y + h);
}

void handleMainMenuTouch(int x, int y) {
  int btnY = 115;
  int btnH = 47;
  int btnGap = 52;

  if (isTouchInBounds(x, y, 5, btnY, 112, btnH)) {
    currentPendantScreen = PSCREEN_JOG_HOMING;
  } else if (isTouchInBounds(x, y, 123, btnY, 112, btnH)) {
    currentPendantScreen = PSCREEN_PROBING_WORK;
  } else if (isTouchInBounds(x, y, 5, btnY + btnGap, 112, btnH)) {
    currentPendantScreen = PSCREEN_FEEDS_SPEEDS;
  } else if (isTouchInBounds(x, y, 123, btnY + btnGap, 112, btnH)) {
    currentPendantScreen = PSCREEN_SPINDLE_CONTROL;
  } else if (isTouchInBounds(x, y, 5, btnY + btnGap*2, 112, btnH)) {
    currentPendantScreen = PSCREEN_MACROS;
  } else if (isTouchInBounds(x, y, 123, btnY + btnGap*2, 112, btnH)) {
    currentPendantScreen = PSCREEN_SD_CARD;
  } else if (isTouchInBounds(x, y, 5, btnY + btnGap*3, 112, btnH)) {
    currentPendantScreen = PSCREEN_PROBING;
  } else if (isTouchInBounds(x, y, 123, btnY + btnGap*3, 112, btnH)) {
    currentPendantScreen = PSCREEN_STATUS;
  }
}

void handleJogHomingTouch(int x, int y) {
  // Axis selection
  for (int i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, 5 + i * 56, 115, 52, 38)) {
      pendantJog.selectedAxis = i;
      // Partial redraw - only update axis buttons and display
      redrawJogAxisButtons();
      return;
    }
  }

  // Home buttons
  String axisNames[] = {"X", "Y", "Z", "A"};
  for (int i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, 5 + i * 56, 173, 52, 38)) {
      // Animate button press - invert colors
      drawButton(5 + i * 56, 173, 52, 38, axisNames[i], COLOR_WHITE, COLOR_DARK_GREEN, 3);
      delay_ms(150);
      drawButton(5 + i * 56, 173, 52, 38, axisNames[i], COLOR_DARK_GREEN, COLOR_WHITE, 3);
      // Send home command for axis i
      dbg_printf("$H%s\n", axisNames[i].c_str());
      return;
    }
  }

  // Increment selection
  for (int i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, 5 + i * 56, 231, 52, 38)) {
      pendantJog.selectedIncrement = i;
      float increments[] = {0.1, 1.0, 10.0, 100.0};
      pendantJog.increment = increments[i];
      // Partial redraw - only update increment buttons
      redrawJogIncrementButtons();
      return;
    }
  }

  // Bottom navigation buttons
  if (isTouchInBounds(x, y, 5, 277, 112, 40)) {
    currentPendantScreen = PSCREEN_MAIN_MENU;
  } else if (isTouchInBounds(x, y, 123, 277, 112, 40)) {
    currentPendantScreen = PSCREEN_PROBING_WORK;
  }
}

void handleSpindleControlTouch(int x, int y) {
  // Direction toggle
  if (isTouchInBounds(x, y, 5, 118, 112, 38)) {
    pendantSpindle.directionFwd = true;
    pendantMachine.spindleDir = "Fwd";
    // Partial redraw - only update direction buttons and RPM display
    redrawSpindleDirectionButtons();
  } else if (isTouchInBounds(x, y, 123, 118, 112, 38)) {
    pendantSpindle.directionFwd = false;
    pendantMachine.spindleDir = "Rev";
    // Partial redraw - only update direction buttons and RPM display
    redrawSpindleDirectionButtons();
  }

  // RPM Presets
  int presets[] = {6000, 12000, 24000};
  for (int i = 0; i < 3; i++) {
    int bx = 5 + i * 75;
    int by = 178;
    if (isTouchInBounds(x, y, bx, by, 70, 37)) {
      pendantSpindle.selectedPreset = i;
      pendantMachine.spindleRPM = presets[i];
      // Partial redraw - only update preset buttons and RPM display
      redrawSpindlePresetButtons();
      return;
    }
  }

  // Start/Stop buttons
  if (isTouchInBounds(x, y, 5, 230, 112, 40)) {
    // Animate button press
    drawButton(5, 230, 112, 40, "Start", COLOR_WHITE, COLOR_DARK_GREEN, 2);
    delay_ms(150);
    drawButton(5, 230, 112, 40, "Start", COLOR_DARK_GREEN, COLOR_WHITE, 2);
    dbg_printf("M3 S%d\n", pendantMachine.spindleRPM);
    pendantMachine.spindleRunning = true;
  } else if (isTouchInBounds(x, y, 123, 230, 112, 40)) {
    // Animate button press
    drawButton(123, 230, 112, 40, "Stop", COLOR_WHITE, COLOR_RED, 2);
    delay_ms(150);
    drawButton(123, 230, 112, 40, "Stop", COLOR_RED, COLOR_WHITE, 2);
    dbg_printf("M5\n");
    pendantMachine.spindleRunning = false;
  }

  // Main Menu
  if (isTouchInBounds(x, y, 5, 280, 230, 37)) {
    currentPendantScreen = PSCREEN_MAIN_MENU;
  }
}

void handleFeedsSpeedsTouch(int x, int y) {
  // Feed override buttons
  // Row 1 - 3 buttons - moved up from 105 to 95
  for (int i = 0; i < 3; i++) {
    int bx = 5 + i * 78;
    if (isTouchInBounds(x, y, bx, 95, 72, 37)) {
      pendantFeeds.selectedFeedOverride = i;
      int percentages[] = {50, 75, 100, 125, 150};
      pendantMachine.feedOverride = percentages[i];
      // Partial redraw - only update feed override buttons and readout
      redrawFeedOverrideButtons();
      return;
    }
  }
  // Row 2 - left button (125%) - moved up from 147 to 137
  if (isTouchInBounds(x, y, 5, 137, 72, 37)) {
    pendantFeeds.selectedFeedOverride = 3;
    pendantMachine.feedOverride = 125;
    // Partial redraw - only update feed override buttons and readout
    redrawFeedOverrideButtons();
    return;
  }
  // Row 2 - right button (150%) - moved up from 147 to 137
  if (isTouchInBounds(x, y, 161, 137, 72, 37)) {
    pendantFeeds.selectedFeedOverride = 4;
    pendantMachine.feedOverride = 150;
    // Partial redraw - only update feed override buttons and readout
    redrawFeedOverrideButtons();
    return;
  }

  // Spindle override buttons
  // Row 1 - 3 buttons - moved up from 204 to 194
  for (int i = 0; i < 3; i++) {
    int bx = 5 + i * 78;
    if (isTouchInBounds(x, y, bx, 194, 72, 37)) {
      pendantFeeds.selectedSpindleOverride = i;
      int percentages[] = {50, 75, 100, 125, 150};
      pendantMachine.spindleOverride = percentages[i];
      // Partial redraw - only update spindle override buttons and readout
      redrawSpindleOverrideButtons();
      return;
    }
  }
  // Row 2 - left button (125%) - moved up from 246 to 236
  if (isTouchInBounds(x, y, 5, 236, 72, 37)) {
    pendantFeeds.selectedSpindleOverride = 3;
    pendantMachine.spindleOverride = 125;
    // Partial redraw - only update spindle override buttons and readout
    redrawSpindleOverrideButtons();
    return;
  }
  // Row 2 - right button (150%) - moved up from 246 to 236
  if (isTouchInBounds(x, y, 161, 236, 72, 37)) {
    pendantFeeds.selectedSpindleOverride = 4;
    pendantMachine.spindleOverride = 150;
    // Partial redraw - only update spindle override buttons and readout
    redrawSpindleOverrideButtons();
    return;
  }

  // Main Menu - moved up from 290 to 280, height from 30 to 40
  if (isTouchInBounds(x, y, 5, 280, 230, 40)) {
    currentPendantScreen = PSCREEN_MAIN_MENU;
  }
}

void handleSDCardTouch(int x, int y) {
  // File selection - selecting a file opens it and goes to Status screen
  for (int i = 0; i < 5; i++) {
    if (isTouchInBounds(x, y, 5, 40 + i * 44, 230, 40)) {
      int displayIndex = i + pendantSdCard.scrollOffset;
      if (displayIndex < pendantSdCard.fileCount) {
        // Open file and switch to Status screen
        pendantMachine.currentFile = pendantSdCard.files[displayIndex];
        dbg_printf("Opening: %s\n", pendantMachine.currentFile.c_str());
        currentPendantScreen = PSCREEN_STATUS;
      }
      return;
    }
  }

  // Navigation - Back button with animation
  if (isTouchInBounds(x, y, 5, 240, 112, 38)) {
    if (pendantSdCard.scrollOffset > 0) {
      // Animate button press
      drawButton(5, 240, 112, 38, "Back", COLOR_WHITE, COLOR_BUTTON_GRAY, 2);
      delay_ms(150);
      pendantSdCard.scrollOffset--;
      drawCurrentPendantScreen();
    }
  }
  // Navigation - Next button with animation
  else if (isTouchInBounds(x, y, 123, 240, 112, 38)) {
    if (pendantSdCard.scrollOffset + 5 < pendantSdCard.fileCount) {
      // Animate button press
      drawButton(123, 240, 112, 38, "Next", COLOR_WHITE, COLOR_BUTTON_GRAY, 2);
      delay_ms(150);
      pendantSdCard.scrollOffset++;
      drawCurrentPendantScreen();
    }
  }

  // Main Menu
  if (isTouchInBounds(x, y, 5, 282, 230, 38)) {
    currentPendantScreen = PSCREEN_MAIN_MENU;
  }
}

void handleProbingWorkTouch(int x, int y) {
  // Work coordinate system selection
  for (int i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, 5 + i * 56, 55, 52, 38)) {
      pendantProbing.selectedCoordIndex = i;
      String coords[] = {"G54", "G55", "G56", "G57"};
      pendantProbing.selectedCoordSystem = coords[i];
      // Partial redraw - only update coordinate system buttons
      redrawWorkCoordButtons();
      return;
    }
  }

  // Set Work Zero
  if (isTouchInBounds(x, y, 5, 230, 46, 38)) {
    // Animate button press
    drawButton(5, 230, 46, 38, "X", COLOR_WHITE, COLOR_DARK_GREEN, 3);
    delay_ms(150);
    drawButton(5, 230, 46, 38, "X", COLOR_DARK_GREEN, COLOR_WHITE, 3);
    dbg_printf("G10 L20 P1 X0\n");
  } else if (isTouchInBounds(x, y, 52, 230, 46, 38)) {
    // Animate button press
    drawButton(52, 230, 46, 38, "Y", COLOR_WHITE, COLOR_DARK_GREEN, 3);
    delay_ms(150);
    drawButton(52, 230, 46, 38, "Y", COLOR_DARK_GREEN, COLOR_WHITE, 3);
    dbg_printf("G10 L20 P1 Y0\n");
  } else if (isTouchInBounds(x, y, 99, 230, 46, 38)) {
    // Animate button press
    drawButton(99, 230, 46, 38, "Z", COLOR_WHITE, COLOR_DARK_GREEN, 3);
    delay_ms(150);
    drawButton(99, 230, 46, 38, "Z", COLOR_DARK_GREEN, COLOR_WHITE, 3);
    dbg_printf("G10 L20 P1 Z0\n");
  } else if (isTouchInBounds(x, y, 146, 230, 46, 38)) {
    // Animate button press
    drawButton(146, 230, 46, 38, "A", COLOR_WHITE, COLOR_DARK_GREEN, 3);
    delay_ms(150);
    drawButton(146, 230, 46, 38, "A", COLOR_DARK_GREEN, COLOR_WHITE, 3);
    dbg_printf("G10 L20 P1 A0\n");
  } else if (isTouchInBounds(x, y, 193, 230, 46, 38)) {
    // Animate button press
    drawButton(193, 230, 46, 38, "ALL", COLOR_WHITE, COLOR_DARK_GREEN, 2);
    delay_ms(150);
    drawButton(193, 230, 46, 38, "ALL", COLOR_DARK_GREEN, COLOR_WHITE, 2);
    dbg_printf("G10 L20 P1 X0 Y0 Z0 A0\n");
  }

  // Bottom navigation buttons
  if (isTouchInBounds(x, y, 5, 277, 112, 40)) {
    currentPendantScreen = PSCREEN_MAIN_MENU;
  } else if (isTouchInBounds(x, y, 123, 277, 112, 40)) {
    currentPendantScreen = PSCREEN_JOG_HOMING;
  }
}

void handleMacrosTouch(int x, int y) {
  // Macro buttons
  for (int i = 0; i < 10; i++) {
    int bx = 5 + (i % 2) * 118;
    int by = 40 + (i / 2) * 48;
    if (isTouchInBounds(x, y, bx, by, 112, 43)) {
      // Animate button press
      String label = "Macro " + String(i);
      drawButton(bx, by, 112, 43, label, COLOR_WHITE, COLOR_BUTTON_GRAY, 2);
      delay_ms(150);
      drawButton(bx, by, 112, 43, label, COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
      dbg_printf("Executing Macro %d\n", i);
      return;
    }
  }

  // Main Menu
  if (isTouchInBounds(x, y, 5, 280, 230, 40)) {
    currentPendantScreen = PSCREEN_MAIN_MENU;
  }
}

void handleProbingTouch(int x, int y) {
  // Probe Type selection - 2 buttons stacked vertically with animation
  String probeTypes[] = {"Z Surface", "Tool Height"};
  uint16_t probeColors[] = {COLOR_DARK_GREEN, COLOR_ORANGE};

  for (int i = 0; i < 2; i++) {
    int by = 116 + i * 43;
    if (isTouchInBounds(x, y, 5, by, 230, 38)) {
      // Animate button press - invert colors
      drawButton(5, by, 230, 38, probeTypes[i], COLOR_WHITE, probeColors[i], 2);
      delay_ms(150);
      drawButton(5, by, 230, 38, probeTypes[i], probeColors[i], COLOR_WHITE, 2);
      pendantProbe.selectedProbeType = i;
      dbg_printf("Probe type selected: %s\n", probeTypes[i].c_str());
      return;
    }
  }

  // Main Menu button
  if (isTouchInBounds(x, y, 5, 280, 230, 40)) {
    currentPendantScreen = PSCREEN_MAIN_MENU;
  }
}

void handlePendantTouch(int x, int y) {
  switch (currentPendantScreen) {
    case PSCREEN_MAIN_MENU:
      handleMainMenuTouch(x, y);
      break;
    case PSCREEN_JOG_HOMING:
      handleJogHomingTouch(x, y);
      break;
    case PSCREEN_SPINDLE_CONTROL:
      handleSpindleControlTouch(x, y);
      break;
    case PSCREEN_FEEDS_SPEEDS:
      handleFeedsSpeedsTouch(x, y);
      break;
    case PSCREEN_SD_CARD:
      handleSDCardTouch(x, y);
      break;
    case PSCREEN_PROBING_WORK:
      handleProbingWorkTouch(x, y);
      break;
    case PSCREEN_PROBING:
      handleProbingTouch(x, y);
      break;
    case PSCREEN_MACROS:
      handleMacrosTouch(x, y);
      break;
    case PSCREEN_STATUS:
      // Main Menu button at bottom left - Y=280, height 40
      if (isTouchInBounds(x, y, 5, 280, 112, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
      }
      // FluidNC button at bottom right - Y=280, height 40
      else if (isTouchInBounds(x, y, 123, 280, 112, 40)) {
        currentPendantScreen = PSCREEN_FLUIDNC;
      }
      break;
    case PSCREEN_FLUIDNC:
      // Main Menu button at bottom - at Y=264
      if (isTouchInBounds(x, y, 5, 264, 230, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
      }
      break;
  }

  if (currentPendantScreen != previousPendantScreen) {
    previousPendantScreen = currentPendantScreen;
    drawCurrentPendantScreen();
  }
}

// ===== Physical Button Handling =====
void handlePendantPhysicalButtons() {
  static unsigned long lastDebounceTime[3] = {0, 0, 0};
  static bool lastButtonState[3] = {HIGH, HIGH, HIGH};
  static bool buttonState[3] = {HIGH, HIGH, HIGH};        // Debounced state
  static bool buttonHandled[3] = {false, false, false};   // Track if press was handled
  const unsigned long debounceDelay = 50;

  int buttons[] = {red_button_pin, dial_button_pin, green_button_pin};

  for (int i = 0; i < 3; i++) {
    if (buttons[i] == -1) continue; // Skip if button not configured

    bool reading = digitalRead(buttons[i]);

    // Reset debounce timer if state changed
    if (reading != lastButtonState[i]) {
      lastDebounceTime[i] = millis();
    }
    lastButtonState[i] = reading;

    // Update debounced state after delay
    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      // Only act on state change to LOW (button pressed) and not already handled
      if (reading == LOW && buttonState[i] == HIGH && !buttonHandled[i]) {
        buttonHandled[i] = true;  // Mark as handled

        switch (i) {
          case 0: // Red - E-Stop
            dbg_printf("!\n");
            pendantMachine.status = "ALARM";
            break;
          case 1: // Yellow - Context-sensitive button
            // If in ALARM state, clear alarm with $X command
            if (pendantMachine.status == "ALARM") {
              dbg_printf("$X\n");
              pendantMachine.status = "IDLE";
            } else {
              // Otherwise, send Pause/Feed Hold
              dbg_printf("!\n");
              pendantMachine.status = "HOLD";
            }
            break;
          case 2: // Green - Cycle Start
            dbg_printf("~\n");
            pendantMachine.status = "RUN";
            break;
        }
        drawCurrentPendantScreen();
      }

      // Reset handled flag when button is released
      if (reading == HIGH) {
        buttonHandled[i] = false;
      }

      buttonState[i] = reading;
    }
  }
}

// ===== Public Interface Functions =====
void setup_pendant() {
  // The hardware initialization is already done by the existing FluidDial code

  // Load saved rotation preference from NVS
  preferences.begin("pendant", false); // Read/Write mode
  int savedRotation = preferences.getInt("rotation", 2); // Default to 2 (normal)
  preferences.end();

  // Apply saved rotation
  pendantMachine.rotation = savedRotation;
  if (pendantMachine.rotation == 2) {
    pendantMachine.displayRotation = "Normal";
  } else {
    pendantMachine.displayRotation = "Upside Down";
  }
  display.setRotation(pendantMachine.rotation);

  dbg_printf("Loaded display rotation: %s (%d)\n", pendantMachine.displayRotation.c_str(), pendantMachine.rotation);

  // Initialize encoder pins if using encoder
  if (USE_ENCODER) {
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
  }

  // Draw our initial screen
  drawCurrentPendantScreen();
  dbg_printf("CNC Pendant UI Initialized\n");
}

void loop_pendant() {
  // Handle physical buttons
  if (USE_PHYSICAL_BUTTONS) {
    handlePendantPhysicalButtons();
  }

  // Handle jog dial encoder for jogging (only active on Jog & Homing screen)
  if (USE_ENCODER && currentPendantScreen == PSCREEN_JOG_HOMING) {
    static int lastJogEncoderCLK = HIGH;
    static unsigned long lastJogTime = 0;

    int currentCLK = digitalRead(ENCODER_CLK);

    // Detect rotation - check if CLK changed from HIGH to LOW
    if (currentCLK != lastJogEncoderCLK && currentCLK == LOW) {
      // Debounce - only handle if not rotated recently
      if (millis() - lastJogTime > 50) { // 50ms debounce for jog dial
        int currentDT = digitalRead(ENCODER_DT);

        // Determine rotation direction
        bool clockwise = (currentDT == HIGH);

        // Get axis names for G-code command
        String axisNames[] = {"X", "Y", "Z", "A"};
        String axis = axisNames[pendantJog.selectedAxis];

        // Calculate jog distance
        float distance = clockwise ? pendantJog.increment : -pendantJog.increment;

        // Send jog command using $J= (jog mode command for FluidNC/GRBL)
        dbg_printf("$J=G91 %s%.3f F1000\n", axis.c_str(), distance);

        lastJogTime = millis();
        dbg_printf("Jog %s: %s%.3fmm\n", axis.c_str(), clockwise ? "+" : "", distance);
      }
    }

    lastJogEncoderCLK = currentCLK;
  }

  // Handle encoder rotation for screen rotation (only on FluidNC screen)
  if (USE_ENCODER) {
    static int lastEncoderCLK = HIGH;
    static unsigned long lastRotationTime = 0;
    static PendantScreen lastScreen = PSCREEN_MAIN_MENU;

    // Reset encoder state when entering FluidNC screen
    if (currentPendantScreen == PSCREEN_FLUIDNC && lastScreen != PSCREEN_FLUIDNC) {
      lastEncoderCLK = digitalRead(ENCODER_CLK);
      lastScreen = PSCREEN_FLUIDNC;
    } else if (currentPendantScreen != PSCREEN_FLUIDNC) {
      lastScreen = currentPendantScreen;
    }

    // Only handle rotation when on FluidNC screen
    if (currentPendantScreen == PSCREEN_FLUIDNC) {
      int currentCLK = digitalRead(ENCODER_CLK);

      // Detect rotation - check if CLK changed from HIGH to LOW
      if (currentCLK != lastEncoderCLK && currentCLK == LOW) {
        // Debounce - only handle if not rotated recently
        if (millis() - lastRotationTime > 300) {
          int currentDT = digitalRead(ENCODER_DT);

          // Determine rotation direction and toggle display rotation
          // When CLK goes LOW, if DT is HIGH = clockwise, if DT is LOW = counter-clockwise
          // Either direction toggles the rotation
          if (pendantMachine.rotation == 2) {
            pendantMachine.rotation = 0;
            pendantMachine.displayRotation = "Upside Down";
          } else {
            pendantMachine.rotation = 2;
            pendantMachine.displayRotation = "Normal";
          }
          display.setRotation(pendantMachine.rotation);

          // Save rotation preference to NVS
          preferences.begin("pendant", false);
          preferences.putInt("rotation", pendantMachine.rotation);
          preferences.end();

          drawCurrentPendantScreen();
          lastRotationTime = millis();
          dbg_printf("Display rotation toggled to: %s (saved to NVS)\n", pendantMachine.displayRotation.c_str());
        }
      }
      lastEncoderCLK = currentCLK;
    }
  }

  // Periodic sprite updates for position displays (reduces full screen redraws)
  static unsigned long lastSpriteUpdate = 0;
  if (millis() - lastSpriteUpdate > 100) { // Update every 100ms
    if (spritesInitialized) {
      switch (currentPendantScreen) {
        case PSCREEN_MAIN_MENU:
          // Update status and axis display using sprite (no full redraw needed)
          updateMainMenuDisplay();
          break;
        case PSCREEN_JOG_HOMING:
          // Update axis position display using sprite (no full redraw needed)
          updateJogAxisDisplay();
          break;
        case PSCREEN_PROBING_WORK:
          // Update both machine and work position displays using sprites
          updateWorkMachinePos();
          updateWorkAreaPos();
          break;
        case PSCREEN_FEEDS_SPEEDS:
          // Update feed/spindle values and override readouts using sprites
          updateFeedsSpeedsTopDisplay();
          updateFeedOverrideDisplay();
          updateSpindleOverrideDisplay();
          break;
        case PSCREEN_SPINDLE_CONTROL:
          // Update RPM and direction display using sprite
          updateSpindleRPMDisplay();
          break;
        case PSCREEN_PROBING:
          // Update current position and probe settings using sprites
          updateProbePositionDisplay();
          updateProbeSettingsDisplay();
          break;
        case PSCREEN_STATUS:
          // Update machine status, current file, axis positions, and feed/spindle info
          updateStatusMachineStatus();
          updateStatusCurrentFile();
          updateStatusAxisPositions();
          updateStatusFeedSpindle();
          break;
        // Add more sprite updates for other screens as needed
        default:
          break;
      }
    }
    lastSpriteUpdate = millis();
  }

  // Handle touch input
  lgfx::touch_point_t tp;
  if (display.getTouch(&tp)) {
    // Map touch coordinates to display coordinates
    int x = tp.x;
    int y = tp.y;

    // Debounce - only handle if not touched recently
    static unsigned long lastTouchTime = 0;
    if (millis() - lastTouchTime > 200) {
      handlePendantTouch(x, y);
      lastTouchTime = millis();
    }
  }
}
