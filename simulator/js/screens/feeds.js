/* screen_feeds_speeds.cpp port */

function enterFeedsSpeeds() { releasePanelSprites(); }
function exitFeedsSpeeds() { pendantFeeds.dialMode = 0; releasePanelSprites(); }

function drawFeedsSpeedsScreen() {
  display.fillScreen(COLOR_BACKGROUND);
  drawTitle("FEEDS & SPEEDS");
  updateFeedsSpeedsTopDisplay();

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 83); display.print("FEED OVERRIDE");
  const pcts = ["50%", "75%", "100%", "125%", "150%"];
  for (let i = 0; i < 3; i++) {
    const bg = i === pendantFeeds.selectedFeedOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 78, 95, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
  }
  drawButton(5, 137, 72, 37, pcts[3], (3 === pendantFeeds.selectedFeedOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY), COLOR_WHITE, 2);
  display.fillRoundRect(83, 137, 72, 37, 2, COLOR_BACKGROUND);
  updateFeedOverrideDisplay();
  drawButton(161, 137, 72, 37, pcts[4], (4 === pendantFeeds.selectedFeedOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY), COLOR_WHITE, 2);

  display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
  display.setCursor(5, 182); display.print("SPINDLE OVERRIDE");
  for (let i = 0; i < 3; i++) {
    const bg = i === pendantFeeds.selectedSpindleOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 78, 194, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
  }
  drawButton(5, 236, 72, 37, pcts[3], (3 === pendantFeeds.selectedSpindleOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY), COLOR_WHITE, 2);
  display.fillRoundRect(83, 236, 72, 37, 2, COLOR_BACKGROUND);
  updateSpindleOverrideDisplay();
  drawButton(161, 236, 72, 37, pcts[4], (4 === pendantFeeds.selectedSpindleOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY), COLOR_WHITE, 2);

  drawButton(5, 280, 230, 40, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

function updateFeedsSpeedsTopDisplay() {
  if (currentPendantScreen !== PSCREEN_FEEDS_SPEEDS) return;
  const P = panel(230, 35, 5, 40);
  const g = P.g, ox = P.ox, oy = P.oy;
  g.fillRect(ox, oy, 230, 35, COLOR_BACKGROUND);
  g.fillRoundRect(ox, oy, 112, 35, 5, COLOR_DARKER_BG);
  g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
  g.setCursor(ox + 5, oy + 3); g.print("FEED");
  g.setTextColor(COLOR_ORANGE); g.setTextSize(2);
  g.setCursor(ox + 5, oy + 13); g.print(pendantMachine.feedRate);
  g.fillRoundRect(ox + 118, oy, 112, 35, 5, COLOR_DARKER_BG);
  g.setTextColor(COLOR_GRAY_TEXT); g.setTextSize(1);
  g.setCursor(ox + 123, oy + 3); g.print("SPINDLE");
  g.setTextColor(COLOR_GREEN); g.setTextSize(2);
  g.setCursor(ox + 123, oy + 13); g.print(pendantMachine.spindleRPM);
}

// Adjustable-field style matching probeDrawKVTouch(), rendered into a panel
// sprite `g`: label on top, large value + unit below; border and value
// highlight (yellow) while the dial is active.
function drawDialField(g, ox, oy, w, h, value, valColor, active) {
  const bg = active ? PROBE_SEL_BG : PROBE_BG_SCREEN;
  const bdr = active ? PROBE_C_YELLOW : PROBE_C_DIMBLUE;
  g.fillRect(ox, oy, w, h, COLOR_BACKGROUND);
  g.fillRoundRect(ox, oy, w, h, 2, bg);
  g.drawRoundRect(ox, oy, w, h, 2, bdr);
  g.setTextSize(1);
  g.setTextColor(active ? COLOR_WHITE : PROBE_C_LBLUE);
  g.setCursor(ox + 3, oy + 2); g.print("DIAL");
  const vbuf = String(value);
  g.setTextSize(2);
  g.setTextColor(active ? PROBE_C_YELLOW : valColor);
  g.setCursor(ox + 3, oy + 11); g.print(vbuf);
  g.setTextSize(1);
  g.setTextColor(PROBE_C_DIMBLUE);
  const vw = g.textWidth(vbuf) * 2;
  g.setCursor(ox + 3 + vw + 1, oy + 14); g.print("%");
}

function updateFeedOverrideDisplay() {
  if (currentPendantScreen !== PSCREEN_FEEDS_SPEEDS) return;
  const fro = pendantMachine.feedOverride;
  const active = pendantFeeds.dialMode === 1;
  const P = panel(72, 37, 83, 137);
  drawDialField(P.g, P.ox, P.oy, 72, 37, fro, COLOR_ORANGE, active);
}

function updateSpindleOverrideDisplay() {
  if (currentPendantScreen !== PSCREEN_FEEDS_SPEEDS) return;
  const sro = pendantMachine.spindleOverride;
  const active = pendantFeeds.dialMode === 2;
  const P = panel(72, 37, 83, 236);
  drawDialField(P.g, P.ox, P.oy, 72, 37, sro, COLOR_GREEN, active);
}

function redrawFeedOverrideButtons() {
  if (currentPendantScreen !== PSCREEN_FEEDS_SPEEDS) return;
  const pcts = ["50%", "75%", "100%", "125%", "150%"];
  for (let i = 0; i < 3; i++) {
    const bg = i === pendantFeeds.selectedFeedOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 78, 95, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
  }
  drawButton(5, 137, 72, 37, pcts[3], (3 === pendantFeeds.selectedFeedOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY), COLOR_WHITE, 2);
  drawButton(161, 137, 72, 37, pcts[4], (4 === pendantFeeds.selectedFeedOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY), COLOR_WHITE, 2);
  updateFeedOverrideDisplay();
}

function redrawSpindleOverrideButtons() {
  if (currentPendantScreen !== PSCREEN_FEEDS_SPEEDS) return;
  const pcts = ["50%", "75%", "100%", "125%", "150%"];
  for (let i = 0; i < 3; i++) {
    const bg = i === pendantFeeds.selectedSpindleOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY;
    drawButton(5 + i * 78, 194, 72, 37, pcts[i], bg, COLOR_WHITE, 2);
  }
  drawButton(5, 236, 72, 37, pcts[3], (3 === pendantFeeds.selectedSpindleOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY), COLOR_WHITE, 2);
  drawButton(161, 236, 72, 37, pcts[4], (4 === pendantFeeds.selectedSpindleOverride ? COLOR_ORANGE : COLOR_BUTTON_GRAY), COLOR_WHITE, 2);
  updateSpindleOverrideDisplay();
}

function applyFeedOverride(targetPct) {
  if (!pendantConnected) return;
  fnc_realtime(FeedOvrReset);
  const delta = targetPct - 100;
  for (let i = 0; i < Math.abs(delta); i++) fnc_realtime(delta > 0 ? FeedOvrFinePlus : FeedOvrFineMinus);
}
function applySpindleOverride(targetPct) {
  if (!pendantConnected) return;
  fnc_realtime(SpindleOvrReset);
  const delta = targetPct - 100;
  for (let i = 0; i < Math.abs(delta); i++) fnc_realtime(delta > 0 ? SpindleOvrFinePlus : SpindleOvrFineMinus);
}

function handleFeedsSpeedsTouch(x, y) {
  const pcts = [50, 75, 100, 125, 150];
  for (let i = 0; i < 3; i++) {
    if (isTouchInBounds(x, y, 5 + i * 78, 95, 72, 37)) {
      pendantFeeds.dialMode = 0; pendantFeeds.selectedFeedOverride = i;
      pendantMachine.feedOverride = pcts[i];
      applyFeedOverride(pcts[i]); redrawFeedOverrideButtons(); updateSpindleOverrideDisplay();
      return;
    }
  }
  if (isTouchInBounds(x, y, 5, 137, 72, 37)) {
    pendantFeeds.dialMode = 0; pendantFeeds.selectedFeedOverride = 3;
    pendantMachine.feedOverride = 125;
    applyFeedOverride(125); redrawFeedOverrideButtons(); updateSpindleOverrideDisplay();
    return;
  }
  if (isTouchInBounds(x, y, 83, 137, 72, 37)) {
    pendantFeeds.dialMode = pendantFeeds.dialMode === 1 ? 0 : 1;
    updateFeedOverrideDisplay(); updateSpindleOverrideDisplay();
    return;
  }
  if (isTouchInBounds(x, y, 161, 137, 72, 37)) {
    pendantFeeds.dialMode = 0; pendantFeeds.selectedFeedOverride = 4;
    pendantMachine.feedOverride = 150;
    applyFeedOverride(150); redrawFeedOverrideButtons(); updateSpindleOverrideDisplay();
    return;
  }
  for (let i = 0; i < 3; i++) {
    if (isTouchInBounds(x, y, 5 + i * 78, 194, 72, 37)) {
      pendantFeeds.dialMode = 0; pendantFeeds.selectedSpindleOverride = i;
      pendantMachine.spindleOverride = pcts[i];
      applySpindleOverride(pcts[i]); redrawSpindleOverrideButtons(); updateFeedOverrideDisplay();
      return;
    }
  }
  if (isTouchInBounds(x, y, 5, 236, 72, 37)) {
    pendantFeeds.dialMode = 0; pendantFeeds.selectedSpindleOverride = 3;
    pendantMachine.spindleOverride = 125;
    applySpindleOverride(125); redrawSpindleOverrideButtons(); updateFeedOverrideDisplay();
    return;
  }
  if (isTouchInBounds(x, y, 83, 236, 72, 37)) {
    pendantFeeds.dialMode = pendantFeeds.dialMode === 2 ? 0 : 2;
    updateSpindleOverrideDisplay(); updateFeedOverrideDisplay();
    return;
  }
  if (isTouchInBounds(x, y, 161, 236, 72, 37)) {
    pendantFeeds.dialMode = 0; pendantFeeds.selectedSpindleOverride = 4;
    pendantMachine.spindleOverride = 150;
    applySpindleOverride(150); redrawSpindleOverrideButtons(); updateFeedOverrideDisplay();
    return;
  }
  if (isTouchInBounds(x, y, 5, 280, 230, 40)) currentPendantScreen = PSCREEN_MAIN_MENU;
}
