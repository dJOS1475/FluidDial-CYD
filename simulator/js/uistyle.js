/*
 * uistyle.js — the v2.4.0 look (port of src/screens/ui_style.cpp): framed
 * title bar with a per-screen icon and status box, bevelled 3D buttons with
 * icons, FreeSansBold text, outlined navy panels.  Keep the two in step.
 *
 * The firmware swaps setTextSize() for uiText(), which selects FreeSansBold at
 * sizes 2-4; the sim models that in the renderer (lgfx.js, UI_THEME.autoFont),
 * so the screen ports keep their setTextSize() calls.  The palette itself is
 * just the COLOR_* values, regenerated from cnc_pendant_config.h by sync.py.
 *
 * Icons are 16x16 1-bit bitmaps; in the firmware each row is a uint16_t,
 * generated from the art below.
 */

// The sim used to carry a Classic/New switch while the look was prototyped;
// the firmware now has only this style.  Kept as a function so the screen
// ports' remaining uiNew() branches read naturally.
function uiNew() { return true; }

// Renderer theme (see lgfx.js): GLCD text at size 2/3/4 renders in
// FreeSansBold 9/12/18pt, exactly as the firmware's uiText() does.
function uiThemeFor() {
  return { map: {}, autoFont: uiFontFor };
}

// ---- Palette (RGB565) ----
const UI_TITLE_BG   = 0x1151;   // title bar fill            rgb(16,40,140)
const UI_STATUS_BG  = 0x08C7;   // title status box fill     rgb(8,24,60)
const UI_PANEL_BG   = 0x08A8;   // info panel fill           rgb(10,20,70)
const UI_EDGE       = 0x2B7F;   // bright-blue frame line    rgb(40,110,255)
const UI_EDGE_LT    = 0x7E5F;   // light-cyan bevel line     rgb(120,200,255)
const UI_LABEL      = 0x9EDF;   // section-label light blue  rgb(155,220,255)
const UI_BTN        = 0x1A9B;   // button blue               rgb(24,80,220)
const UI_BTN_GREEN  = 0x2E46;   // go / home / run           rgb(40,200,50)
const UI_BTN_GREEN_LT = 0xA7F4;
const UI_BTN_RED    = 0xD945;   // stop                      rgb(220,40,40)
const UI_BTN_YELLOW = 0xEEC0;   // selected                  rgb(235,220,0)
const UI_BTN_YELLOW_LT = 0xFFF6;
const UI_TEXT_DARK  = 0x0886;   // dark text on yellow
const UI_BTN_DIM    = 0x198D;   // unlit / inactive fills    rgb(30,50,110)
// Probe routine colours — one hue per routine so they can be told apart at a
// glance, picked to sit beside the theme blue / green and stay clear of the
// yellow that means "selected".
const UI_RT_CORNER  = 0x0512;   // XYZ Corner: teal         rgb(0,160,150)
const UI_RT_BORE    = 0x147D;   // Bore: sky blue           rgb(20,140,235)
const UI_RT_BOSS    = 0x8219;   // Boss: violet             rgb(135,65,205)
const UI_UNIT       = 0xD71E;   // unit suffixes (mm, %)    rgb(215,225,240)
const UI_FIELD_BG   = 0x0045;   // tap-to-edit field (inset) rgb(4,10,40)

// Palette colours the screens pass -> button fill + bevel colour.
// Selected (orange) becomes yellow as in the reference; the colour groups the
// classic menu used (blue / teal / indigo) all become one button blue.
function uiButtonColors(bg, fg) {
  // BUTTON_GRAY means "unselected" on most screens (drawn blue) but,
  // with grey text, "disabled" (e.g. the SD footer on a single page): keep muted.
  if (fg === COLOR_GRAY_TEXT && (bg === COLOR_BUTTON_GRAY || bg === COLOR_DARKER_BG))
    return { fill: UI_PANEL_BG, edge: UI_EDGE, text: fg };
  // Some go-buttons pass green text on dark green (Probe Config's
  // Save); on the bright New green that text vanishes, so use white.
  if ((bg === COLOR_DARK_GREEN || bg === COLOR_GREEN) && fg === COLOR_GREEN)
    return { fill: UI_BTN_GREEN, edge: UI_BTN_GREEN_LT, text: COLOR_WHITE };
  switch (bg) {
    case COLOR_DARK_GREEN: case COLOR_GREEN:
      return { fill: UI_BTN_GREEN, edge: UI_BTN_GREEN_LT, text: fg };
    case COLOR_RED:
      return { fill: UI_BTN_RED, edge: COLOR_YELLOW, text: fg };
    case COLOR_ORANGE: case COLOR_YELLOW:
      return { fill: UI_BTN_YELLOW, edge: UI_BTN_YELLOW_LT, text: fg === COLOR_WHITE ? UI_TEXT_DARK : fg };
    case COLOR_BLUE: case COLOR_TEAL: case COLOR_TEAL_BRIGHT: case COLOR_INDIGO:
    case COLOR_BUTTON_ACTIVE: case COLOR_BUTTON_GRAY:
      return { fill: UI_BTN, edge: UI_EDGE_LT, text: fg };
    case COLOR_DARKER_BG:                               // readout boxes: panel look
      return { fill: UI_PANEL_BG, edge: UI_EDGE, text: fg };
    default:   // any other colour: its own lighter tint as the bevel edge
      return { fill: bg, edge: uiMix(bg, COLOR_WHITE, 0.55), text: fg };
  }
}

// textSize -> GFX font (firmware uiText()).  Size 1 stays on the 5x7 GLCD font: it is
// used for dense small print where a proportional font would not fit.
function uiFontFor(textSize) {
  if (textSize >= 4) return GFX_FONTS.FreeSansBold18pt7b;
  if (textSize === 3) return GFX_FONTS.FreeSansBold12pt7b;
  if (textSize === 2) return GFX_FONTS.FreeSansBold9pt7b;
  return null;
}
function uiSetFont(g, textSize) {
  const f = uiFontFor(textSize);
  g.setFont(f);
  g.setTextSize(f ? 1 : textSize);
}

// ---- Icons: 16x16, '#' = lit ----
const UI_ICONS = {
  jog: [
    ".......#........",
    "......###.......",
    ".....#####......",
    ".......#........",
    ".......#........",
    "..#....#....#...",
    ".##....#....##..",
    "###############.",
    ".##....#....##..",
    "..#....#....#...",
    ".......#........",
    ".......#........",
    ".....#####......",
    "......###.......",
    ".......#........",
    "................",
  ],
  work: [
    "..#.............",
    ".###............",
    "#.#.#...........",
    "..#.............",
    "..#..#######....",
    "..#..#.....#....",
    "..#..#.....#....",
    "..#..#.....#....",
    "..#..#.....#....",
    "..#..#######....",
    "..#.........#...",
    "..#..........#..",
    "..##############",
    ".............#..",
    "............#...",
    "................",
  ],
  gauge: [
    "................",
    ".....######.....",
    "...##......##...",
    "..#..........#..",
    ".#............#.",
    ".#.........##.#.",
    "#.........##...#",
    "#........##....#",
    "#.......##.....#",
    "#......###.....#",
    "#......###.....#",
    ".#............#.",
    ".#............#.",
    "..##........##..",
    "....########....",
    "................",
  ],
  spindle: [
    "..############..",
    "..#..........#..",
    "..#..........#..",
    "..############..",
    "....#......#....",
    "....########....",
    "......####......",
    ".......##.......",
    "......##........",
    ".......##.......",
    "........##......",
    ".......##.......",
    "......##........",
    ".......#........",
    "................",
    "................",
  ],
  macro: [
    "...########.....",
    "...#......##....",
    "...#......#.#...",
    "...#......####..",
    "...#.........#..",
    "...#.######..#..",
    "...#.........#..",
    "...#.######..#..",
    "...#.........#..",
    "...#.######..#..",
    "...#.........#..",
    "...#.####....#..",
    "...#.........#..",
    "...###########..",
    "................",
    "................",
  ],
  sd: [
    ".....#########..",
    "....#........#..",
    "...#..#.#.#..#..",
    "..#...#.#.#..#..",
    "..#...#.#.#..#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..#..........#..",
    "..############..",
    "................",
    "................",
  ],
  probe: [
    ".....######.....",
    ".....######.....",
    ".....######.....",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    "......####......",
    "......####......",
    ".......##.......",
    "................",
    "################",
    "................",
    "................",
  ],
  status: [
    "################",
    "#..............#",
    "#..............#",
    "#.......#......#",
    "#......#.#.....#",
    "#.##..#...#.##.#",
    "#...##.....##..#",
    "#..............#",
    "#..............#",
    "################",
    ".......##.......",
    ".......##.......",
    "....########....",
    "................",
    "................",
    "................",
  ],
  target: [
    ".......#........",
    ".....#####......",
    "...##..#..##....",
    "..#....#....#...",
    "..#....#....#...",
    ".#.....#.....#..",
    ".#.....#.....#..",
    "#######.#######.",
    ".#.....#.....#..",
    ".#.....#.....#..",
    "..#....#....#...",
    "..#....#....#...",
    "...##..#..##....",
    ".....#####......",
    ".......#........",
    "................",
  ],
  link: [
    "................",
    "....########....",
    "..##........##..",
    ".#............#.",
    "#....######....#",
    "...##......##...",
    "..#..........#..",
    ".....######.....",
    "....#......#....",
    "................",
    "......####......",
    "......####......",
    "......####......",
    "................",
    "................",
    "................",
  ],
  info: [
    ".....######.....",
    "...##......##...",
    "..#....##....#..",
    ".#.....##.....#.",
    ".#............#.",
    "#.....###......#",
    "#......##......#",
    "#......##......#",
    "#......##......#",
    "#......##......#",
    ".#.....##.....#.",
    ".#....####....#.",
    "..#..........#..",
    "...##......##...",
    ".....######.....",
    "................",
  ],
  // probe routines
  zsurf: [
    ".......##.......",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    "....########....",
    ".....######.....",
    "......####......",
    ".......##.......",
    "................",
    "################",
    "################",
    ".#..#..#..#..#..",
    "#..#..#..#..#..#",
    "................",
  ],
  corner: [
    ".........#......",
    ".........#......",
    ".........#......",
    ".......#.#.#....",
    "........###.....",
    ".........#......",
    "................",
    "......##########",
    "......##########",
    "..#...##########",
    "...#..##########",
    "#####.##########",
    "...#..##########",
    "..#...##########",
    "......##########",
    "................",
  ],
  bore: [
    ".....######.....",
    "...##......##...",
    "..#..........#..",
    ".#............#.",
    ".#............#.",
    "#..............#",
    "#..#........#..#",
    "#.############.#",
    "#..#........#..#",
    "#..............#",
    ".#............#.",
    ".#............#.",
    "..#..........#..",
    "...##......##...",
    ".....######.....",
    "................",
  ],
  boss: [
    "................",
    "................",
    "................",
    "......####......",
    ".....######.....",
    "..#..######..#..",
    "...#.######.#...",
    "####.######.####",
    "...#.######.#...",
    "..#..######..#..",
    ".....######.....",
    "......####......",
    "................",
    "################",
    "................",
    "................",
  ],
  // power plug, 14 wide — shown in the status box on a wired (UART) pendant
  plug: [
    "...#....#.......",
    "...#....#.......",
    ".##########.....",
    ".##########.....",
    ".#........#.....",
    ".#........#.....",
    "..#......#......",
    "...######.......",
    ".....##.........",
    ".....##.........",
    ".....##.........",
    ".....##.........",
    "................",
    "................",
    "................",
    "................",
  ],
};

function drawIcon(g, x, y, name, color) {
  const rows = UI_ICONS[name];
  if (!rows) return;
  for (let r = 0; r < 16; r++) {
    const row = rows[r];
    let c = 0;
    while (c < 16) {                      // draw horizontal runs, like the firmware will
      if (row[c] !== "#") { c++; continue; }
      let e = c;
      while (e < 16 && row[e] === "#") e++;
      g.drawFastHLine(x + c, y + r, e - c, color);
      c = e;
    }
  }
}

// Title text -> icon in the title bar's left box.
function uiTitleIcon(title) {
  const t = title.toUpperCase();
  if (t === "MAIN MENU") return "target";
  if (t.startsWith("JOG")) return "jog";
  if (t === "WORK AREA") return "work";
  if (t.startsWith("FEEDS")) return "gauge";
  if (t.startsWith("SPINDLE")) return "spindle";
  if (t === "MACROS") return "macro";
  if (t === "SD CARD") return "sd";
  if (t === "STATUS") return "status";
  if (t === "CONNECTION" || t.startsWith("ESP-NOW") || t === "MACHINES") return "link";
  if (t === "FLUIDNC") return "info";
  return "probe";                          // PROBE, Z SURFACE, XYZ CORNER, BORE, BOSS, PROBE CONFIG
}

// ---- Title bar ----
//  [icon] | TITLE ............ [signal battery]   (wireless)
//  [icon] | TITLE ...................... [plug]   (wired UART)
const UI_TITLE_H = 34;
const UI_SHORT_TITLES = {
  "SPINDLE CONTROL": "SPINDLE",
  "FEEDS & SPEEDS": "FEED/SPEED",
  "PROBE CONFIG": "CONFIG",
  "ESP-NOW PAIRING": "PAIRING",
};
function uiStatusBoxW() { return comms_active_mode() === COMMS_MODE_UART ? 28 : 48; }

function uiDrawTitle(title) {
  const g = display;
  g.fillRect(0, 0, 240, UI_TITLE_H + 2, COLOR_BACKGROUND);
  uiShadedRoundRect(g, 1, 1, 238, UI_TITLE_H - 1, 7, UI_TITLE_BG);   // same 3D shade as the buttons
  g.drawRoundRect(1, 1, 238, UI_TITLE_H - 1, 7, UI_EDGE);      // double outline:
  g.drawRoundRect(2, 2, 236, UI_TITLE_H - 3, 6, UI_EDGE_LT);   // blue, then light bevel

  // left icon box + divider
  g.drawRoundRect(5, 6, 22, 23, 4, UI_EDGE_LT);
  drawIcon(g, 8, 10, uiTitleIcon(title), COLOR_WHITE);
  g.drawFastVLine(30, 6, 23, UI_EDGE_LT);

  // centred title in the space between divider and status box.  A few classic
  // titles are too wide for FreeSansBold9pt beside the status box, so the New
  // style uses a short form; the icon carries the rest of the meaning.
  title = UI_SHORT_TITLES[title] || title;
  const sbw = uiStatusBoxW();
  const left = 31, right = 234 - sbw - 1;          // clear space between divider and box
  const MARGIN = 4;                                 // minimum gap either side of the text
  g.setFont(GFX_FONTS.FreeSansBold9pt7b); g.setTextSize(1);
  let tw = g.textWidth(title);
  if (tw > right - left - 2 * MARGIN) { g.setFont("glcd"); tw = g.textWidth(title); }   // last resort
  g.setTextColor(COLOR_WHITE);
  g.setCursor(left + ((right - left - tw) / 2 | 0), uiCenterCursorY(g, 1 + UI_TITLE_H / 2));
  g.print(title);
  g.setFont(null);

  uiDrawTitleStatus();
}

// The status box is redrawn on its own every tick (signal / battery changes).
function uiDrawTitleStatus() {
  const g = display;
  const sbw = uiStatusBoxW();
  const x = 234 - sbw, y = 6, h = 23;
  // Only the box's own footprint is redrawn: a transport change repaints the
  // whole screen, so there is never a wider old box to clear (and clearing a
  // fixed-width strip would cut into a long title on UART).
  // Background behind the box's rounded corners follows the title bar's shade.
  for (let r = 0; r < h; r++) g.drawFastHLine(x, y + r, sbw, uiShadeRow(UI_TITLE_BG, y + r - 1, UI_TITLE_H - 1));
  g.fillRoundRect(x, y, sbw, h, 4, UI_STATUS_BG);
  g.drawRoundRect(x, y, sbw, h, 4, UI_EDGE_LT);
  if (comms_active_mode() === COMMS_MODE_UART) {
    drawIcon(g, x + 8, y + 6, "plug", UI_LABEL);
    return;
  }
  // signal bars (4 ascending, as drawLinkIcon)
  let bars = pendantMachine.linkSignalBars;
  if (bars < 0) bars = 0;
  const live = bars >= 3 ? COLOR_GREEN : bars >= 2 ? COLOR_ORANGE : bars >= 1 ? COLOR_RED : COLOR_GRAY_TEXT;
  const bx = x + 4, by = y + 5;   // 15 wide
  for (let i = 0; i < 4; i++) {
    const bh = 3 + i * 3;
    g.fillRect(bx + i * 4, by + 12 - bh, 3, bh, i < bars ? live : COLOR_BUTTON_GRAY);
  }
  // battery, no percentage number
  const pct = pendantMachine.batteryPercent;
  if (pct < 0) return;
  const ox = x + 22, oy = y + 6;   // 24 wide incl. nub
  const fg = pct > 50 ? COLOR_GREEN : pct > 20 ? COLOR_ORANGE : COLOR_RED;
  g.drawRoundRect(ox, oy, 22, 11, 2, COLOR_GRAY_TEXT);
  g.fillRect(ox + 22, oy + 3, 2, 5, COLOR_GRAY_TEXT);
  const fillW = (18 * pct) / 100 | 0;
  if (fillW > 0) g.fillRect(ox + 2, oy + 2, fillW, 7, fg);
  if (pendantMachine.batteryCharging) drawChargeBolt(ox + 8, oy + 1);
}

// ---- Vertical text centring ----
// Centre on the CAPITAL height, not fontHeight(): fontHeight includes the
// descender space, so caps-only labels (X / Y / Z, ALL, MAIN MENU) would sit
// visibly high.  Returns [capTop, capBottom] below the cursor y for the
// current effective font (GFX, Font2 or GLCD), measured from the 'H' glyph.
function uiCapBox(g) {
  const t = g._tf(), f = t.font, s = t.size;
  if (!f) return [0, 7 * s];                         // GLCD: caps are rows 0..6
  if (f.bmp) {
    if (!f._cap) {
      const i = 0x48 - 0x20, w = f.widths[i], bpr = (w + 6) >> 3, off = f.offsets[i];
      let top = -1, bot = -1;
      for (let r = 0; r < f.height; r++) {
        let ink = false;
        for (let b = 0; b < bpr; b++) if (f._bits[off + r * bpr + b]) ink = true;
        if (ink) { if (top < 0) top = r; bot = r + 1; }
      }
      f._cap = [top, bot];
    }
    return [f._cap[0] * s, f._cap[1] * s];
  }
  const H = f.glyphs[0x48 - f.first];
  const top = f._ascent + H[5];
  return [top * s, (top + H[2]) * s];
}
// Cursor y that puts the caps of a single line centred on cy.
function uiCenterCursorY(g, cy) {
  const [a, b] = uiCapBox(g);
  return Math.round(cy - (a + b) / 2);
}
// Lines centred as a block on cy (caps-to-caps), each centred horizontally in
// [x0, x0 + w).
function uiPrintBlock(g, x0, w, cy, lines) {
  const [a, b] = uiCapBox(g), capH = b - a;
  const pitch = capH + Math.max(4, Math.round(capH * 0.45));
  const blockH = capH + pitch * (lines.length - 1);
  let top = Math.round(cy - blockH / 2);
  for (const ln of lines) {
    g.setCursor(x0 + ((w - g.textWidth(ln)) / 2 | 0), top - a);
    g.print(ln);
    top += pitch;
  }
}

// ---- Buttons ----
// 3D (bevelled) buttons, used for every button (set uiButton3D false to
// compare with the flat look).  The fill is a vertical gradient drawn
// one row at a time (light at the top, the base colour in the middle, darker at
// the bottom) with a bright highlight line under the top edge: h drawFastHLine
// calls per button, cheap on the device.
let uiButton3D = true;

function uiRgb(c)  { return [((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31]; }
function uiPack(r, g, b) {
  r = Math.max(0, Math.min(255, Math.round(r))); g = Math.max(0, Math.min(255, Math.round(g))); b = Math.max(0, Math.min(255, Math.round(b)));
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
function uiMix(a, b, t) {               // RGB565 a..b, t in 0..1
  const A = uiRgb(a), B = uiRgb(b);
  return uiPack(A[0] + (B[0] - A[0]) * t, A[1] + (B[1] - A[1]) * t, A[2] + (B[2] - A[2]) * t);
}

// The 3D shade: lighter at the top, the base colour 45% down, darker at the
// bottom (firmware shadeRow()).  Shared by buttons and the title bar.
function uiShadeRow(fill, row, h) {
  const t = h > 1 ? row / (h - 1) : 0;
  return t < 0.45 ? uiMix(uiMix(fill, COLOR_WHITE, 0.30), fill, t / 0.45)
                  : uiMix(fill, uiMix(fill, COLOR_BACKGROUND, 0.40), (t - 0.45) / 0.55);
}
// Rounded rect filled with the 3D shade, one drawFastHLine per row.
function uiShadedRoundRect(g, x, y, w, h, r, fill) {
  for (let row = 0; row < h; row++) {
    let inset = 0;   // horizontal inset of the rounded corners on this row
    const dy = row < r ? r - row - 0.5 : (row >= h - r ? row - (h - r) + 0.5 : -1);
    if (dy >= 0) inset = r - Math.floor(Math.sqrt(Math.max(0, r * r - dy * dy)));
    g.drawFastHLine(x + inset, y + row, w - 2 * inset, uiShadeRow(fill, row, h));
  }
}

function uiButtonFrame3D(x, y, w, h, c) {
  const g = display, r = 7;
  uiShadedRoundRect(g, x, y, w, h, r, c.fill);
  g.drawFastHLine(x + r, y + 2, w - 2 * r, uiMix(c.fill, COLOR_WHITE, 0.55));   // top highlight
  g.drawRoundRect(x, y, w, h, r, c.edge);                                     // light outline
  g.drawFastHLine(x + r, y + h - 2, w - 2 * r, uiMix(c.fill, COLOR_BACKGROUND, 0.6)); // bottom shade
}

function uiButtonFrame(x, y, w, h, c) {
  if (uiButton3D) { uiButtonFrame3D(x, y, w, h, c); return; }
  const g = display;
  g.fillRoundRect(x, y, w, h, 7, c.fill);
  g.drawRoundRect(x, y, w, h, 7, c.edge);           // light outline = the bevel
  g.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 6, c.fill === UI_BTN ? UI_EDGE : c.fill);
}

// lines: a string or [line1, line2]; icon: UI_ICONS key or null.
function uiDrawButton(x, y, w, h, lines, bg, fg, textSize, icon) {
  const g = display;
  const c = uiButtonColors(bg, fg);
  uiButtonFrame(x, y, w, h, c);
  if (!Array.isArray(lines)) lines = [String(lines)];
  uiSetFont(g, textSize);
  const widest = () => Math.max(...lines.map((ln) => g.textWidth(ln)));
  // with an icon, the label centres in the space to its right.  Fit fallbacks:
  // drop the icon, then drop to the GLCD font at the classic size.
  let tx0 = x, tw0 = w;
  if (icon && widest() <= w - 29) {
    drawIcon(g, x + 9, y + ((h - 16) / 2 | 0), icon, c.text);
    tx0 = x + 27; tw0 = w - 29;
  } else if (g._font && widest() > w - 6) {
    g.setFont("glcd"); g.setTextSize(textSize);
  }
  g.setTextColor(c.text);
  uiPrintBlock(g, tx0, tw0, y + h / 2, lines);
  g.setFont(null);
}

// Narrow button whose label won't fit in FreeSansBold: Font2, made bold by
// drawing it twice 1 px apart (as the Jog MPos/WPos captions), centred.
function uiDrawNarrowButton(x, y, w, h, label, bg, fg) {
  const g = display;
  const c = uiButtonColors(bg, fg);
  uiButtonFrame(x, y, w, h, c);
  g.setFont(GFX_FONTS.Font2); g.setTextSize(1); g.setTextColor(c.text);
  const tx = x + ((w - (g.textWidth(label) + 1)) / 2 | 0), ty = uiCenterCursorY(g, y + h / 2);
  g.setCursor(tx, ty); g.print(label);
  g.setCursor(tx + 1, ty); g.print(label);
  g.setFont(null);
}

// Navy panel with a blue outline (firmware uiPanel()).  Clears its box to black
// first, as the firmware must when drawing into the shared panel sprite.
function uiPanel(g, x, y, w, h, r = 5) {
  if (r < 5) r = 5;
  g.fillRect(x, y, w, h, COLOR_BACKGROUND);
  g.fillRoundRect(x, y, w, h, r, UI_PANEL_BG);
  g.drawRoundRect(x, y, w, h, r, UI_EDGE);
}
// The larger framed panels (Main Menu status, Jog DRO, Work Area) use r = 6.
function uiDrawPanel(x, y, w, h) { uiPanel(display, x, y, w, h, 6); }

let UI_THEME = uiThemeFor();
