/*
 * CNC Pendant UI Configuration
 * Portable configuration for CYD (Cheap Yellow Display) ESP32
 *
 * To integrate into your project:
 * 1. Copy this file and cnc_pendant_ui.h to your project
 * 2. Adjust pin definitions below if needed
 * 3. Include in your main.cpp: #include "cnc_pendant_ui.h"
 * 4. Call setup_pendant() in your setup()
 * 5. Call loop_pendant() in your loop()
 */

#ifndef CNC_PENDANT_CONFIG_H
#define CNC_PENDANT_CONFIG_H

// ===== Display Configuration =====
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320
#define DISPLAY_ROTATION 2  // 0, 1, 2, 3 for different orientations

// ===== SPI Display Pins (CYD Standard) =====
#define TFT_SCLK 14
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1  // Not used on CYD
#define TFT_BL 27   // Backlight

// ===== Touch Screen Pins =====
#define TOUCH_CS 33
#define TOUCH_IRQ 36

// Touch calibration (adjust if touch is misaligned)
#define TS_MINX 200
#define TS_MINY 200
#define TS_MAXX 3700
#define TS_MAXY 3700

// ===== Physical Button Pins (Optional - P6 Connector) =====
#define BTN_RED_PIN 4       // E-Stop
#define BTN_YELLOW_PIN 17   // Pause
#define BTN_GREEN_PIN 16    // Cycle Start
#define USE_PHYSICAL_BUTTONS true  // Set to false if not using physical buttons

// ===== Rotary Encoder Pins (Optional - CN1 Connector) =====
#define ENCODER_CLK 22
#define ENCODER_DT 21
#define ENCODER_SW 35
#define USE_ENCODER true  // Set to false if not using encoder

// ===== Color Scheme - Dark Mode =====
#define COLOR_BACKGROUND 0x0000      // Pure black
#define COLOR_DARKER_BG 0x2104       // Very dark gray
#define COLOR_TITLE 0xFD20           // Orange
#define COLOR_GRAY_TEXT 0x7BEF       // Medium gray
#define COLOR_ORANGE 0xFD20          // Orange
#define COLOR_GREEN 0x07E0           // Green
#define COLOR_DARK_GREEN 0x0360      // Darker green
#define COLOR_CYAN 0x07FF            // Cyan
#define COLOR_BLUE 0x1C9F            // Blue
#define COLOR_RED 0xF800             // Red
#define COLOR_WHITE 0xFFFF           // White
#define COLOR_BUTTON_GRAY 0x31A6     // Dark button gray
#define COLOR_BUTTON_ACTIVE 0x5AEB   // Active button gray

// ===== Serial Configuration =====
#define SERIAL_BAUD 115200

#endif // CNC_PENDANT_CONFIG_H
