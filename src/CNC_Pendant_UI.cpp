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

// Include from existing FluidDial project
#include "System.h"
#include "Scene.h"

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
  PSCREEN_FEEDS_SPEEDS,
  PSCREEN_SPINDLE_CONTROL,
  PSCREEN_MACROS,
  PSCREEN_SD_CARD,
  PSCREEN_FLUIDNC
};

PendantScreen currentPendantScreen = PSCREEN_MAIN_MENU;
PendantScreen previousPendantScreen = PSCREEN_MAIN_MENU;

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

// ===== Screen Drawing Functions =====

void drawMainMenu() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("MAIN MENU");

  // Combined status and axis info box (reduced height from 75 to 65)
  display.fillRoundRect(5, 40, 230, 65, 5, COLOR_DARKER_BG);

  // STATUS label and value on the left
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 45);
  display.print("STATUS");

  display.setTextColor(COLOR_CYAN);
  display.setTextSize(3);
  display.setCursor(10, 62);
  display.print(pendantMachine.status);

  // AXIS label in the center
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(95, 45);
  display.print("AXIS");

  // Axis coordinates in a 2x2 grid on the right side
  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(2);

  // X coordinate (top left of axis grid)
  display.setCursor(95, 56);
  display.print("X");
  display.print(pendantMachine.posX, 2);

  // Y coordinate (top right of axis grid)
  display.setCursor(165, 56);
  display.print(" Y");
  display.print(pendantMachine.posY, 2);

  // Z coordinate (bottom left of axis grid)
  display.setCursor(95, 78);
  display.print("Z");
  display.print(pendantMachine.posZ, 2);

  // A coordinate (bottom right of axis grid)
  display.setCursor(165, 78);
  display.print(" A");
  display.print(pendantMachine.posA, 2);

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
  drawButton(5, btnY + btnGap*3, 112, btnH, "FluidNC", COLOR_BLUE, COLOR_WHITE, 2);
  drawButton(123, btnY + btnGap*3, 112, btnH, "Status", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawStatusScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("STATUS");

  // Machine Status
  display.fillRoundRect(5, 40, 230, 50, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 45);
  display.print("MACHINE STATUS");
  display.setTextColor(COLOR_CYAN);
  display.setTextSize(3);
  display.setCursor(10, 62);
  display.print(pendantMachine.status);

  // Current File - moved up from 100 to 95
  display.fillRoundRect(5, 95, 230, 40, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 100);
  display.print("CURRENT FILE");
  display.setTextSize(1);
  display.setCursor(10, 115);
  display.print(pendantMachine.currentFile);

  // Axis Positions - moved up from 150 to 140, reduced height from 70 to 65
  display.fillRoundRect(5, 140, 230, 65, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 145);
  display.print("AXIS POSITIONS");

  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(2);
  display.setCursor(10, 160);
  display.print("X:");
  display.print(pendantMachine.posX, 1);
  display.setCursor(130, 160);
  display.print("Y:");
  display.print(pendantMachine.posY, 1);
  display.setCursor(10, 183);
  display.print("Z:");
  display.print(pendantMachine.posZ, 1);
  display.setCursor(130, 183);
  display.print("A:");
  display.print(pendantMachine.posA, 1);

  // Feed Rate and Spindle - reduced height from 70 to 65 for consistent gaps
  display.fillRoundRect(5, 210, 112, 65, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 213);
  display.print("FEED");
  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(2);
  display.setCursor(10, 235);
  display.print(pendantMachine.feedRate);
  // Unit below and right-justified
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  int16_t mmMinWidth = display.textWidth("mm/min");
  display.setCursor(117 - 5 - mmMinWidth, 260);
  display.print("mm/min");

  display.fillRoundRect(123, 210, 112, 65, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(128, 213);
  display.print("SPINDLE");
  // Direction inline with label, right-justified
  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(1);
  int16_t dirWidth = display.textWidth(pendantMachine.spindleDir.c_str());
  display.setCursor(235 - 5 - dirWidth, 213);
  display.print(pendantMachine.spindleDir);

  display.setTextColor(COLOR_GREEN);
  display.setTextSize(2);
  display.setCursor(128, 235);
  display.print(pendantMachine.spindleRPM);
  // RPM below and right-justified
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  int16_t rpmWidth = display.textWidth("RPM");
  display.setCursor(235 - 5 - rpmWidth, 260);
  display.print("RPM");

  // Main Menu button - with 5px gap from panels above
  drawButton(5, 280, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawJogHomingScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("JOG & HOMING");

  // Display area showing selected axis and position (reduced height)
  display.fillRoundRect(5, 40, 230, 55, 5, COLOR_DARKER_BG);

  // Current axis indicator on left side
  String axisNames[] = {"X", "Y", "Z", "A"};
  display.setTextColor(COLOR_GREEN);
  display.setTextSize(3);
  display.setCursor(10, 52);
  display.print(axisNames[pendantJog.selectedAxis]);

  // Current position for selected axis
  float positions[] = {pendantMachine.posX, pendantMachine.posY, pendantMachine.posZ, pendantMachine.posA};
  display.setTextSize(3);
  display.setCursor(55, 52);

  // Get position text to calculate width
  char posBuffer[16];
  dtostrf(positions[pendantJog.selectedAxis], 1, 2, posBuffer);
  display.print(posBuffer);

  // Calculate width of position text
  int16_t posTextWidth = display.textWidth(posBuffer);

  // Measurement unit below selected axis position, right-aligned to position
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(2);
  int16_t unitTextWidth = display.textWidth("mm");
  display.setCursor(55 + posTextWidth - unitTextWidth, 78);
  display.print("mm");

  // All axis positions in 2x2 grid on right side
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  // X (top left)
  display.setCursor(155, 48);
  display.print("X:");
  display.print(pendantMachine.posX, 1);
  // Y (top right)
  display.setCursor(155, 60);
  display.print("Y:");
  display.print(pendantMachine.posY, 1);
  // Z (bottom left)
  display.setCursor(155, 72);
  display.print("Z:");
  display.print(pendantMachine.posZ, 1);
  // A (bottom right)
  display.setCursor(155, 84);
  display.print("A:");
  display.print(pendantMachine.posA, 1);

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

  // Machine Position
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 100);
  display.print("MACHINE POS");

  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(2);
  display.setCursor(5, 113);
  display.print("X:");
  display.print(pendantMachine.posX, 1);
  display.setCursor(125, 113);
  display.print("Y:");
  display.print(pendantMachine.posY, 1);
  display.setCursor(5, 133);
  display.print("Z:");
  display.print(pendantMachine.posZ, 1);
  display.setCursor(125, 133);
  display.print("A:");
  display.print(pendantMachine.posA, 1);

  // Work Area Position
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(5, 158);
  display.print("WORK POS");

  display.setTextColor(COLOR_CYAN);
  display.setTextSize(2);
  display.setCursor(5, 171);
  display.print("X:");
  display.print(pendantMachine.workX, 1);
  display.setCursor(125, 171);
  display.print("Y:");
  display.print(pendantMachine.workY, 1);
  display.setCursor(5, 191);
  display.print("Z:");
  display.print(pendantMachine.workZ, 1);
  display.setCursor(125, 191);
  display.print("A:");
  display.print(pendantMachine.workA, 1);

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

void drawFeedsSpeedsScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("FEEDS & SPEEDS");

  // Current values display - reduced height from 45 to 35
  display.fillRoundRect(5, 40, 112, 35, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 43);
  display.print("FEED");
  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(2);
  display.setCursor(10, 53);
  display.print(pendantMachine.feedRate);
  // Unit inline
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10 + display.textWidth(String(pendantMachine.feedRate).c_str()) * 2 + 4, 57);
  display.print("mm/min");

  display.fillRoundRect(123, 40, 112, 35, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(128, 43);
  display.print("SPINDLE");
  display.setTextColor(COLOR_GREEN);
  display.setTextSize(2);
  display.setCursor(128, 53);
  display.print(pendantMachine.spindleRPM);
  // Unit inline
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(128 + display.textWidth(String(pendantMachine.spindleRPM).c_str()) * 2 + 4, 57);
  display.print("RPM");

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

  // Readout inline - aligned to middle column
  display.fillRoundRect(83, 137, 72, 37, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(2);
  int16_t textW = display.textWidth((String(pendantMachine.feedOverride) + "%").c_str());
  display.setCursor(119 - textW/2, 148);
  display.print(pendantMachine.feedOverride);
  display.print("%");

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

  // Readout inline - aligned to middle column
  display.fillRoundRect(83, 236, 72, 37, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GREEN);
  display.setTextSize(2);
  textW = display.textWidth((String(pendantMachine.spindleOverride) + "%").c_str());
  display.setCursor(119 - textW/2, 247);
  display.print(pendantMachine.spindleOverride);
  display.print("%");

  uint16_t bgColor4s = (4 == pendantFeeds.selectedSpindleOverride) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
  drawButton(161, 236, 72, 37, percentages[4], bgColor4s, COLOR_WHITE, 2);

  // Main Menu button - moved up from 290 to 280, height from 30 to 40
  drawButton(5, 280, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawSpindleControlScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("SPINDLE CONTROL");

  // RPM Display
  display.fillRoundRect(5, 40, 230, 60, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 45);
  display.print("RPM");

  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(4);
  display.setCursor(10, 60);
  display.print(pendantMachine.spindleRPM);

  display.setTextColor(COLOR_CYAN);
  display.setTextSize(2);
  display.setCursor(160, 70);
  display.print(pendantMachine.spindleDir);

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
  drawButton(5, 297, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawSDCardScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("SD CARD");

  // File list - 4 files visible
  for (int i = 0; i < 4 && i < pendantSdCard.fileCount; i++) {
    int displayIndex = i + pendantSdCard.scrollOffset;
    if (displayIndex >= pendantSdCard.fileCount) break;

    uint16_t bgColor = (displayIndex == pendantSdCard.selectedFile) ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    display.fillRoundRect(5, 40 + i * 48, 230, 43, 8, bgColor);
    display.setTextColor(COLOR_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 54 + i * 48);
    display.print(pendantSdCard.files[displayIndex]);
  }

  // Navigation buttons
  drawButton(5, 235, 112, 38, "Back", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
  drawButton(123, 235, 112, 38, "Next", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);

  // Action buttons
  drawButton(5, 278, 112, 38, "Open", COLOR_GREEN, COLOR_WHITE, 2);
  drawButton(123, 278, 112, 38, "Delete", COLOR_RED, COLOR_WHITE, 2);

  // Main Menu button
  drawButton(5, 297, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void drawFluidNCScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("FLUIDNC");

  // FluidDial Version
  display.fillRoundRect(5, 40, 230, 50, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 45);
  display.print("FLUIDDIAL VERSION");
  display.setTextColor(COLOR_GREEN);
  display.setTextSize(2);
  display.setCursor(10, 62);
  display.print(pendantMachine.fluidDialVersion);

  // FluidNC Version
  display.fillRoundRect(5, 98, 230, 50, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 103);
  display.print("FLUIDNC VERSION");
  display.setTextColor(COLOR_GREEN);
  display.setTextSize(2);
  display.setCursor(10, 120);
  display.print(pendantMachine.fluidNCVersion);

  // Connection Info
  display.fillRoundRect(5, 156, 230, 80, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 161);
  display.print("CONNECTION");

  display.setCursor(10, 178);
  display.print("Baud:");
  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(2);
  display.setCursor(100, 175);
  display.print(pendantMachine.baudRate);

  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 198);
  display.print("Port:");
  display.setTextColor(COLOR_CYAN);
  display.setTextSize(1);
  display.setCursor(10, 210);
  display.print(pendantMachine.port);

  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 223);
  display.print("Status:");
  display.setTextColor(COLOR_GREEN);
  display.setTextSize(1);
  display.setCursor(100, 223);
  display.print(pendantMachine.connectionStatus);

  // CYD ESP32 Resources
  display.fillRoundRect(5, 244, 230, 45, 5, COLOR_DARKER_BG);
  display.setTextColor(COLOR_GRAY_TEXT);
  display.setTextSize(1);
  display.setCursor(10, 249);
  display.print("FREE HEAP");

  display.setTextColor(COLOR_ORANGE);
  display.setTextSize(2);
  display.setCursor(10, 266);
  display.print(pendantMachine.freeHeap);
  display.print(" KB");

  // Main Menu button
  drawButton(5, 297, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
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
    currentPendantScreen = PSCREEN_FLUIDNC;
  } else if (isTouchInBounds(x, y, 123, btnY + btnGap*3, 112, btnH)) {
    currentPendantScreen = PSCREEN_STATUS;
  }
}

void handleJogHomingTouch(int x, int y) {
  // Axis selection
  for (int i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, 5 + i * 56, 115, 52, 38)) {
      pendantJog.selectedAxis = i;
      drawCurrentPendantScreen();
      return;
    }
  }

  // Home buttons
  for (int i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, 5 + i * 56, 173, 52, 38)) {
      // Send home command for axis i
      String axisNames[] = {"X", "Y", "Z", "A"};
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
      drawCurrentPendantScreen();
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
    drawCurrentPendantScreen();
  } else if (isTouchInBounds(x, y, 123, 118, 112, 38)) {
    pendantSpindle.directionFwd = false;
    pendantMachine.spindleDir = "Rev";
    drawCurrentPendantScreen();
  }

  // RPM Presets
  int presets[] = {6000, 12000, 24000};
  for (int i = 0; i < 3; i++) {
    int bx = 5 + i * 75;
    int by = 178;
    if (isTouchInBounds(x, y, bx, by, 70, 37)) {
      pendantSpindle.selectedPreset = i;
      pendantMachine.spindleRPM = presets[i];
      drawCurrentPendantScreen();
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
      drawCurrentPendantScreen();
      return;
    }
  }
  // Row 2 - left button (125%) - moved up from 147 to 137
  if (isTouchInBounds(x, y, 5, 137, 72, 37)) {
    pendantFeeds.selectedFeedOverride = 3;
    pendantMachine.feedOverride = 125;
    drawCurrentPendantScreen();
    return;
  }
  // Row 2 - right button (150%) - moved up from 147 to 137
  if (isTouchInBounds(x, y, 161, 137, 72, 37)) {
    pendantFeeds.selectedFeedOverride = 4;
    pendantMachine.feedOverride = 150;
    drawCurrentPendantScreen();
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
      drawCurrentPendantScreen();
      return;
    }
  }
  // Row 2 - left button (125%) - moved up from 246 to 236
  if (isTouchInBounds(x, y, 5, 236, 72, 37)) {
    pendantFeeds.selectedSpindleOverride = 3;
    pendantMachine.spindleOverride = 125;
    drawCurrentPendantScreen();
    return;
  }
  // Row 2 - right button (150%) - moved up from 246 to 236
  if (isTouchInBounds(x, y, 161, 236, 72, 37)) {
    pendantFeeds.selectedSpindleOverride = 4;
    pendantMachine.spindleOverride = 150;
    drawCurrentPendantScreen();
    return;
  }

  // Main Menu - moved up from 290 to 280, height from 30 to 40
  if (isTouchInBounds(x, y, 5, 280, 230, 40)) {
    currentPendantScreen = PSCREEN_MAIN_MENU;
  }
}

void handleSDCardTouch(int x, int y) {
  // File selection
  for (int i = 0; i < 4; i++) {
    if (isTouchInBounds(x, y, 5, 40 + i * 48, 230, 43)) {
      pendantSdCard.selectedFile = i + pendantSdCard.scrollOffset;
      drawCurrentPendantScreen();
      return;
    }
  }

  // Navigation
  if (isTouchInBounds(x, y, 5, 235, 112, 38)) {
    // Back
    if (pendantSdCard.scrollOffset > 0) {
      pendantSdCard.scrollOffset--;
      drawCurrentPendantScreen();
    }
  } else if (isTouchInBounds(x, y, 123, 235, 112, 38)) {
    // Next
    if (pendantSdCard.scrollOffset + 4 < pendantSdCard.fileCount) {
      pendantSdCard.scrollOffset++;
      drawCurrentPendantScreen();
    }
  }

  // Actions
  if (isTouchInBounds(x, y, 5, 278, 112, 38)) {
    // Open file
    pendantMachine.currentFile = pendantSdCard.files[pendantSdCard.selectedFile];
    dbg_printf("Opening: %s\n", pendantMachine.currentFile.c_str());
  } else if (isTouchInBounds(x, y, 123, 278, 112, 38)) {
    // Delete file
    dbg_printf("Deleting: %s\n", pendantSdCard.files[pendantSdCard.selectedFile].c_str());
  }

  // Main Menu
  if (isTouchInBounds(x, y, 5, 297, 230, 40)) {
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
      drawCurrentPendantScreen();
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
      dbg_printf("Executing Macro %d\n", i);
      return;
    }
  }

  // Main Menu
  if (isTouchInBounds(x, y, 5, 297, 230, 40)) {
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
    case PSCREEN_MACROS:
      handleMacrosTouch(x, y);
      break;
    case PSCREEN_STATUS:
      // Main Menu button at bottom - Y=280, height 40
      if (isTouchInBounds(x, y, 5, 280, 230, 40)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
      }
      break;
    case PSCREEN_FLUIDNC:
      // Main Menu button at bottom
      if (isTouchInBounds(x, y, 5, 297, 230, 40)) {
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
  const unsigned long debounceDelay = 50;

  int buttons[] = {red_button_pin, dial_button_pin, green_button_pin};

  for (int i = 0; i < 3; i++) {
    if (buttons[i] == -1) continue; // Skip if button not configured

    bool reading = digitalRead(buttons[i]);

    if (reading != lastButtonState[i]) {
      lastDebounceTime[i] = millis();
    }

    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      if (reading == LOW) {
        switch (i) {
          case 0: // Red - E-Stop
            dbg_printf("!\n");
            pendantMachine.status = "ALARM";
            break;
          case 1: // Yellow - Pause
            dbg_printf("!\n");
            pendantMachine.status = "HOLD";
            break;
          case 2: // Green - Cycle Start
            dbg_printf("~\n");
            pendantMachine.status = "RUN";
            break;
        }
        drawCurrentPendantScreen();
      }
    }

    lastButtonState[i] = reading;
  }
}

// ===== Public Interface Functions =====
void setup_pendant() {
  // The hardware initialization is already done by the existing FluidDial code
  // Display rotation is already set to 180 degrees in ardmain.cpp

  // Draw our initial screen
  drawCurrentPendantScreen();
  dbg_printf("CNC Pendant UI Initialized\n");
}

void loop_pendant() {
  // Handle physical buttons
  if (USE_PHYSICAL_BUTTONS) {
    handlePendantPhysicalButtons();
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
