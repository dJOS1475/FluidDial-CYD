#pragma once
/*
 * ui_style — the v2.4.0 look: framed title bar with a per-screen icon and a
 * status box (signal + battery, or a plug on a wired pendant), bevelled 3D
 * buttons with optional icons, FreeSansBold text, outlined navy panels.
 *
 * The palette itself lives in cnc_pendant_config.h (the COLOR_* values were
 * re-pointed at this theme); these are the extra colours only the new pieces
 * use.  Prototyped in the simulator (simulator/js/uistyle.js) first — keep the
 * two in step.
 */
#include "pendant_shared.h"

// ---- Extra theme colours (RGB565) ----
#define UI_TITLE_BG      0x1151   // title bar fill            rgb(16,40,140)
#define UI_STATUS_BG     0x08C7   // title status box fill     rgb(8,24,60)
#define UI_PANEL_BG      0x08A8   // info panel fill           rgb(10,20,70)  (== COLOR_DARKER_BG)
#define UI_EDGE          0x2B7F   // bright-blue frame line    rgb(40,110,255)
#define UI_EDGE_LT       0x7E5F   // light-cyan bevel line     rgb(120,200,255)
#define UI_LABEL         0x9EDF   // section-label light blue  rgb(155,220,255) (== COLOR_GRAY_TEXT)
#define UI_BTN           0x1A9B   // button blue               rgb(24,80,220)  (== COLOR_BLUE)
#define UI_BTN_GREEN     0x2E46   // go / home / run           rgb(40,200,50)
#define UI_BTN_GREEN_LT  0xA7F4
#define UI_BTN_RED       0xD945   // stop                      rgb(220,40,40)
#define UI_BTN_YELLOW    0xEEC0   // selected                  rgb(235,220,0)  (== COLOR_ORANGE)
#define UI_BTN_YELLOW_LT 0xFFF6
#define UI_TEXT_DARK     0x0886   // dark text on yellow
#define UI_BTN_DIM       0x198D   // unlit / inactive fills    rgb(30,50,110)  (== COLOR_BUTTON_GRAY)
#define UI_FIELD_BG      0x0045   // tap-to-edit field (inset) rgb(4,10,40)
#define UI_UNIT          0xD71E   // unit suffixes (mm, %)     rgb(215,225,240)
// Probe routines — one hue each, clear of the yellow that means "selected".
#define UI_RT_CORNER     0x0512   // XYZ Corner: teal          rgb(0,160,150)
#define UI_RT_BORE       0x147D   // Bore: sky blue            rgb(20,140,235)
#define UI_RT_BOSS       0x8219   // Boss: violet              rgb(135,65,205)

// ---- Icons: 16x16, one uint16_t per row, MSB = leftmost pixel ----
extern const uint16_t UI_ICON_JOG[16], UI_ICON_WORK[16], UI_ICON_GAUGE[16], UI_ICON_SPINDLE[16],
                      UI_ICON_MACRO[16], UI_ICON_SD[16], UI_ICON_PROBE[16], UI_ICON_STATUS[16],
                      UI_ICON_TARGET[16], UI_ICON_LINK[16], UI_ICON_INFO[16], UI_ICON_ZSURF[16],
                      UI_ICON_CORNER[16], UI_ICON_BORE[16], UI_ICON_BOSS[16], UI_ICON_PLUG[16];
void uiDrawIcon(LovyanGFX* g, int x, int y, const uint16_t* icon, uint16_t color);

// ---- Text ----
// Replaces setTextSize(): 1 = GLCD 5x7 (dense small print), 2/3/4 =
// FreeSansBold 9/12/18pt.  The heights match the old GLCD sizes within a pixel
// (16/24/32 vs 18/23/33), so layouts built for the GLCD font still hold.
void uiText(LovyanGFX* g, int size);
// Font2 (LovyanGFX's 16 px proportional font) for compact text.
void uiFont2(LovyanGFX* g);
// Cap-height box of the current font, as offsets below the cursor y.
void uiCapBox(LovyanGFX* g, int& top, int& bottom);
// Cursor y that centres the current font's capitals on cy (centre on the CAP
// height, not fontHeight(), which includes descender space and so sits
// caps-only labels visibly high).
int  uiCenterCursorY(LovyanGFX* g, float cy);
// Print n lines centred as a block on cy, each centred across [x0, x0+w).
void uiPrintBlock(LovyanGFX* g, int x0, int w, float cy, const char* const* lines, int n);
// Font2 drawn twice, 1 px apart — a bold Font2.
void uiPrintBold2(LovyanGFX* g, int x, int y, const char* s);

// ---- Colour ----
uint16_t uiMix(uint16_t a, uint16_t b, float t);   // RGB565 a..b, t in 0..1

// ---- Buttons ----
struct UiBtnColors { uint16_t fill, edge, text; };
UiBtnColors uiButtonColors(uint16_t bg, uint16_t fg);
// Bevelled frame: vertical gradient fill + top highlight + outline + bottom shade.
void uiButtonFrame(LovyanGFX* g, int x, int y, int w, int h, const UiBtnColors& c);
// Button with 1+ lines (FreeSansBold at textSize >= 2) and an optional icon to
// the left of the label.  Falls back to no icon, then GLCD, if it won't fit.
void uiDrawButton(int x, int y, int w, int h, const char* const* lines, int n,
                  uint16_t bg, uint16_t fg, int textSize, const uint16_t* icon = nullptr);
// Narrow button whose label won't fit in FreeSansBold: bold Font2, centred.
void uiDrawNarrowButton(int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t fg);

// ---- Panels / fields ----
// Navy panel with a blue outline.  Clears its bounding box to black first, so
// it is safe to call into the shared panel scratch sprite (rounded corners
// would otherwise keep the previous panel's pixels).
void uiPanel(LovyanGFX* g, int x, int y, int w, int h, int r = 5);
// A field's value and unit on one line in Font2 ("150 mm/m") — value in its
// colour, unit near-white — caps centred on cy.  centre=false left-aligns at x0.
void uiFieldValue(LovyanGFX* g, int x0, int w, float cy, const char* value, const char* unit,
                  uint16_t valColor, bool centre);

// ---- Title bar ----
void uiDrawTitle(const char* title);
void uiDrawTitleStatus();   // just the right-hand status box (signal/battery or plug)
