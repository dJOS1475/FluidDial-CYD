/*
 * colors.js — RGB565 colour constants.
 *
 * The block between the GENERATED markers below is produced by sync.py from the
 * firmware's colour #defines (src/cnc_pendant_config.h + src/screens/screen_probe.h),
 * so a colour tweak in the firmware propagates to the sim automatically:
 *     python3 simulator/sync.py
 * Don't hand-edit inside the markers — your changes will be overwritten. Add any
 * sim-only colour aliases BELOW the END marker instead.
 */

// === BEGIN GENERATED COLORS (sync.py — from firmware #defines) ===
const COLOR_BACKGROUND = 0x0000;
const COLOR_DARKER_BG = 0x2104;
const COLOR_TITLE = 0xFD20;
const COLOR_GRAY_TEXT = 0x7BEF;
const COLOR_ORANGE = 0xFD20;
const COLOR_GREEN = 0x07E0;
const COLOR_DARK_GREEN = 0x0360;
const COLOR_CYAN = 0x07FF;
const COLOR_BLUE = 0x1C9F;
const COLOR_RED = 0xF800;
const COLOR_WHITE = 0xFFFF;
const COLOR_BUTTON_GRAY = 0x31A6;
const COLOR_BUTTON_ACTIVE = 0x5AEB;
const COLOR_TEAL = 0x032C;
const COLOR_TEAL_BRIGHT = 0x05B6;

const PROBE_BG_SCREEN = COLOR_BACKGROUND;
const PROBE_BG_PANEL = COLOR_DARKER_BG;
const PROBE_C_YELLOW = COLOR_ORANGE;
const PROBE_C_GREEN = COLOR_GREEN;
const PROBE_C_RED = COLOR_RED;
const PROBE_C_BLUE = COLOR_CYAN;
const PROBE_C_LBLUE = COLOR_GRAY_TEXT;
const PROBE_C_DIMBLUE = 0x4208;
const PROBE_BTN_GREEN = COLOR_DARK_GREEN;
const PROBE_BTN_YELLOW = COLOR_ORANGE;
const PROBE_BTN_BLUE = COLOR_BLUE;
const PROBE_BTN_NAVY = COLOR_BUTTON_GRAY;
const PROBE_BTN_TEAL = COLOR_TEAL_BRIGHT;
const PROBE_SEL_BG = COLOR_DARKER_BG;
const PROBE_WARN_BG = 0x2880;
const PROBE_WARN_BDR = 0x6800;
const PROBE_WARNR_BG = 0x2800;
const PROBE_WARNR_BDR = 0x6808;
const PROBE_AMBER = COLOR_ORANGE;
// === END GENERATED COLORS ===

// Hand-maintained: LGFX built-in alias (not a firmware #define).
const TFT_RED = 0xF800;
