/*
 * lgfx.js — a LovyanGFX-compatible drawing surface backed by a 240×320 canvas
 * rendered at 1:1 DEVICE pixels (the visible canvas is CSS-upscaled with
 * nearest-neighbour, so what you see is the panel's actual pixel grid).
 *
 * Fidelity choices that make hardware bugs reproduce in the sim:
 *   • No anti-aliasing — every primitive is rasterised with integer 1-px
 *     fillRects, exactly like the LCD.  fillRoundRect / drawRoundRect /
 *     circles use the Adafruit-GFX midpoint algorithms (the same shapes
 *     LovyanGFX produces), so corner pixels land where they do on the device.
 *   • Real GLCD 5×7 bitmap font (font.js) with the device's metrics:
 *     textWidth == nchars*6*size, fontHeight == 8*size, no wrapping.
 *   • Hard clipping at the 240×320 canvas edge — text/shapes that run off the
 *     panel get cut off here just as they would on the hardware.
 */

function rgb565ToCss(c) {
  c &= 0xffff;
  const r5 = (c >> 11) & 0x1f;
  const g6 = (c >> 5) & 0x3f;
  const b5 = c & 0x1f;
  const r = Math.round((r5 * 255) / 31);
  const g = Math.round((g6 * 255) / 63);
  const b = Math.round((b5 * 255) / 31);
  return `rgb(${r},${g},${b})`;
}

class LGFX {
  constructor(ctx, w, h) {
    this.ctx = ctx;
    this.W = w;
    this.H = h;
    this._textColor = COLOR_WHITE;
    this._textSize = 1;
    this._cx = 0;
    this._cy = 0;
    this._rotation = 0;
    ctx.imageSmoothingEnabled = false;
  }

  color565(r, g, b) {
    return (((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3)) & 0xffff;
  }
  setRotation(r) {
    this._rotation = r;
  }

  // ---- low-level pixel primitives (no AA) ----
  _px(x, y) {
    this.ctx.fillRect(x | 0, y | 0, 1, 1);
  }
  drawFastHLine(x, y, w, color) {
    if (color !== undefined) this.ctx.fillStyle = rgb565ToCss(color);
    if (w < 0) { x += w + 1; w = -w; }
    this.ctx.fillRect(x | 0, y | 0, w | 0, 1);
  }
  drawFastVLine(x, y, h, color) {
    if (color !== undefined) this.ctx.fillStyle = rgb565ToCss(color);
    if (h < 0) { y += h + 1; h = -h; }
    this.ctx.fillRect(x | 0, y | 0, 1, h | 0);
  }

  // ---- fills / rects ----
  fillScreen(color) {
    this.ctx.fillStyle = rgb565ToCss(color);
    this.ctx.fillRect(0, 0, this.W, this.H);
  }
  fillRect(x, y, w, h, color) {
    this.ctx.fillStyle = rgb565ToCss(color);
    this.ctx.fillRect(Math.round(x), Math.round(y), Math.round(w), Math.round(h));
  }
  drawRect(x, y, w, h, color) {
    this.ctx.fillStyle = rgb565ToCss(color);
    x = Math.round(x); y = Math.round(y); w = Math.round(w); h = Math.round(h);
    this.drawFastHLine(x, y, w);
    this.drawFastHLine(x, y + h - 1, w);
    this.drawFastVLine(x, y, h);
    this.drawFastVLine(x + w - 1, y, h);
  }

  drawLine(x0, y0, x1, y1, color) {
    this.ctx.fillStyle = rgb565ToCss(color);
    x0 = Math.round(x0); y0 = Math.round(y0); x1 = Math.round(x1); y1 = Math.round(y1);
    if (x0 === x1) { this.drawFastVLine(x0, Math.min(y0, y1), Math.abs(y1 - y0) + 1); return; }
    if (y0 === y1) { this.drawFastHLine(Math.min(x0, x1), y0, Math.abs(x1 - x0) + 1); return; }
    const steep = Math.abs(y1 - y0) > Math.abs(x1 - x0);
    if (steep) { [x0, y0] = [y0, x0]; [x1, y1] = [y1, x1]; }
    if (x0 > x1) { [x0, x1] = [x1, x0]; [y0, y1] = [y1, y0]; }
    const dx = x1 - x0, dy = Math.abs(y1 - y0);
    let err = dx / 2, ystep = y0 < y1 ? 1 : -1, y = y0;
    for (let x = x0; x <= x1; x++) {
      if (steep) this._px(y, x); else this._px(x, y);
      err -= dy;
      if (err < 0) { y += ystep; err += dx; }
    }
  }

  // ---- circle helpers (Adafruit GFX) ----
  _drawCircleHelper(x0, y0, r, corner) {
    let f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
    while (x < y) {
      if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
      x++; ddF_x += 2; f += ddF_x;
      if (corner & 0x4) { this._px(x0 + x, y0 + y); this._px(x0 + y, y0 + x); }
      if (corner & 0x2) { this._px(x0 + x, y0 - y); this._px(x0 + y, y0 - x); }
      if (corner & 0x8) { this._px(x0 - y, y0 + x); this._px(x0 - x, y0 + y); }
      if (corner & 0x1) { this._px(x0 - y, y0 - x); this._px(x0 - x, y0 - y); }
    }
  }
  _fillCircleHelper(x0, y0, r, corners, delta) {
    let f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r, px = x, py = y;
    delta++;
    while (x < y) {
      if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
      x++; ddF_x += 2; f += ddF_x;
      if (x < y + 1) {
        if (corners & 1) this.drawFastVLine(x0 + x, y0 - y, 2 * y + delta);
        if (corners & 2) this.drawFastVLine(x0 - x, y0 - y, 2 * y + delta);
      }
      if (y !== py) {
        if (corners & 1) this.drawFastVLine(x0 + py, y0 - px, 2 * px + delta);
        if (corners & 2) this.drawFastVLine(x0 - py, y0 - px, 2 * px + delta);
        py = y;
      }
      px = x;
    }
  }
  fillCircle(x0, y0, r, color) {
    this.ctx.fillStyle = rgb565ToCss(color);
    x0 = Math.round(x0); y0 = Math.round(y0); r = Math.round(r);
    this.drawFastVLine(x0, y0 - r, 2 * r + 1);
    this._fillCircleHelper(x0, y0, r, 3, 0);
  }
  drawCircle(x0, y0, r, color) {
    this.ctx.fillStyle = rgb565ToCss(color);
    x0 = Math.round(x0); y0 = Math.round(y0); r = Math.round(r);
    let f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
    this._px(x0, y0 + r); this._px(x0, y0 - r); this._px(x0 + r, y0); this._px(x0 - r, y0);
    while (x < y) {
      if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
      x++; ddF_x += 2; f += ddF_x;
      this._px(x0 + x, y0 + y); this._px(x0 - x, y0 + y);
      this._px(x0 + x, y0 - y); this._px(x0 - x, y0 - y);
      this._px(x0 + y, y0 + x); this._px(x0 - y, y0 + x);
      this._px(x0 + y, y0 - x); this._px(x0 - y, y0 - x);
    }
  }

  drawEllipse(x0, y0, rx, ry, color) {
    this.ctx.strokeStyle = rgb565ToCss(color);
    this.ctx.lineWidth = 1;
    this.ctx.beginPath();
    this.ctx.ellipse(Math.round(x0) + 0.5, Math.round(y0) + 0.5,
                     Math.max(0, Math.round(rx)), Math.max(0, Math.round(ry)),
                     0, 0, Math.PI * 2);
    this.ctx.stroke();
  }

  // ---- rounded rects (Adafruit GFX) ----
  fillRoundRect(x, y, w, h, r, color) {
    this.ctx.fillStyle = rgb565ToCss(color);
    x = Math.round(x); y = Math.round(y); w = Math.round(w); h = Math.round(h); r = Math.round(r);
    const maxR = Math.min(w / 2, h / 2) | 0;
    if (r > maxR) r = maxR;
    this.ctx.fillRect(x + r, y, w - 2 * r, h);
    this._fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1);
    this._fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1);
  }
  drawRoundRect(x, y, w, h, r, color) {
    this.ctx.fillStyle = rgb565ToCss(color);
    x = Math.round(x); y = Math.round(y); w = Math.round(w); h = Math.round(h); r = Math.round(r);
    const maxR = Math.min(w / 2, h / 2) | 0;
    if (r > maxR) r = maxR;
    this.drawFastHLine(x + r, y, w - 2 * r);
    this.drawFastHLine(x + r, y + h - 1, w - 2 * r);
    this.drawFastVLine(x, y + r, h - 2 * r);
    this.drawFastVLine(x + w - 1, y + r, h - 2 * r);
    this._drawCircleHelper(x + r, y + r, r, 1);
    this._drawCircleHelper(x + w - r - 1, y + r, r, 2);
    this._drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4);
    this._drawCircleHelper(x + r, y + h - r - 1, r, 8);
  }

  // ---- text ----
  setTextColor(c) { this._textColor = c; }
  setTextSize(s) { this._textSize = s; }
  setCursor(x, y) { this._cx = x | 0; this._cy = y | 0; }
  fontHeight() { return 8 * this._textSize; }
  textWidth(s) { return String(s).length * 6 * this._textSize; }

  print(value, decimals) {
    let s;
    if (decimals !== undefined && typeof value === "number") s = value.toFixed(decimals);
    else s = String(value);
    this._drawText(s);
  }

  printf(fmt, ...args) {
    let i = 0;
    const out = String(fmt).replace(/%(?:\.(\d+))?[dufsx]|%%/g, (m, prec) => {
      if (m === "%%") return "%";
      const a = args[i++];
      if (m.endsWith("f")) return Number(a).toFixed(prec !== undefined ? +prec : 6);
      if (m.endsWith("x")) return Number(a).toString(16);
      return String(a);
    });
    this._drawText(out);
  }

  _drawText(s) {
    const size = this._textSize;
    const ctx = this.ctx;
    ctx.fillStyle = rgb565ToCss(this._textColor);
    for (let k = 0; k < s.length; k++) {
      let code = s.charCodeAt(k);
      if (code > 255) code = 63; // '?'
      const base = code * 5;
      const gx = this._cx + k * 6 * size;
      for (let col = 0; col < 5; col++) {
        const bits = GLCD_FONT[base + col];
        for (let row = 0; row < 8; row++) {
          if (bits & (1 << row)) {
            ctx.fillRect(gx + col * size, this._cy + row * size, size, size);
          }
        }
      }
    }
    this._cx += s.length * 6 * size;
  }
}
