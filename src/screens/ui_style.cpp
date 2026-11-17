// ui_style.cpp — see ui_style.h.  Ported from simulator/js/uistyle.js.
#include "ui_style.h"
#include "../Comms.h"
#include <math.h>

// ===== Icons (generated from the simulator's UI_ICONS pixel art) =====
const uint16_t UI_ICON_JOG[16] = { 0x0100, 0x0380, 0x07C0, 0x0100, 0x0100, 0x2108, 0x610C, 0xFFFE, 0x610C, 0x2108, 0x0100, 0x0100, 0x07C0, 0x0380, 0x0100, 0x0000 };
const uint16_t UI_ICON_WORK[16] = { 0x2000, 0x7000, 0xA800, 0x2000, 0x27F0, 0x2410, 0x2410, 0x2410, 0x2410, 0x27F0, 0x2008, 0x2004, 0x3FFF, 0x0004, 0x0008, 0x0000 };
const uint16_t UI_ICON_GAUGE[16] = { 0x0000, 0x07E0, 0x1818, 0x2004, 0x4002, 0x401A, 0x8031, 0x8061, 0x80C1, 0x81C1, 0x81C1, 0x4002, 0x4002, 0x300C, 0x0FF0, 0x0000 };
const uint16_t UI_ICON_SPINDLE[16] = { 0x3FFC, 0x2004, 0x2004, 0x3FFC, 0x0810, 0x0FF0, 0x03C0, 0x0180, 0x0300, 0x0180, 0x00C0, 0x0180, 0x0300, 0x0100, 0x0000, 0x0000 };
const uint16_t UI_ICON_MACRO[16] = { 0x1FE0, 0x1030, 0x1028, 0x103C, 0x1004, 0x17E4, 0x1004, 0x17E4, 0x1004, 0x17E4, 0x1004, 0x1784, 0x1004, 0x1FFC, 0x0000, 0x0000 };
const uint16_t UI_ICON_SD[16] = { 0x07FC, 0x0804, 0x12A4, 0x22A4, 0x22A4, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x3FFC, 0x0000, 0x0000 };
const uint16_t UI_ICON_PROBE[16] = { 0x07E0, 0x07E0, 0x07E0, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x03C0, 0x03C0, 0x0180, 0x0000, 0xFFFF, 0x0000, 0x0000 };
const uint16_t UI_ICON_STATUS[16] = { 0xFFFF, 0x8001, 0x8001, 0x8081, 0x8141, 0xB22D, 0x8C19, 0x8001, 0x8001, 0xFFFF, 0x0180, 0x0180, 0x0FF0, 0x0000, 0x0000, 0x0000 };
const uint16_t UI_ICON_TARGET[16] = { 0x0100, 0x07C0, 0x1930, 0x2108, 0x2108, 0x4104, 0x4104, 0xFEFE, 0x4104, 0x4104, 0x2108, 0x2108, 0x1930, 0x07C0, 0x0100, 0x0000 };
const uint16_t UI_ICON_LINK[16] = { 0x0000, 0x0FF0, 0x300C, 0x4002, 0x87E1, 0x1818, 0x2004, 0x07E0, 0x0810, 0x0000, 0x03C0, 0x03C0, 0x03C0, 0x0000, 0x0000, 0x0000 };
const uint16_t UI_ICON_INFO[16] = { 0x07E0, 0x1818, 0x2184, 0x4182, 0x4002, 0x8381, 0x8181, 0x8181, 0x8181, 0x8181, 0x4182, 0x43C2, 0x2004, 0x1818, 0x07E0, 0x0000 };
const uint16_t UI_ICON_ZSURF[16] = { 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0FF0, 0x07E0, 0x03C0, 0x0180, 0x0000, 0xFFFF, 0xFFFF, 0x4924, 0x9249, 0x0000 };
const uint16_t UI_ICON_CORNER[16] = { 0x0040, 0x0040, 0x0040, 0x0150, 0x00E0, 0x0040, 0x0000, 0x03FF, 0x03FF, 0x23FF, 0x13FF, 0xFBFF, 0x13FF, 0x23FF, 0x03FF, 0x0000 };
const uint16_t UI_ICON_BORE[16] = { 0x07E0, 0x1818, 0x2004, 0x4002, 0x4002, 0x8001, 0x9009, 0xBFFD, 0x9009, 0x8001, 0x4002, 0x4002, 0x2004, 0x1818, 0x07E0, 0x0000 };
const uint16_t UI_ICON_BOSS[16] = { 0x0000, 0x0000, 0x0000, 0x03C0, 0x07E0, 0x27E4, 0x17E8, 0xF7EF, 0x17E8, 0x27E4, 0x07E0, 0x03C0, 0x0000, 0xFFFF, 0x0000, 0x0000 };
const uint16_t UI_ICON_PLUG[16] = { 0x1080, 0x1080, 0x7FE0, 0x7FE0, 0x4020, 0x4020, 0x2040, 0x1F80, 0x0600, 0x0600, 0x0600, 0x0600, 0x0000, 0x0000, 0x0000, 0x0000 };

void uiDrawIcon(LovyanGFX* g, int x, int y, const uint16_t* icon, uint16_t color) {
    if (!icon) return;
    for (int r = 0; r < 16; ++r) {
        uint16_t bits = icon[r];
        int c = 0;
        while (c < 16) {                         // horizontal runs, one call each
            if (!(bits & (0x8000 >> c))) { ++c; continue; }
            int e = c;
            while (e < 16 && (bits & (0x8000 >> e))) ++e;
            g->drawFastHLine(x + c, y + r, e - c, color);
            c = e;
        }
    }
}

// ===== Text =====
void uiText(LovyanGFX* g, int size) {
    switch (size) {
        case 1:  g->setFont(&fonts::Font0); g->setTextSize(1); return;
        case 2:  g->setFont(&fonts::FreeSansBold9pt7b); break;
        case 3:  g->setFont(&fonts::FreeSansBold12pt7b); break;
        default: g->setFont(size <= 0 ? (const lgfx::IFont*)&fonts::Font0 : &fonts::FreeSansBold18pt7b); break;
    }
    g->setTextSize(1);
}

void uiFont2(LovyanGFX* g) {
    g->setFont(&fonts::Font2);
    g->setTextSize(1);
}

// Cap-height boxes measured from each font's 'H' glyph (simulator uiCapBox()).
void uiCapBox(LovyanGFX* g, int& top, int& bottom) {
    const lgfx::IFont* f = g->getFont();
    if (f == &fonts::Font2)              { top = 3; bottom = 13; return; }
    if (f == &fonts::FreeSansBold9pt7b)  { top = 1; bottom = 14; return; }
    if (f == &fonts::FreeSansBold12pt7b) { top = 0; bottom = 18; return; }
    if (f == &fonts::FreeSansBold18pt7b) { top = 0; bottom = 26; return; }
    int s = (int)g->getTextSizeY();      // GLCD: caps are rows 0..6
    top = 0; bottom = 7 * s;
}

int uiCenterCursorY(LovyanGFX* g, float cy) {
    int a, b;
    uiCapBox(g, a, b);
    return (int)lroundf(cy - (a + b) / 2.0f);
}

void uiPrintBlock(LovyanGFX* g, int x0, int w, float cy, const char* const* lines, int n) {
    int a, b;
    uiCapBox(g, a, b);
    int capH  = b - a;
    int gap   = (int)lroundf(capH * 0.45f);
    int pitch = capH + (gap < 4 ? 4 : gap);
    int blockH = capH + pitch * (n - 1);
    int top = (int)lroundf(cy - blockH / 2.0f);
    for (int i = 0; i < n; ++i) {
        g->setCursor(x0 + (w - g->textWidth(lines[i])) / 2, top - a);
        g->print(lines[i]);
        top += pitch;
    }
}

void uiPrintBold2(LovyanGFX* g, int x, int y, const char* s) {
    g->setCursor(x, y);     g->print(s);
    g->setCursor(x + 1, y); g->print(s);
}

// ===== Colour =====
uint16_t uiMix(uint16_t a, uint16_t b, float t) {
    float ar = ((a >> 11) & 31) * 255.0f / 31, ag = ((a >> 5) & 63) * 255.0f / 63, ab = (a & 31) * 255.0f / 31;
    float br = ((b >> 11) & 31) * 255.0f / 31, bg = ((b >> 5) & 63) * 255.0f / 63, bb = (b & 31) * 255.0f / 31;
    int r  = (int)lroundf(ar + (br - ar) * t);
    int gg = (int)lroundf(ag + (bg - ag) * t);
    int bl = (int)lroundf(ab + (bb - ab) * t);
    r  = r < 0 ? 0 : r > 255 ? 255 : r;
    gg = gg < 0 ? 0 : gg > 255 ? 255 : gg;
    bl = bl < 0 ? 0 : bl > 255 ? 255 : bl;
    return (uint16_t)(((r >> 3) << 11) | ((gg >> 2) << 5) | (bl >> 3));
}

// ===== Buttons =====
// Screens pass the palette colours they always have; this maps each to its
// button treatment.  (Several COLOR_* values coincide in the new palette, so
// this is an if-chain, not a switch.)
UiBtnColors uiButtonColors(uint16_t bg, uint16_t fg) {
    // Grey text on a grey/panel button = disabled (e.g. SD footer on one page).
    if (fg == COLOR_GRAY_TEXT && (bg == COLOR_BUTTON_GRAY || bg == COLOR_DARKER_BG))
        return { UI_PANEL_BG, UI_EDGE, fg };
    if (bg == COLOR_DARK_GREEN || bg == COLOR_GREEN)
        // green-on-green (Probe Config's Save) would vanish: use white
        return { UI_BTN_GREEN, UI_BTN_GREEN_LT, fg == COLOR_GREEN ? (uint16_t)COLOR_WHITE : fg };
    if (bg == COLOR_RED)
        return { UI_BTN_RED, COLOR_YELLOW, fg };
    if (bg == COLOR_ORANGE || bg == COLOR_YELLOW)          // selected
        return { UI_BTN_YELLOW, UI_BTN_YELLOW_LT, fg == COLOR_WHITE ? (uint16_t)UI_TEXT_DARK : fg };
    if (bg == COLOR_BLUE || bg == COLOR_TEAL || bg == COLOR_TEAL_BRIGHT || bg == COLOR_INDIGO ||
        bg == COLOR_BUTTON_ACTIVE || bg == COLOR_BUTTON_GRAY)   // unselected / nav
        return { UI_BTN, UI_EDGE_LT, fg };
    if (bg == COLOR_DARKER_BG)                              // readout boxes: panel look
        return { UI_PANEL_BG, UI_EDGE, fg };
    return { bg, uiMix(bg, COLOR_WHITE, 0.55f), fg };       // own tint as the edge
}

// The 3D shade: lighter at the top, the base colour 45% down, darker at the
// bottom.  row/h -> colour; shared by buttons and the title bar.
static uint16_t shadeRow(uint16_t fill, int row, int h) {
    float t = h > 1 ? (float)row / (h - 1) : 0;
    return t < 0.45f ? uiMix(uiMix(fill, COLOR_WHITE, 0.30f), fill, t / 0.45f)
                     : uiMix(fill, uiMix(fill, COLOR_BACKGROUND, 0.40f), (t - 0.45f) / 0.55f);
}

// Rounded rect filled with the 3D shade, one drawFastHLine per row.
static void shadedRoundRect(LovyanGFX* g, int x, int y, int w, int h, int r, uint16_t fill) {
    for (int row = 0; row < h; ++row) {
        // horizontal inset of the rounded corners on this row
        int   inset = 0;
        float dy    = row < r ? r - row - 0.5f : (row >= h - r ? row - (h - r) + 0.5f : -1.0f);
        if (dy >= 0) {
            float q = r * r - dy * dy;
            inset   = r - (int)floorf(sqrtf(q > 0 ? q : 0));
        }
        g->drawFastHLine(x + inset, y + row, w - 2 * inset, shadeRow(fill, row, h));
    }
}

void uiButtonFrame(LovyanGFX* g, int x, int y, int w, int h, const UiBtnColors& c) {
    const int r = 7;
    shadedRoundRect(g, x, y, w, h, r, c.fill);
    g->drawFastHLine(x + r, y + 2, w - 2 * r, uiMix(c.fill, COLOR_WHITE, 0.55f));            // top highlight
    g->drawRoundRect(x, y, w, h, r, c.edge);                                                 // light outline
    g->drawFastHLine(x + r, y + h - 2, w - 2 * r, uiMix(c.fill, COLOR_BACKGROUND, 0.6f));    // bottom shade
}

void uiDrawButton(int x, int y, int w, int h, const char* const* lines, int n,
                  uint16_t bg, uint16_t fg, int textSize, const uint16_t* icon) {
    LovyanGFX* g = &display;
    UiBtnColors c = uiButtonColors(bg, fg);
    uiButtonFrame(g, x, y, w, h, c);
    uiText(g, textSize);
    int widest = 0;
    for (int i = 0; i < n; ++i) { int tw = g->textWidth(lines[i]); if (tw > widest) widest = tw; }
    // With an icon the label centres in the space to its right.  Fit fallbacks:
    // drop the icon, then drop to the GLCD font at the requested size.
    int tx0 = x, tw0 = w;
    if (icon && widest <= w - 29) {
        uiDrawIcon(g, x + 9, y + (h - 16) / 2, icon, c.text);
        tx0 = x + 27; tw0 = w - 29;
    } else if (textSize >= 2 && widest > w - 6) {
        g->setFont(&fonts::Font0);
        g->setTextSize(textSize);
    }
    g->setTextColor(c.text);
    uiPrintBlock(g, tx0, tw0, y + h / 2.0f, lines, n);
    uiText(g, 1);
}

void uiDrawNarrowButton(int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t fg) {
    LovyanGFX* g = &display;
    UiBtnColors c = uiButtonColors(bg, fg);
    uiButtonFrame(g, x, y, w, h, c);
    uiFont2(g);
    g->setTextColor(c.text);
    uiPrintBold2(g, x + (w - (g->textWidth(label) + 1)) / 2, uiCenterCursorY(g, y + h / 2.0f), label);
    uiText(g, 1);
}

// ===== Panels / fields =====
void uiPanel(LovyanGFX* g, int x, int y, int w, int h, int r) {
    if (r < 5) r = 5;
    g->fillRect(x, y, w, h, COLOR_BACKGROUND);
    g->fillRoundRect(x, y, w, h, r, UI_PANEL_BG);
    g->drawRoundRect(x, y, w, h, r, UI_EDGE);
}

void uiFieldValue(LovyanGFX* g, int x0, int w, float cy, const char* value, const char* unit,
                  uint16_t valColor, bool centre) {
    uiFont2(g);
    bool hasUnit = unit && *unit;
    int sp = hasUnit ? g->textWidth(" ") : 0;
    int vw = g->textWidth(value), uw = hasUnit ? g->textWidth(unit) : 0;
    int x  = centre ? x0 + (w - (vw + sp + uw)) / 2 : x0;
    int y  = uiCenterCursorY(g, cy);
    g->setTextColor(valColor); g->setCursor(x, y); g->print(value);
    if (hasUnit) { g->setTextColor(UI_UNIT); g->setCursor(x + vw + sp, y); g->print(unit); }
    uiText(g, 1);
}

// ===== Title bar =====
//  [icon] | TITLE ............ [signal battery]   (wireless)
//  [icon] | TITLE ...................... [plug]   (wired UART)
static const int UI_TITLE_H = 34;

static const uint16_t* titleIcon(const char* t) {
    if (!strcmp(t, "MAIN MENU"))  return UI_ICON_TARGET;
    if (!strncmp(t, "JOG", 3))    return UI_ICON_JOG;
    if (!strcmp(t, "WORK AREA"))  return UI_ICON_WORK;
    if (!strncmp(t, "FEEDS", 5))  return UI_ICON_GAUGE;
    if (!strncmp(t, "SPINDLE", 7)) return UI_ICON_SPINDLE;
    if (!strcmp(t, "MACROS"))     return UI_ICON_MACRO;
    if (!strcmp(t, "SD CARD"))    return UI_ICON_SD;
    if (!strcmp(t, "STATUS"))     return UI_ICON_STATUS;
    if (!strcmp(t, "CONNECTION") || !strncmp(t, "ESP-NOW", 7) || !strcmp(t, "MACHINES")) return UI_ICON_LINK;
    if (!strcasecmp(t, "FLUIDNC")) return UI_ICON_INFO;
    return UI_ICON_PROBE;          // PROBE, Z SURFACE, XYZ CORNER, BORE, BOSS, PROBE CONFIG
}

// Titles too wide for FreeSansBold9pt beside the status box get a short form;
// the icon carries the rest of the meaning.
static const char* shortTitle(const char* t) {
    if (!strcmp(t, "SPINDLE CONTROL")) return "SPINDLE";
    if (!strcmp(t, "FEEDS & SPEEDS"))  return "FEED/SPEED";
    if (!strcmp(t, "PROBE CONFIG"))    return "CONFIG";
    if (!strcmp(t, "ESP-NOW PAIRING")) return "PAIRING";
    return t;
}

static int statusBoxW() { return comms_active_mode() == COMMS_MODE_UART ? 28 : 48; }

void uiDrawTitle(const char* title) {
    LovyanGFX* g = &display;
    g->fillRect(0, 0, 240, UI_TITLE_H + 2, COLOR_BACKGROUND);
    shadedRoundRect(g, 1, 1, 238, UI_TITLE_H - 1, 7, UI_TITLE_BG);   // same 3D shade as the buttons
    g->drawRoundRect(1, 1, 238, UI_TITLE_H - 1, 7, UI_EDGE);      // double outline:
    g->drawRoundRect(2, 2, 236, UI_TITLE_H - 3, 6, UI_EDGE_LT);   // blue, then light bevel

    g->drawRoundRect(5, 6, 22, 23, 4, UI_EDGE_LT);                // icon box + divider
    uiDrawIcon(g, 8, 10, titleIcon(title), COLOR_WHITE);
    g->drawFastVLine(30, 6, 23, UI_EDGE_LT);

    const char* t = shortTitle(title);
    const int left = 31, right = 234 - statusBoxW() - 1, MARGIN = 4;
    uiText(g, 2);
    int tw = g->textWidth(t);
    if (tw > right - left - 2 * MARGIN) { uiText(g, 1); tw = g->textWidth(t); }   // last resort
    g->setTextColor(COLOR_WHITE);
    g->setCursor(left + (right - left - tw) / 2, uiCenterCursorY(g, 1 + UI_TITLE_H / 2.0f));
    g->print(t);
    uiText(g, 1);

    uiDrawTitleStatus();
}

// Lightning-bolt charging glyph (~6w x 9h): yellow with a 1 px black outline.
static void drawChargeBolt(LovyanGFX* g, int x, int y) {
    static const uint8_t runs[9][3] = {   // {row, x-offset, width}
        {0, 4, 2}, {1, 3, 2}, {2, 2, 2}, {3, 1, 5},
        {4, 3, 2}, {5, 2, 2}, {6, 1, 2}, {7, 0, 2}, {8, 0, 1},
    };
    static const int ox[4] = { -1, 1, 0, 0 };
    static const int oy[4] = {  0, 0, -1, 1 };
    for (int d = 0; d < 4; ++d)
        for (int i = 0; i < 9; ++i)
            g->drawFastHLine(x + runs[i][1] + ox[d], y + runs[i][0] + oy[d], runs[i][2], COLOR_BACKGROUND);
    for (int i = 0; i < 9; ++i)
        g->drawFastHLine(x + runs[i][1], y + runs[i][0], runs[i][2], COLOR_YELLOW);
}

// Rendered into a small persistent sprite and pushed atomically, so the
// periodic refresh (every 100 ms from updateCurrentScreenSprites) never
// flickers.  Reads the CACHED pendantMachine.linkSignalBars (sampled on Core 0)
// and batteryPercent — no radio or ADC access from the UI core.
void uiDrawTitleStatus() {
    const int sbw = statusBoxW(), h = 23;
    const int sx = 234 - sbw, sy = 6;
    static LGFX_Sprite spr(&display);
    if (spr.getBuffer() && spr.width() != sbw) spr.deleteSprite();   // transport changed
    if (!spr.getBuffer()) {
        spr.setColorDepth(16);
        spr.createSprite(sbw, h);
    }
    LovyanGFX* g  = spr.getBuffer() ? (LovyanGFX*)&spr : (LovyanGFX*)&display;
    const int  x0 = spr.getBuffer() ? 0 : sx;
    const int  y0 = spr.getBuffer() ? 0 : sy;

    // Background behind the box's rounded corners follows the title bar's shade
    // (bar rows start at screen y 1, height UI_TITLE_H - 1).
    for (int r = 0; r < h; ++r)
        g->drawFastHLine(x0, y0 + r, sbw, shadeRow(UI_TITLE_BG, sy + r - 1, UI_TITLE_H - 1));
    g->fillRoundRect(x0, y0, sbw, h, 4, UI_STATUS_BG);
    g->drawRoundRect(x0, y0, sbw, h, 4, UI_EDGE_LT);

    if (comms_active_mode() == COMMS_MODE_UART) {
        uiDrawIcon(g, x0 + 8, y0 + 6, UI_ICON_PLUG, UI_LABEL);   // wired: no radio, no battery
    } else {
        int bars = pendantMachine.linkSignalBars;
        if (bars < 0) bars = 0;
        uint16_t live = bars >= 3 ? COLOR_GREEN : bars >= 2 ? COLOR_ORANGE : bars >= 1 ? COLOR_RED : COLOR_GRAY_TEXT;
        for (int i = 0; i < 4; ++i) {
            int bh = 3 + i * 3;
            g->fillRect(x0 + 4 + i * 4, y0 + 5 + 12 - bh, 3, bh, i < bars ? live : COLOR_BUTTON_GRAY);
        }
        int pct = pendantMachine.batteryPercent;
        if (pct >= 0) {                                   // battery, no percentage number
            int ox = x0 + 22, oy = y0 + 6;
            uint16_t fg = pct > 50 ? COLOR_GREEN : pct > 20 ? COLOR_ORANGE : COLOR_RED;
            g->drawRoundRect(ox, oy, 22, 11, 2, COLOR_GRAY_TEXT);
            g->fillRect(ox + 22, oy + 3, 2, 5, COLOR_GRAY_TEXT);
            int fillW = 18 * pct / 100;
            if (fillW > 0) g->fillRect(ox + 2, oy + 2, fillW, 7, fg);
            if (pendantMachine.batteryCharging) drawChargeBolt(g, ox + 8, oy + 1);
        }
    }
    if (spr.getBuffer()) spr.pushSprite(sx, sy);
}
