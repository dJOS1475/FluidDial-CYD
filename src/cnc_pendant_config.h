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

#ifdef PIBOT_PENDANT
#include "PiBotPins.h"
#endif

// ===== Firmware Version =====
// Single source of truth for the release version shown on the FluidNC screen
// (and anywhere else the pendant displays its own version).  Bump this in lock
// step with the top entry in CHANGELOG.md on every release — it is independent
// of the git-tag-derived `git_info` string (which only changes when a tag is
// actually cut, and is kept for the full build identifier in the About screen).
#ifdef PIBOT_PENDANT
#define FIRMWARE_VERSION "v2.4.1-pibot-dev"
#else
#define FIRMWARE_VERSION "v2.4.1"
#endif

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
#ifdef PIBOT_PENDANT
#define TFT_BL pibot::backlight
#else
#define TFT_BL 27   // Backlight
#endif

// ===== Touch Screen Pins =====
#define TOUCH_CS 33
#define TOUCH_IRQ 36

// Touch calibration (adjust if touch is misaligned)
#define TS_MINX 200
#define TS_MINY 200
#define TS_MAXX 3700
#define TS_MAXY 3700

// ===== Physical Button Pins (Optional - P6 Connector) =====
#ifdef PIBOT_PENDANT
#define BTN_RED_PIN pibot::button_cancel
#define BTN_YELLOW_PIN pibot::button_pause
#define BTN_GREEN_PIN pibot::button_start
#else
#define BTN_RED_PIN 4       // Cancel (software reset, not a safety E-stop)
#define BTN_YELLOW_PIN 17   // Pause
#define BTN_GREEN_PIN 16    // Cycle Start
#endif
#define USE_PHYSICAL_BUTTONS true  // Set to false if not using physical buttons

// ===== Rotary Encoder Pins (Optional - CN1 Connector) =====
#ifdef PIBOT_PENDANT
#define ENCODER_CLK pibot::encoder_a
#define ENCODER_DT pibot::encoder_b
#define ENCODER_SW -1  // No encoder push switch; GPIO35 is the selector
#else
#define ENCODER_CLK 22
#define ENCODER_DT 21
#define ENCODER_SW 35
#endif
#define USE_ENCODER true  // Set to false if not using encoder

// ===== Color Scheme =====
// v2.4.0 theme: navy panels, light-blue labels, bright-blue buttons, yellow =
// selected.  Several entries were re-pointed rather than renamed so every screen
// picks the theme up unchanged; screens/ui_style.h holds the extra colours the
// new title bar, buttons and fields use.  Note several values now coincide
// (COLOR_BLUE == COLOR_BUTTON_ACTIVE, COLOR_TITLE == COLOR_ORANGE), so never
// switch() on these.
#define COLOR_BACKGROUND 0x0000      // Pure black
#define COLOR_DARKER_BG 0x08A8       // Navy panel fill            rgb(10,20,70)
#define COLOR_TITLE 0xEEC0           // Yellow (title text on the old title bar)
#define COLOR_GRAY_TEXT 0x9EDF       // Light blue — labels, captions, hints  rgb(155,220,255)
#define COLOR_ORANGE 0xEEC0          // Yellow — selected / highlight         rgb(235,220,0)
#define COLOR_GREEN 0x07E0           // Green
#define COLOR_DARK_GREEN 0x0360      // Darker green
#define COLOR_CYAN 0x07FF            // Cyan
#define COLOR_BLUE 0x1A9B            // Button blue                         rgb(24,80,220)
#define COLOR_RED 0xF800             // Red
#define COLOR_YELLOW 0xFFE0          // Yellow (e.g. charging bolt)
#define COLOR_WHITE 0xFFFF           // White
#define COLOR_BUTTON_GRAY 0x198D     // Unselected / unlit (dim navy); unselected buttons draw blue  rgb(30,50,110)
#define COLOR_BUTTON_ACTIVE 0x1A9B   // Selected list row (button blue)
#define COLOR_TEAL 0x032C            // 565 of (0,100,100) — Spindle Dial idle; main-menu params group
#define COLOR_TEAL_BRIGHT 0x05B6     // 565 of (0,180,180) — Spindle Dial active
#define COLOR_INDIGO 0x52B9          // 565 of (82,85,206) — XYZ Corner routine; main-menu jobs group

// ===== Serial Configuration =====
#define SERIAL_BAUD 115200

#endif // CNC_PENDANT_CONFIG_H
