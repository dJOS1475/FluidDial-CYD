/* Shared button-row layout (mirrors pendant_shared.h) — evenly spans n buttons
   across the standard 5..235 content width with 2 px gaps, so every button row on
   every screen ends flush on both sides and the rows line up with each other.  The
   last slot absorbs any rounding remainder, so odd counts still finish at 235.
     drawButton(rowBtnX(n, i), y, rowBtnWAt(n, i), h, ...)  */
const ROW_X0 = 5, ROW_W = 230, ROW_GAP = 2;
function rowBtnW(n) { return ((ROW_W - (n - 1) * ROW_GAP) / n) | 0; }
function rowBtnX(n, i) { return ROW_X0 + i * (rowBtnW(n) + ROW_GAP); }
function rowBtnWAt(n, i) { return i === n - 1 ? ROW_X0 + ROW_W - rowBtnX(n, i) : rowBtnW(n); }

/*
 * helpers.js — shared UI helpers, ported from CNC_Pendant_UI.cpp and
 * screen_probe.cpp.  Names and signatures match the firmware so the screen
 * ports read like the originals.
 */

// printf-style float formatting used throughout the probe screens
function fmtF(v, dec) {
  return Number(v).toFixed(dec);
}

function isTouchInBounds(tx, ty, x, y, w, h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

function drawRoundRect(x, y, w, h, r, color) {
  display.fillRoundRect(x, y, w, h, r, color);
}

function drawButton(x, y, w, h, text, bgColor, textColor, textSize = 2) {
  drawRoundRect(x, y, w, h, 8, bgColor);
  display.setTextColor(textColor);
  display.setTextSize(textSize);
  const tw = display.textWidth(String(text));
  const th = display.fontHeight();
  display.setCursor(x + ((w - tw) / 2) | 0, y + ((h - th) / 2) | 0);
  display.print(String(text));
}

function drawMultiLineButton(x, y, w, h, line1, line2, bgColor, textColor, textSize = 1) {
  drawRoundRect(x, y, w, h, 8, bgColor);
  display.setTextColor(textColor);
  display.setTextSize(textSize);
  const fh = display.fontHeight();
  const totalH = fh * 2 + 4;
  const startY = y + ((h - totalH) / 2) | 0;
  const tw1 = display.textWidth(line1);
  display.setCursor(x + ((w - tw1) / 2) | 0, startY);
  display.print(line1);
  const tw2 = display.textWidth(line2);
  display.setCursor(x + ((w - tw2) / 2) | 0, startY + fh + 4);
  display.print(line2);
}

// ---- Battery icon (top-right, WiFi mode only) ----
// Lightning-bolt charging glyph (~6w x 9h, full battery height), yellow with a
// 1px black outline.
function drawChargeBolt(x, y) {
  const runs = [[0, 4, 2], [1, 3, 2], [2, 2, 2], [3, 1, 5], [4, 3, 2], [5, 2, 2], [6, 1, 2], [7, 0, 2], [8, 0, 1]];
  const ox = [-1, 1, 0, 0], oy = [0, 0, -1, 1];
  for (let d = 0; d < 4; d++)
    for (const r of runs)
      display.drawFastHLine(x + r[1] + ox[d], y + r[0] + oy[d], r[2], COLOR_BACKGROUND);
  for (const r of runs)
    display.drawFastHLine(x + r[1], y + r[0], r[2], COLOR_YELLOW);
}

function drawBatteryIcon() {
  if (comms_active_mode() !== COMMS_MODE_WIFI) return;
  const pct = pendantMachine.batteryPercent;
  const charging = pendantMachine.batteryCharging;
  if (pct < 0) return;

  const outline = COLOR_GRAY_TEXT;   // outline no longer signals charging
  const fg = pct > 50 ? COLOR_GREEN : pct > 20 ? COLOR_ORANGE : COLOR_RED;

  // sprite was 25x13 at (212,11); draw directly at that origin
  const ox = 212,
    oy = 11;
  display.fillRect(ox, oy, 25, 13, COLOR_DARKER_BG);
  display.drawRoundRect(ox + 1, oy + 1, 20, 11, 2, outline);
  display.fillRect(ox + 21, oy + 4, 3, 5, outline);
  const fillW = (16 * pct) / 100 | 0;
  if (fillW > 0) display.fillRect(ox + 3, oy + 3, fillW, 7, fg);
  if (charging) drawChargeBolt(ox + 8, oy + 2);   // full-height yellow lightning bolt overlay
}

// ---- WiFi signal icon (top-left, WiFi mode only) ----
function drawWiFiIcon() {
  if (comms_active_mode() !== COMMS_MODE_WIFI) return;
  const ox = 5,
    oy = 11;
  display.fillRect(ox, oy, 22, 13, COLOR_DARKER_BG);
  if (pendantMachine.wifiInApMode) {
    display.setTextSize(1);
    display.setTextColor(COLOR_ORANGE);
    display.setCursor(ox + 2, oy + 3);
    display.print("AP");
    return;
  }
  let bars = pendantMachine.wifiSignalBars;
  if (bars < 0) bars = 0;
  const live = bars >= 3 ? COLOR_GREEN : bars >= 2 ? COLOR_ORANGE : bars >= 1 ? COLOR_RED : COLOR_GRAY_TEXT;
  const bar_h = [3, 6, 9, 12];
  for (let i = 0; i < 4; i++) {
    const x = 1 + i * 5;
    const h = bar_h[i];
    const y = 12 - h;
    const col = i < bars ? live : COLOR_BUTTON_GRAY;
    display.fillRect(ox + x, oy + y, 3, h, col);
  }
}

function drawTitle(title) {
  display.fillRect(0, 0, 240, 35, COLOR_DARKER_BG);
  display.setTextColor(COLOR_TITLE);
  display.setTextSize(2);
  const tw = display.textWidth(title);
  display.setCursor(((240 - tw) / 2) | 0, 10);
  display.print(title);
  drawWiFiIcon();
  drawBatteryIcon();
}

function drawInfoBox(x, y, w, h, label, value, valueColor = COLOR_ORANGE) {
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

function alarmDescription(status) {
  if (!status.startsWith("Alarm:")) return "";
  const code = parseInt(status.substring(6), 10);
  switch (code) {
    case 1: return "Hard limit triggered";
    case 2: return "Soft limit exceeded";
    case 3: return "Abort during cycle";
    case 4: return "Probe fail - no contact";
    case 5: return "Probe fail - contact lost";
    case 6: return "Homing fail - reset";
    case 7: return "Homing fail - door open";
    case 8: return "Homing fail - pull off";
    case 9: return "Homing fail - no limit";
    case 10: return "Homing fail - on limit";
    default: return "Check controller";
  }
}

// ============================================================================
//  Probe shared helpers (from screen_probe.cpp / screen_probe.h)
// ============================================================================

const PROBE_TYPE_ZPLATE = 0,
  PROBE_TYPE_XYZPLATE = 1,
  PROBE_TYPE_3D = 2,
  PROBE_TYPE_COUNT = 3;
function probeIs3D() {
  return pendantProbeV2.probeTypeIdx === PROBE_TYPE_3D;
}
function probeTipOffset3D() {
  const o = pendantProbeV2.ballDia / 2 - pendantProbeV2.deflection;
  return o > 0 ? o : 0;
}
function probeIsPlate() {
  return pendantProbeV2.probeTypeIdx !== PROBE_TYPE_3D;
}
function probeRoutineCount() {
  return pendantProbeV2.probeTypeIdx === PROBE_TYPE_3D
    ? 4
    : pendantProbeV2.probeTypeIdx === PROBE_TYPE_XYZPLATE
    ? 2
    : 1;
}

function probeDrawPosPanel(y, h = 28) {
  const px = pendantMachine.wpos[0],
    py = pendantMachine.wpos[1],
    pz = pendantMachine.wpos[2];
  const inInch = pendantMachine.inInches;
  const uu = inInch ? "in" : "mm";
  const P = panel(230, h, 5, y);
  const g = P.g,
    ox = P.ox,
    oy = P.oy;
  g.fillRoundRect(ox, oy, 230, h, 4, PROBE_BG_PANEL);

  if (h >= 38) {
    const cols = [5, 80, 155];
    const vals = [px, py, pz];
    const axLabels = ["X", "Y", "Z"];
    const dec = inInch ? 3 : 1;
    g.setTextSize(2);
    for (let i = 0; i < 3; i++) {
      g.setTextColor(PROBE_C_GREEN);
      g.setCursor(ox + cols[i], oy + 3);
      g.print(axLabels[i]);
    }
    g.setTextSize(1);
    g.setTextColor(PROBE_C_DIMBLUE);
    g.setCursor(ox + 212, oy + 3);
    g.print(uu);
    g.setTextSize(2);
    g.setTextColor(PROBE_C_YELLOW);
    for (let i = 0; i < 3; i++) {
      g.setCursor(ox + cols[i], oy + 20);
      g.print(fmtF(vals[i], dec));
    }
  } else {
    g.setTextSize(1);
    g.setTextColor(PROBE_C_LBLUE);
    g.setCursor(ox + 5, oy + 3);
    g.print("CURRENT POSITION");
    const dec = inInch ? 4 : 2;
    g.setTextColor(PROBE_C_GREEN);
    g.setCursor(ox + 5, oy + 14);
    g.print("X");
    g.setTextColor(PROBE_C_YELLOW);
    g.setCursor(ox + 13, oy + 14);
    g.print(fmtF(px, dec));
    g.setTextColor(PROBE_C_GREEN);
    g.setCursor(ox + 83, oy + 14);
    g.print("Y");
    g.setTextColor(PROBE_C_YELLOW);
    g.setCursor(ox + 91, oy + 14);
    g.print(fmtF(py, dec));
    g.setTextColor(PROBE_C_GREEN);
    g.setCursor(ox + 161, oy + 14);
    g.print("Z");
    g.setTextColor(PROBE_C_YELLOW);
    g.setCursor(ox + 169, oy + 14);
    g.print(fmtF(pz, dec));
    g.setTextColor(PROBE_C_DIMBLUE);
    g.setCursor(ox + 215, oy + 18);
    g.print(uu);
  }
}

function probeDrawSettingsLink(y) {
  drawButton(5, y, 230, 24, "Back", PROBE_BTN_BLUE, COLOR_WHITE, 2);
}

function probeDrawSelBar(y, fieldName, value, unit, decimals = 3) {
  display.fillRoundRect(5, y, 230, 20, 3, PROBE_SEL_BG);
  display.drawRoundRect(5, y, 230, 20, 3, PROBE_C_YELLOW);
  display.setTextSize(1);
  display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, y + 6);
  display.print(fieldName);
  const vbuf = fmtF(value, decimals);
  display.setTextSize(2);
  display.setTextColor(PROBE_C_YELLOW);
  const vw = display.textWidth(vbuf);
  display.setCursor(215 - vw - display.textWidth(unit), y + 3);
  display.print(vbuf);
  display.setTextSize(1);
  display.setTextColor(PROBE_C_DIMBLUE);
  display.setCursor(218 - display.textWidth(unit), y + 8);
  display.print(unit);
}

function probeDrawSelBarInt(y, fieldName, value) {
  display.fillRoundRect(5, y, 230, 20, 3, PROBE_SEL_BG);
  display.drawRoundRect(5, y, 230, 20, 3, PROBE_C_YELLOW);
  display.setTextSize(1);
  display.setTextColor(PROBE_C_LBLUE);
  display.setCursor(10, y + 6);
  display.print(fieldName);
  display.setTextSize(2);
  display.setTextColor(PROBE_C_YELLOW);
  const vbuf = String(value);
  const vw = display.textWidth(vbuf);
  display.setCursor(228 - vw, y + 3);
  display.print(vbuf);
}

function probeDrawKVTouch(x, y, w, h, label, value, unit, valColor, focused, decimals = 3) {
  const bg = focused ? PROBE_SEL_BG : PROBE_BG_SCREEN;
  const bdr = focused ? PROBE_C_YELLOW : PROBE_C_TAPBDR;
  display.fillRoundRect(x, y, w, h, 2, bg);
  display.drawRoundRect(x, y, w, h, 2, bdr);
  display.setTextSize(1);
  display.setTextColor(focused ? COLOR_WHITE : PROBE_C_LBLUE);
  display.setCursor(x + 3, y + 2);
  display.print(label);
  const vbuf = fmtF(value, decimals);
  display.setTextSize(2);
  display.setTextColor(focused ? PROBE_C_YELLOW : valColor);
  display.setCursor(x + 3, y + 11);
  display.print(vbuf);
  display.setTextSize(1);
  display.setTextColor(PROBE_C_DIMBLUE);
  const vw = display.textWidth(vbuf) * 2;
  display.setCursor(x + 3 + vw + 1, y + 14);
  display.print(unit);
}

function probeDrawKVTouchInt(x, y, w, h, label, value, valColor, focused) {
  const bg = focused ? PROBE_SEL_BG : PROBE_BG_SCREEN;
  const bdr = focused ? PROBE_C_YELLOW : PROBE_C_TAPBDR;
  display.fillRoundRect(x, y, w, h, 2, bg);
  display.drawRoundRect(x, y, w, h, 2, bdr);
  display.setTextSize(1);
  display.setTextColor(focused ? COLOR_WHITE : PROBE_C_LBLUE);
  display.setCursor(x + 3, y + 2);
  display.print(label);
  display.setTextSize(2);
  display.setTextColor(focused ? PROBE_C_YELLOW : valColor);
  display.setCursor(x + 3, y + 11);
  display.print(String(value));
}

function probeDrawWarn(y, msg, isRed = false, h = 14) {
  const bg = isRed ? PROBE_WARNR_BG : PROBE_WARN_BG;
  const bdr = isRed ? PROBE_WARNR_BDR : PROBE_WARN_BDR;
  const fg = isRed ? PROBE_C_RED : PROBE_AMBER;
  display.fillRoundRect(5, y, 230, h, 3, bg);
  display.drawRoundRect(5, y, 230, h, 3, bdr);
  display.setTextSize(1);
  display.setTextColor(fg);
  display.setCursor(10, y + ((h - 8) / 2) | 0);
  display.print(msg);
}

function probeDrawConfirmOverlay(routineName) {
  display.fillRoundRect(20, 100, 200, 120, 8, PROBE_BG_PANEL);
  display.drawRoundRect(20, 100, 200, 120, 8, PROBE_C_YELLOW);
  display.setTextSize(1);
  display.setTextColor(PROBE_C_YELLOW);
  display.setCursor(30, 110);
  display.print("CONFIRM PROBE?");
  display.setTextSize(1);
  display.setTextColor(PROBE_C_LBLUE);
  const tw = display.textWidth(routineName);
  display.setCursor(120 - (tw / 2) | 0, 126);
  display.print(routineName);
  display.fillRoundRect(28, 175, 78, 32, 5, COLOR_BUTTON_GRAY);
  display.fillRoundRect(114, 175, 98, 32, 5, PROBE_BTN_GREEN);
  display.setTextSize(2);
  display.setTextColor(COLOR_WHITE);
  display.setCursor(36, 183);
  display.print("CANCEL");
  const cw = display.textWidth("CONFIRM");
  display.setCursor(114 + ((98 - cw) / 2) | 0, 183);
  display.print("CONFIRM");
}

function probeDialStep(delta, baseStep) {
  const now = millis();
  if (now - pendantProbeV2.dialLastMs < 500) pendantProbeV2.dialAccelCount++;
  else pendantProbeV2.dialAccelCount = 0;
  pendantProbeV2.dialLastMs = now;
  const mult = pendantProbeV2.dialAccelCount >= 5 ? 10.0 : 1.0;
  return baseStep * mult;
}

// ===== List paging (shared by the SD Card and Macros screens) =====
// Both screens show LIST_ROWS rows at a time out of a longer list, and page
// through it a whole screen at a time.  scrollOffset stays the single source of
// truth (it is the row-loop base, the touch hit-test base, and part of the
// render-dirty key) and is always kept on a page boundary.
const LIST_ROWS = 5;

function listPageCount(count) {
  return Math.max(1, Math.ceil(count / LIST_ROWS));
}

function listPageIndex(scrollOffset) {
  return Math.floor(scrollOffset / LIST_ROWS);
}

// Step the page by `delta` detents (collapsed to +/-1) and clamp at both ends —
// clamped rather than wrapping, like every other dial-driven field in the UI.
// Returns the new scrollOffset, or -1 if nothing changed (so callers can skip
// the repaint entirely).
function listPageStep(delta, count, scrollOffset) {
  const pages = listPageCount(count);
  const cur = listPageIndex(scrollOffset);
  const next = constrain(cur + (delta > 0 ? 1 : -1), 0, pages - 1);
  return next === cur ? -1 : next * LIST_ROWS;
}

// Advance one page, wrapping back to the first — used by a tap on the page
// readout, where there is only one control and it has to reach every page.
function listPageAdvanceWrap(count, scrollOffset) {
  const pages = listPageCount(count);
  if (pages <= 1) return -1;
  return ((listPageIndex(scrollOffset) + 1) % pages) * LIST_ROWS;
}

// "Page 3 of 20", plus a trailing "+" when the controller had MORE entries than
// the list can hold — the pendant shows the alphabetically-first `count` of them.
function listPageLabel(count, scrollOffset, truncated) {
  return `Page ${listPageIndex(scrollOffset) + 1} of ${listPageCount(count)}${truncated ? "+" : ""}`;
}

// ---- List breadcrumb + row geometry (SD Card / Macros) ----
// The breadcrumb occupies the top of the list panel; rows run beneath it at a
// 36 px pitch so five still fit in the 200 px panel.
const LIST_CRUMB_H  = 16;   // breadcrumb band, local y 0..16
const LIST_ROW_Y0   = 18;   // first row, local
const LIST_ROW_PITCH= 36;
const LIST_ROW_H    = 32;

// Touch region for row i — the FULL row pitch, not the drawn height, so the 4 px
// gap between rows doesn't swallow taps.  Row 4 ends at 238, clear of the footer.
function listRowHitY(i) { return 40 + LIST_ROW_Y0 + i * LIST_ROW_PITCH; }
function listRowHitH()  { return LIST_ROW_PITCH; }

// Fit a path into `maxChars` by dropping leading segments — the folder you are
// IN matters more than how you got there, so truncation eats the left.
function crumbText(path, maxChars) {
  if (path.length <= maxChars) return path;
  const parts = path.split("/").filter(Boolean);
  let out = parts[parts.length - 1];
  for (let i = parts.length - 2; i >= 0; i--) {
    const next = parts[i] + "/" + out;
    if (next.length + 2 > maxChars) break;
    out = next;
  }
  return ".../" + out;
}

function drawListCrumb(ox, oy, text, color) {
  display.setTextSize(1);
  display.setTextColor(color || COLOR_GRAY_TEXT);
  display.setCursor(ox + 5, oy + 4);
  display.print(crumbText(text, 36));
}

// ---- Three-column list footer: dial hint | page readout | Refresh ----
// Col 0 tells you the dial pages the list, col 1 shows where you are.  Both are
// tappable and both advance a page, so the row behaves the way it looks —
// teal keeps the hint distinct from the green Refresh beside it and the blue
// Main Menu directly below.
const LIST_HINT_BG = COLOR_TEAL;

// Draws the whole footer row at `y`.  `pages`/`page` are 1-based.
function drawListFooter(y, page, pages, truncated, live) {
  // Single page: nothing can be paged, so the hint drops its teal and both
  // columns sit at the same plain grey with dimmed text — present, but clearly
  // doing nothing.  Multi-page: teal hint, grey readout, white text.
  const idleBg = COLOR_BUTTON_GRAY, idleFg = COLOR_GRAY_TEXT;

  drawMultiLineButton(rowBtnX(3, 0), y, rowBtnWAt(3, 0), 36,
                      "JOG", "- / +",
                      live ? LIST_HINT_BG : idleBg,
                      live ? COLOR_WHITE  : idleFg, 1);

  const val = pages > 0 ? `${page}/${pages}${truncated ? "+" : ""}` : "-/-";
  drawMultiLineButton(rowBtnX(3, 1), y, rowBtnWAt(3, 1), 36, "PAGE", val,
                      COLOR_BUTTON_GRAY,
                      live ? COLOR_WHITE : idleFg, 1);

  drawButton(rowBtnX(3, 2), y, rowBtnWAt(3, 2), 36, "Refresh",
             COLOR_DARK_GREEN, COLOR_WHITE, 1);
}

// Did this touch land on either page-advancing footer column (the JOG hint or
// the PAGE readout)?  Both do the same thing, so they share one test.
function listFooterPageTap(x, y, footerY) {
  const x0 = rowBtnX(3, 0);
  const x1 = rowBtnX(3, 1) + rowBtnWAt(3, 1);
  return isTouchInBounds(x, y, x0, footerY, x1 - x0, 36);
}

// Crash-safe two-pass approach: fast seek to contact, back off, slow re-probe.
// Ends just PAST the fine trigger — G38.2 records where the probe input fired,
// but the machine keeps moving while it decelerates.  The trigger point itself is
// in #5061/#5062/#5063; callers must probeReturnToTrigger() before zeroing a WCS
// axis, or they bake the overshoot into the work offset.  Stays G91.
function probeSeekFine(axis, seekDist, seekF, fineF) {
  const BACKOFF = 1.5;
  const dir = seekDist >= 0 ? 1 : -1;
  send_line(`G38.2 ${axis}${fmtF(seekDist, 3)} F${fmtF(seekF, 0)}`);
  send_line(`G0 ${axis}${fmtF(-dir * BACKOFF, 3)} F1000`);
  send_line(`G38.2 ${axis}${fmtF(dir * (BACKOFF + 1), 3)} F${fmtF(fineF, 0)}`);
}

// Move back to the exact G38.2 trigger point (#5061/#5062/#5063) before zeroing a
// WCS axis.  G38.2 *reports* the trigger position, but the machine coasts past it
// while decelerating unless FluidNC's hard_stop is set, so zeroing where it came
// to rest bakes that overshoot straight into the work offset.  Always a retreat
// away from the work, so safe at rapid.  Must be called in G90.
function probeReturnToTrigger(axis) {
  const param = axis === "X" ? "5061" : axis === "Y" ? "5062" : "5063";
  send_line(`G53 G0 ${axis}#${param}`);
}

// Work-area selector button — styled like the Z-Surface "Sets" box.
function probeDrawWorkAreaButton(x, y, w, h) {
  display.fillRoundRect(x, y, w, h, 8, PROBE_BG_SCREEN);
  display.drawRoundRect(x, y, w, h, 8, PROBE_C_TAPBDR);
  display.setTextSize(1); display.setTextColor(PROBE_C_LBLUE);
  const lbl = "WORK AREA";
  display.setCursor(x + ((w - display.textWidth(lbl)) / 2 | 0), y + 5);
  display.print(lbl);
  display.setTextSize(2); display.setTextColor(PROBE_C_BLUE);
  const v = pendantProbing.selectedCoordSystem;
  display.setCursor(x + ((w - display.textWidth(v)) / 2 | 0), y + 17);
  display.print(v);
}

// Cycle G54 → G55 → G56 → G57.  Selection only; the probe routine activates the
// chosen system when you actually probe.
function probeCycleWorkArea() {
  const coords = ["G54", "G55", "G56", "G57"];
  pendantProbing.selectedCoordIndex = (pendantProbing.selectedCoordIndex + 1) % 4;
  pendantProbing.selectedCoordSystem = coords[pendantProbing.selectedCoordIndex];
}

// Activate the selected WCS so the probe's G10 L20 zero is immediately in effect.
function probeActivateWcs() {
  const coords = ["G54", "G55", "G56", "G57"];
  send_line(coords[pendantProbing.selectedCoordIndex]);
}

// Sequence-step badge: filled numbered circle + label beside it.
function drawSeqStep(x, y, num, txt, active) {
  const bg = active ? PROBE_AMBER : PROBE_BG_PANEL;
  const fg = active ? COLOR_WHITE : PROBE_C_DIMBLUE;
  const tc = active ? PROBE_C_YELLOW : PROBE_C_DIMBLUE;
  display.fillCircle(x + 6, y + 6, 6, bg);
  display.setTextSize(1); display.setTextColor(fg);
  const nw = display.textWidth(String(num));
  display.setCursor(x + 6 - (nw / 2 | 0), y + 2); display.print(num);
  display.setTextColor(tc);
  display.setCursor(x + 16, y + 2); display.print(txt);
}
