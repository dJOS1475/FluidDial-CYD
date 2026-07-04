/*
 * CNC Pendant UI — Coordinator
 *
 * Dual-core architecture:
 *   Core 0 (pendant_hw_task): UART/FluidNC polling, PCNT encoder, button debounce
 *   Core 1 (Arduino loop):    All display/touch/UI operations
 *
 * Shared state is protected by stateMutex.
 * Hardware events travel Core 0 → Core 1 via hwEventQueue.
 */

#include "cnc_pendant_config.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include "System.h"
#include "Scene.h"
#include "AboutScene.h"   // aboutScene.getBrightness() — normal backlight level
#include "FluidNCModel.h"
#include "FileParser.h"
#include "ConfigItem.h"
#include "Encoder.h"
#include "GrblParserC.h"

// Screen files
#include "screens/pendant_shared.h"
#include "screens/screen_main_menu.h"
#include "screens/screen_status.h"
#include "screens/screen_jog_homing.h"
#include "screens/screen_probing_work.h"
#include "screens/screen_probe.h"
#include "screens/screen_probe_cfg.h"
#include "screens/screen_probe_z.h"
#include "screens/screen_probe_corner.h"
#include "screens/screen_probe_bore_boss.h"
#include "screens/screen_feeds_speeds.h"
#include "screens/screen_spindle_control.h"
#include "screens/screen_macros.h"
#include "screens/screen_sd_card.h"
#include "screens/screen_fluidnc.h"
#include "screens/screen_wifi_setup.h"

#include "Comms.h"
#ifdef USE_WIFI
// Needed for drawWiFiIcon() which renders signal bars / AP indicator into the
// title bar on every screen.  All actual byte-level I/O still goes through
// the comms facade (comms_putchar / comms_getchar / comms_poll); WiFiConnection
// is included here only for display-side status queries.
#include "WiFiConnection.h"
#endif

// ===== Hardware pin externs (defined in Hardware2432.cpp) =====
extern int red_button_pin;
extern int dial_button_pin;
extern int green_button_pin;

// ===== NVS Preferences =====
Preferences preferences;

// ===== FreeRTOS Sync Objects =====
SemaphoreHandle_t stateMutex   = nullptr;
QueueHandle_t     hwEventQueue = nullptr;

// ===== Connection State =====
// Driven exclusively by fnc_is_connected() on Core 0 (backed by update_rx_time() per UART byte).
volatile bool pendantConnected = false;

// Sync tracking (all touched only on Core 0: the connection edge logic and the
// parser scene callbacks both run there).  pendantSynced gates the main menu's
// "Connecting" indicator — see pendant_shared.h.
volatile bool        pendantSynced  = false;
static unsigned long syncConnectMs  = 0;      // millis() at the connect edge

// ===== Screen State =====
PendantScreen currentPendantScreen = PSCREEN_MAIN_MENU;

// ===== Machine & UI State Variables =====
MachineState  pendantMachine;
JogState      pendantJog;
SDCardState   pendantSdCard;
MacroState    pendantMacros;
SpindleState  pendantSpindle;
FeedsState    pendantFeeds;
ProbingState  pendantProbing;
ProbeV2State  pendantProbeV2;

// ===== Shared Sprite Buffers (reused across screens) =====
LGFX_Sprite spriteAxisDisplay(&display);
LGFX_Sprite spriteValueDisplay(&display);
LGFX_Sprite spriteStatusBar(&display);
LGFX_Sprite spriteFileDisplay(&display);

// 8-bit panel-sprite allocator (see pendant_shared.h).  Depth is set BEFORE
// createSprite() — that's the whole point: the previous code set 16-bit AFTER
// creation, a no-op, so panels were full 16-bit and frequently failed to
// allocate on the heap-tight WiFi build (→ flicker).  8-bit (rgb332) halves
// the buffer, so allocation succeeds far more often and the flicker-free
// sprite path becomes the common case.  pushSprite() converts 8→16 bit for the
// display automatically.
bool allocPanelSprite(LGFX_Sprite& s, int w, int h, uint32_t minHeap) {
    s.deleteSprite();
    if (minHeap && ESP.getFreeHeap() < minHeap) return false;
    s.setColorDepth(8);
    s.createSprite(w, h);
    if (!s.getBuffer()) { s.deleteSprite(); return false; }
    return true;
}

// One shared 16-bit scratch sprite, reused for every panel draw on a screen.
// Grows on demand to the largest panel size seen and is then held (no per-frame
// alloc/free churn), and is released by releasePanelSprites() on screen change
// so it never competes with the macros/SD list sprites.
static LGFX_Sprite spritePanelScratch(&display);
static int         scratchW = 0;
static int         scratchH = 0;

void releasePanelSprites() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    spritePanelScratch.deleteSprite();
    scratchW = scratchH = 0;
}

// Shared-scratch panel helpers.  Instead of allocating/freeing a sprite on
// every panel every frame (heap churn + fragmentation), or holding a separate
// persistent buffer per panel (too much RAM at 16-bit), these draw every panel
// into ONE 16-bit scratch sprite and push only the panel's w×h region via a
// destination clip rect.  The scratch grows monotonically to the screen's
// largest panel, then stays put — zero churn in steady state.  16-bit keeps the
// near-neutral COLOR_DARKER_BG panels true gray (8-bit rgb332 crushed them
// green).  Freed by releasePanelSprites() on screen change.
//
// beginPanelSprite() returns the graphics target and sets ox/oy to the draw
// origin: (0,0) into the scratch, or the on-screen panel origin (px,py) when the
// scratch can't allocate (direct-draw fallback — never blank).  Pair every call
// with endPanelSprite(), passing the SAME w/h/px/py.
LovyanGFX* beginPanelSprite(int w, int h, int& ox, int& oy, int px, int py) {
    if (!spritePanelScratch.getBuffer() || w > scratchW || h > scratchH) {
        int nw = w > scratchW ? w : scratchW;
        int nh = h > scratchH ? h : scratchH;
        spritePanelScratch.deleteSprite();
        spritePanelScratch.setColorDepth(16);
        spritePanelScratch.createSprite(nw, nh);
        if (spritePanelScratch.getBuffer()) { scratchW = nw; scratchH = nh; }
        else                                { scratchW = scratchH = 0; }
    }
    if (spritePanelScratch.getBuffer()) {
        ox = 0; oy = 0;
        return (LovyanGFX*)&spritePanelScratch;
    }
    ox = px; oy = py;
    return (LovyanGFX*)&display;
}

void endPanelSprite(int w, int h, int px, int py) {
    if (spritePanelScratch.getBuffer()) {
        // Clip the destination so a larger scratch writes only the panel region.
        display.setClipRect(px, py, w, h);
        spritePanelScratch.pushSprite(px, py);
        display.clearClipRect();
    }
}

// ===== Helper Functions =====
bool isTouchInBounds(int tx, int ty, int x, int y, int w, int h) {
    return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    display.fillRoundRect(x, y, w, h, r, color);
}

void drawButton(int x, int y, int w, int h, String text, uint16_t bgColor, uint16_t textColor, int textSize) {
    drawRoundRect(x, y, w, h, 8, bgColor);
    display.setTextColor(textColor);
    display.setTextSize(textSize);
    int16_t tw = display.textWidth(text.c_str());
    int16_t th = display.fontHeight();
    display.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    display.print(text);
}

void drawMultiLineButton(int x, int y, int w, int h, String line1, String line2,
                         uint16_t bgColor, uint16_t textColor, int textSize) {
    drawRoundRect(x, y, w, h, 8, bgColor);
    display.setTextColor(textColor);
    display.setTextSize(textSize);
    int16_t fh     = display.fontHeight();
    int16_t totalH = fh * 2 + 4;
    int16_t startY = y + (h - totalH) / 2;
    int16_t tw1    = display.textWidth(line1.c_str());
    display.setCursor(x + (w - tw1) / 2, startY);
    display.print(line1);
    int16_t tw2 = display.textWidth(line2.c_str());
    display.setCursor(x + (w - tw2) / 2, startY + fh + 4);
    display.print(line2);
}

// ── Battery icon ─────────────────────────────────────────────────────────────
// Draws a small battery body + nub at the top-right of the title bar.
// Called from drawTitle() on every full screen redraw, and from
// updateCurrentScreenSprites() every 100 ms for live updates between redraws.
// Reads pendantMachine.batteryPercent without the mutex — safe on Xtensa LX6
// (32-bit int write/read is atomic) and consistent with all other live fields.
// Battery icon — rendered into a persistent 25×13 sprite and pushed atomically.
// Sprite screen position: (215, 11).  Layout inside the sprite (offsets from top-left):
//   Body outline : (1,1) 20×11  Nub fill : (21,4) 3×5
//   Charge fill  : (3,3) up-to-16 × 7
// The sprite is allocated once on first call and kept alive for the session (~650 B).
//
// Visibility rule: the gauge is shown ONLY when the comms layer is running in
// WiFi mode.  WiFi mode implies a mobile / battery-powered pendant (per
// product policy — wired pendants always use UART).  This is a more robust
// gate than I2C-probing the IP5306, which can fail on some hardware variants
// even when a battery and resistor-divider are present.
// Lightning-bolt charging glyph, drawn into the battery sprite at top-left
// (x,y).  ~6w × 9h — spans the full height of the battery body — yellow with a
// 1px black outline so it reads on any charge-level bar colour behind it.
static void drawChargeBolt(LGFX_Sprite& spr, int x, int y) {
    static const uint8_t runs[9][3] = {   // {row, x-offset, width}
        {0, 4, 2}, {1, 3, 2}, {2, 2, 2}, {3, 1, 5},
        {4, 3, 2}, {5, 2, 2}, {6, 1, 2}, {7, 0, 2}, {8, 0, 1},
    };
    static const int ox[4] = { -1, 1, 0, 0 };
    static const int oy[4] = {  0, 0, -1, 1 };
    for (int d = 0; d < 4; ++d)            // black 1px outline (4-way offset)
        for (int i = 0; i < 9; ++i)
            spr.drawFastHLine(x + runs[i][1] + ox[d], y + runs[i][0] + oy[d], runs[i][2], COLOR_BACKGROUND);
    for (int i = 0; i < 9; ++i)            // yellow fill
        spr.drawFastHLine(x + runs[i][1], y + runs[i][0], runs[i][2], COLOR_YELLOW);
}

static void drawBatteryIcon() {
#ifdef USE_WIFI
    if (comms_active_mode() != COMMS_MODE_WIFI) return;  // wired pendant
#else
    return;
#endif
    int  pct      = pendantMachine.batteryPercent;
    bool charging = pendantMachine.batteryCharging;
    if (pct < 0) return;  // ADC not yet sampled or out of valid range — skip

    static LGFX_Sprite spr(&display);
    if (!spr.getBuffer()) {
        spr.createSprite(25, 13);
        if (!spr.getBuffer()) return;  // allocation failed — silent no-op
    }

    uint16_t outline = COLOR_GRAY_TEXT;   // outline no longer signals charging
    uint16_t fg      = (pct > 50) ? COLOR_GREEN : (pct > 20) ? COLOR_ORANGE : COLOR_RED;

    spr.fillSprite(COLOR_DARKER_BG);          // background
    spr.drawRoundRect(1, 1, 20, 11, 2, outline);  // body
    spr.fillRect(21, 4, 3, 5, outline);           // nub
    int fillW = 16 * pct / 100;                   // interior width = bw-4 = 16
    if (fillW > 0)
        spr.fillRect(3, 3, fillW, 7, fg);         // charge level bar
    if (charging)
        drawChargeBolt(spr, 8, 2);                // full-height yellow lightning bolt overlay
    // Position: x=212 to leave a ~3px right margin so the icon's right edge
    // sits symmetrically relative to the WiFi icon's left edge at x=5.
    spr.pushSprite(212, 11);                      // atomic blit — no visible clear step
}

// ── WiFi signal-strength icon ────────────────────────────────────────────────
// 4 vertical bars in a 22×13 region anchored at the top-left of the title bar.
// Heights are ascending; bars below the current signal level are dimmed gray.
// In AP captive-portal mode we show "AP" text instead.  Hidden entirely when
// the active comms transport is UART (the icon would be meaningless there).
//
// IMPORTANT: this function reads CACHED WiFi state from pendantMachine, NOT
// the live WiFi API.  Sampling happens in pendant_hw_task on Core 0 every
// 500 ms; Core 1's UI never calls WiFi.RSSI() / wifi_in_ap_mode() directly,
// avoiding cross-core access to the Arduino WiFi state machine which has
// been a suspected crash source.
static void drawWiFiIcon() {
#ifdef USE_WIFI
    if (comms_active_mode() != COMMS_MODE_WIFI) return;

    static LGFX_Sprite spr(&display);
    if (!spr.getBuffer()) {
        spr.createSprite(22, 13);
        if (!spr.getBuffer()) return;
    }
    spr.fillSprite(COLOR_DARKER_BG);

    if (pendantMachine.wifiInApMode) {
        spr.setTextSize(1);
        spr.setTextColor(COLOR_ORANGE);
        spr.setCursor(2, 3);
        spr.print("AP");
    } else {
        int bars = pendantMachine.wifiSignalBars;
        if (bars < 0) bars = 0;
        uint16_t live = (bars >= 3) ? COLOR_GREEN
                      : (bars >= 2) ? COLOR_ORANGE
                      : (bars >= 1) ? COLOR_RED
                                    : COLOR_GRAY_TEXT;
        // 4 bars at x = 1, 6, 11, 16  (3 wide, 4 px gap between centres)
        // Heights ascending; baseline at y = 12 so all bars are bottom-aligned.
        static const int bar_h[4] = { 3, 6, 9, 12 };
        for (int i = 0; i < 4; ++i) {
            int x = 1 + i * 5;
            int h = bar_h[i];
            int y = 12 - h;
            uint16_t col = (i < bars) ? live : COLOR_BUTTON_GRAY;
            spr.fillRect(x, y, 3, h, col);
        }
    }
    spr.pushSprite(5, 11);
#endif
}

void drawTitle(String title) {
    display.fillRect(0, 0, 240, 35, COLOR_DARKER_BG);
    display.setTextColor(COLOR_TITLE);
    display.setTextSize(2);
    int16_t tw = display.textWidth(title.c_str());
    display.setCursor((240 - tw) / 2, 10);
    display.print(title);
    drawWiFiIcon();     // overlay icon at top-left;  no-op if not in WiFi mode
    drawBatteryIcon();  // overlay icon at top-right; no-op if battery unavailable
}

void drawInfoBox(int x, int y, int w, int h, String label, String value, uint16_t valueColor) {
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

// ===== Screen sleep (PSCREEN_SLEEP) =====
// A hidden, button-less screen used to BLANK the display after a period of
// inactivity while the CNC is idle.  Because it becomes the ACTIVE screen while
// asleep, the touch dispatcher (a switch on currentPendantScreen) can only reach
// handleSleepTouch() — there are no other screen's buttons under the blank, so a
// wake touch can never reach a control or send a byte to the controller.
// This is a screen blank only: the framebuffer, the ESP32 and all comms keep
// running (it is NOT power-off / deep sleep).
#define SLEEP_TIMEOUT_MS (15UL * 60UL * 1000UL)   // 15 min of idle inactivity

extern AboutScene aboutScene;   // normal backlight level (AboutScene.cpp)

static PendantScreen sleepReturnScreen        = PSCREEN_MAIN_MENU;
static unsigned long lastActivityMs           = 0;
static bool          swallowTouchUntilRelease = false;

static void enterSleep()      { display.setBrightness(0); }                          // backlight off
static void exitSleep()       { display.setBrightness(aboutScene.getBrightness()); } // restore normal
static void drawSleepScreen() { display.fillScreen(COLOR_BACKGROUND); }              // black (invisible w/ BL off)

static void handleSleepTouch(int /*x*/, int /*y*/) {
    // ANY touch wakes.  The touch is consumed here and never dispatched to the
    // previous screen.  Per the dispatch convention we only set the target
    // screen; handlePendantTouch()'s wrapper then runs exitSleep() (restores
    // brightness) + enter/draw of the return screen exactly once.  The
    // release-gate stops a held finger from carrying into a button on the
    // restored screen until it is lifted.
    swallowTouchUntilRelease = true;
    lastActivityMs           = millis();
    currentPendantScreen     = sleepReturnScreen;
}

// ===== Screen Lifecycle Routing =====
static void callScreenExit(PendantScreen s) {
    switch (s) {
        case PSCREEN_MAIN_MENU:        exitMainMenu();        break;
        case PSCREEN_STATUS:           exitStatus();          break;
        case PSCREEN_JOG_HOMING:       exitJogHoming();       break;
        case PSCREEN_PROBING_WORK:     exitProbingWork();     break;
        case PSCREEN_PROBE:            exitProbe();           break;
        case PSCREEN_PROBE_CFG_3D:     exitProbeCfg3D();      break;
        case PSCREEN_PROBE_CFG_PLATE:  exitProbeCfgPlate();   break;
        case PSCREEN_PROBE_Z:          exitProbeZ();          break;
        case PSCREEN_PROBE_CORNER:     exitProbeCorner();     break;
        case PSCREEN_PROBE_BORE:       exitProbeBore();       break;
        case PSCREEN_PROBE_BOSS:       exitProbeBoss();       break;
        case PSCREEN_FEEDS_SPEEDS:     exitFeedsSpeeds();     break;
        case PSCREEN_SPINDLE_CONTROL:  exitSpindleControl();  break;
        case PSCREEN_MACROS:           exitMacros();          break;
        case PSCREEN_SD_CARD:          exitSDCard();          break;
        case PSCREEN_FLUIDNC:          exitFluidNC();         break;
        case PSCREEN_WIFI_SETUP:       exitWiFiSetup();       break;
        case PSCREEN_SLEEP:            exitSleep();           break;
    }
}

static void callScreenEnter(PendantScreen s) {
    switch (s) {
        case PSCREEN_MAIN_MENU:        enterMainMenu();        break;
        case PSCREEN_STATUS:           enterStatus();          break;
        case PSCREEN_JOG_HOMING:       enterJogHoming();       break;
        case PSCREEN_PROBING_WORK:     enterProbingWork();     break;
        case PSCREEN_PROBE:            enterProbe();           break;
        case PSCREEN_PROBE_CFG_3D:     enterProbeCfg3D();      break;
        case PSCREEN_PROBE_CFG_PLATE:  enterProbeCfgPlate();   break;
        case PSCREEN_PROBE_Z:          enterProbeZ();          break;
        case PSCREEN_PROBE_CORNER:     enterProbeCorner();     break;
        case PSCREEN_PROBE_BORE:       enterProbeBore();       break;
        case PSCREEN_PROBE_BOSS:       enterProbeBoss();       break;
        case PSCREEN_FEEDS_SPEEDS:     enterFeedsSpeeds();     break;
        case PSCREEN_SPINDLE_CONTROL:  enterSpindleControl();  break;
        case PSCREEN_MACROS:           enterMacros();          break;
        case PSCREEN_SD_CARD:          enterSDCard();          break;
        case PSCREEN_FLUIDNC:          enterFluidNC();         break;
        case PSCREEN_WIFI_SETUP:       enterWiFiSetup();       break;
        case PSCREEN_SLEEP:            enterSleep();           break;
    }
}

void drawCurrentPendantScreen() {
    switch (currentPendantScreen) {
        case PSCREEN_MAIN_MENU:        drawMainMenu();              break;
        case PSCREEN_STATUS:           drawStatusScreen();          break;
        case PSCREEN_JOG_HOMING:       drawJogHomingScreen();       break;
        case PSCREEN_PROBING_WORK:     drawProbingWorkScreen();     break;
        case PSCREEN_PROBE:            drawProbeScreen();           break;
        case PSCREEN_PROBE_CFG_3D:     drawProbeCfg3DScreen();      break;
        case PSCREEN_PROBE_CFG_PLATE:  drawProbeCfgPlateScreen();   break;
        case PSCREEN_PROBE_Z:          drawProbeZScreen();          break;
        case PSCREEN_PROBE_CORNER:     drawProbeCornerScreen();     break;
        case PSCREEN_PROBE_BORE:       drawProbeBoreScreen();       break;
        case PSCREEN_PROBE_BOSS:       drawProbeBossScreen();       break;
        case PSCREEN_FEEDS_SPEEDS:     drawFeedsSpeedsScreen();     break;
        case PSCREEN_SPINDLE_CONTROL:  drawSpindleControlScreen();  break;
        case PSCREEN_MACROS:           drawMacrosScreen();          break;
        case PSCREEN_SD_CARD:          drawSDCardScreen();          break;
        case PSCREEN_FLUIDNC:          drawFluidNCScreen();         break;
        case PSCREEN_WIFI_SETUP:       drawWiFiSetupScreen();       break;
        case PSCREEN_SLEEP:            drawSleepScreen();           break;
    }
}

void navigateTo(PendantScreen next) {
    if (next == currentPendantScreen) return;
    callScreenExit(currentPendantScreen);
    currentPendantScreen = next;
    callScreenEnter(next);
    drawCurrentPendantScreen();
}

// ===== Touch Dispatch (Core 1) =====
static uint32_t lastNavMs = 0;  // timestamp of last screen navigation

static void handlePendantTouch(int x, int y) {
    // Ignore touch events for 350 ms after a navigation to prevent the same
    // tap from registering on the newly-drawn screen (touch bounce).
    if ((uint32_t)milliseconds() - lastNavMs < 350) return;

    PendantScreen before = currentPendantScreen;

    switch (currentPendantScreen) {
        case PSCREEN_MAIN_MENU:        handleMainMenuTouch(x, y);        break;
        case PSCREEN_JOG_HOMING:       handleJogHomingTouch(x, y);       break;
        case PSCREEN_SPINDLE_CONTROL:  handleSpindleControlTouch(x, y);  break;
        case PSCREEN_FEEDS_SPEEDS:     handleFeedsSpeedsTouch(x, y);     break;
        case PSCREEN_SD_CARD:          handleSDCardTouch(x, y);          break;
        case PSCREEN_PROBING_WORK:     handleProbingWorkTouch(x, y);     break;
        case PSCREEN_PROBE:            handleProbeTouch(x, y);           break;
        case PSCREEN_PROBE_CFG_3D:     handleProbeCfg3DTouch(x, y);      break;
        case PSCREEN_PROBE_CFG_PLATE:  handleProbeCfgPlateTouch(x, y);   break;
        case PSCREEN_PROBE_Z:          handleProbeZTouch(x, y);          break;
        case PSCREEN_PROBE_CORNER:     handleProbeCornerTouch(x, y);     break;
        case PSCREEN_PROBE_BORE:       handleProbeBoreTouch(x, y);       break;
        case PSCREEN_PROBE_BOSS:       handleProbeBossTouch(x, y);       break;
        case PSCREEN_MACROS:           handleMacrosTouch(x, y);          break;
        case PSCREEN_STATUS:           handleStatusTouch(x, y);          break;
        case PSCREEN_FLUIDNC:          handleFluidNCTouch(x, y);         break;
        case PSCREEN_WIFI_SETUP:       handleWiFiSetupTouch(x, y);       break;
        case PSCREEN_SLEEP:            handleSleepTouch(x, y);           break;
    }

    if (currentPendantScreen != before) {
        PendantScreen dest = currentPendantScreen;
        currentPendantScreen = before;   // restore so navigateTo sees correct previous
        navigateTo(dest);
        lastNavMs = (uint32_t)milliseconds();  // start cooldown
    }
}

// ===== Encoder Delta Handler (Core 1) =====
static void handleEncoderDelta(int32_t delta) {
    if (currentPendantScreen == PSCREEN_SPINDLE_CONTROL && pendantSpindle.dialMode) {
        int maxRPM  = pendantMachine.spindleMaxRPM > 0 ? pendantMachine.spindleMaxRPM : 24000;
        int minRPM  = pendantMachine.spindleMinRPM;
        int rpmStep = (maxRPM <= 10000) ? 100 : 1000;
        pendantSpindle.targetRPM = constrain(pendantSpindle.targetRPM + delta * rpmStep, minRPM, maxRPM);
        updateSpindleRPMDisplay();
        return;
    } else if (currentPendantScreen == PSCREEN_JOG_HOMING) {
        if (!pendantConnected) return;

        // Jog flow control: if FluidNC's motion planner is full (many sends
        // in flight without acks), skip this encoder event entirely.  The
        // velFactor logic below already coalesces fast detents into larger
        // single-command moves; skipping here is the last line of defence
        // against overflowing FluidNC's TCP RX buffer and corrupting the
        // command stream.  FluidNC's default planner is ~16 deep but we
        // throttle earlier to leave room for status polls and other commands.
        if (pending_nowait_sends >= 6) {
            return;
        }

        if (pendantJog.speedDialMode) {
            // Adjust jog speed cap — metric: 500 mm/min/step, imperial: 20 ipm/step
            int maxIn = constrain((int)(pendantJog.maxFeedRate / 25.4f), 40, 400);
            if (pendantMachine.inInches) {
                pendantJog.jogSpeedIn = constrain(pendantJog.jogSpeedIn + delta * 20, 40, maxIn);
            } else {
                pendantJog.jogSpeedMm = constrain(pendantJog.jogSpeedMm + delta * 500, 1000, pendantJog.maxFeedRate);
            }
            redrawJogSpeedButton();
            updateJogAxisDisplay();
            return;
        }
        if (pendantJog.selectedAxis < 0) return;  // no axis selected — do nothing

        // Send $J immediately per tick (like cyd_buttons) so FluidNC's planner buffer
        // stays populated and the deceleration ramp bridges the gap between ticks.
        // Time-based velocity scaling: fast turns send a proportionally larger distance.
        {
            static unsigned long lastTickMs = 0;
            unsigned long now = millis();
            unsigned long interval = now - lastTickMs;
            lastTickMs = now;
            int velFactor = (interval < 80) ? 4 : (interval < 150) ? 2 : 1;

            String axisNames[] = { "X", "Y", "Z", "A" };
            float  distance    = (float)delta * velFactor * pendantJog.increment;

            // Safety clamp: never request more than half the axis travel range in a
            // single jog tick. Prevents a fast wheel turn at a coarse increment from
            // queueing a move that would crash into a hard stop or trip soft limits.
            // $13x is reported in mm regardless of G20/G21 — convert to inches if needed.
            // Falls back to a hard-coded cap (100 mm / 4 in) if the controller hasn't
            // reported $13x yet (e.g. immediately after connect).
            {
                int   axis  = pendantJog.selectedAxis;
                float capMm = (pendantJog.maxTravel[axis] > 0)
                                ? pendantJog.maxTravel[axis] * 0.5f
                                : 100.0f;
                float cap   = pendantMachine.inInches ? (capMm / 25.4f) : capMm;
                if (distance >  cap) distance =  cap;
                if (distance < -cap) distance = -cap;
            }

            // Soft-limit clamp (absolute): keep the resulting MACHINE position
            // inside the homed travel envelope so cumulative G91 jogs can't walk
            // into a hard stop — the per-tick cap above only bounds a single tick.
            // Envelope per axis (home = MPos 0): $23 bit clear → homes +, travel
            // runs [-maxTravel, 0]; bit set → homes −, travel runs [0, +maxTravel].
            // Only engages for a linear axis (X/Y/Z) once travel and the $23 mask
            // are known and the machine isn't in Alarm (MPos unreferenced); until
            // then it falls through to FluidNC's own soft limits unchanged.
            //
            // We clamp against a PREDICTED position, not the live MPos: successive
            // G91 jogs queue in FluidNC's planner while the reported MPos lags, so
            // clamping on MPos alone would let a fast continuous spin over-commit
            // past the limit. predMm[] leads MPos by the queued-but-unexecuted
            // distance; it is re-seeded from the real MPos whenever a jog burst
            // ends and the machine settles to Idle (planner drained).
            {
                int axis = pendantJog.selectedAxis;
                if (axis >= 0 && axis <= 2 &&
                    pendantJog.maxTravel[axis] > 0 &&
                    pendantJog.homingDirMask >= 0 &&
                    !pendantMachine.status.startsWith("Alarm")) {

                    static float predMm[3]  = { NAN, NAN, NAN };  // predicted MPos incl. queued jogs
                    static int   lastAxis   = -1;

                    bool  homesNeg  = (pendantJog.homingDirMask >> axis) & 1;
                    float travelMm  = (float)pendantJog.maxTravel[axis];
                    float loMm      = homesNeg ? 0.0f      : -travelMm;   // envelope bounds
                    float hiMm      = homesNeg ? travelMm  :  0.0f;
                    const float MARGIN_MM = 0.5f;                         // stay off the switch

                    float mposDisp  = (axis == 0) ? pendantMachine.workX
                                    : (axis == 1) ? pendantMachine.workY
                                                  : pendantMachine.workZ;   // MPos, display units
                    float mposMm    = pendantMachine.inInches ? mposDisp * 25.4f : mposDisp;
                    float distMm    = pendantMachine.inInches ? distance * 25.4f : distance;

                    // Re-seed the prediction from the real MPos at the start of a
                    // burst (gap since last tick, or axis change) once the machine
                    // has settled to Idle — and always on first use.
                    bool newBurst = (interval > 400) || (axis != lastAxis);
                    if (isnan(predMm[axis]) ||
                        (newBurst && pendantMachine.status.startsWith("Idle"))) {
                        predMm[axis] = mposMm;
                    }
                    lastAxis = axis;

                    // Clamp against the predicted position; only ever REDUCE the
                    // move toward the limit — never flip its sign.
                    if (distMm > 0.0f) {
                        float room = (hiMm - MARGIN_MM) - predMm[axis];
                        if (room < 0.0f) room = 0.0f;
                        if (distMm > room) distMm = room;
                    } else if (distMm < 0.0f) {
                        float room = (loMm + MARGIN_MM) - predMm[axis];
                        if (room > 0.0f) room = 0.0f;
                        if (distMm < room) distMm = room;
                    }

                    distance = pendantMachine.inInches ? distMm / 25.4f : distMm;
                    // Fully blocked at the limit — drop the tick instead of emitting a no-op jog.
                    if (fabsf(distance) < 1e-4f) return;
                    predMm[axis] += distMm;   // commit the queued distance to the prediction
                }
            }

            char   cmd[64];
            if (pendantMachine.inInches) {
                int maxIn = constrain((int)(pendantJog.maxFeedRate / 25.4f), 40, 400);
                int speed = constrain(pendantJog.jogSpeedIn, 40, maxIn);
                snprintf(cmd, sizeof(cmd), "$J=G91 G20 %s%.4f F%d",
                         axisNames[pendantJog.selectedAxis].c_str(), distance, speed);
            } else {
                int speed = constrain(pendantJog.jogSpeedMm, 1000, pendantJog.maxFeedRate);
                snprintf(cmd, sizeof(cmd), "$J=G91 G21 %s%.3f F%d",
                         axisNames[pendantJog.selectedAxis].c_str(), distance, speed);
            }
            // Use the no-ack-wait variant — jog commands queue in FluidNC's
            // motion planner and don't need synchronous handshake.  Critical
            // for smooth fine-increment jogging over WiFi where the ~100 ms
            // network round-trip would otherwise serialize each command and
            // produce noticeable jerk between consecutive 1 mm moves.
            send_line_nowait(cmd);
        }
    } else if (currentPendantScreen == PSCREEN_PROBE        ||
               currentPendantScreen == PSCREEN_PROBE_CFG_3D ||
               currentPendantScreen == PSCREEN_PROBE_CFG_PLATE ||
               currentPendantScreen == PSCREEN_PROBE_Z      ||
               currentPendantScreen == PSCREEN_PROBE_CORNER ||
               currentPendantScreen == PSCREEN_PROBE_BORE   ||
               currentPendantScreen == PSCREEN_PROBE_BOSS) {

        int fo = pendantProbeV2.focusedField;
        if (fo < 0) return;  // no field focused — dial does nothing

        auto& p = pendantProbeV2;

        if (currentPendantScreen == PSCREEN_PROBE) {
            // SCR0 shared fields: 0=probeRate 1=seekRate 2=retractDist 3=maxZTravel
            float step = probeDialStep(delta, (fo <= 1) ? 10.0f : 0.1f);
            if (fo == 0) p.probeRate   = constrain(p.probeRate   + delta * step, 10.0f, 3000.0f);
            if (fo == 1) p.seekRate    = constrain(p.seekRate    + delta * step, 10.0f, 3000.0f);
            if (fo == 2) p.retractDist = constrain(p.retractDist + delta * step,  0.1f,   50.0f);
            if (fo == 3) p.maxZTravel  = constrain(p.maxZTravel  + delta * step,  1.0f,  200.0f);
            drawProbeScreen();

        } else if (currentPendantScreen == PSCREEN_PROBE_CFG_3D) {
            // 0=ballDia 1=deflection
            float step = probeDialStep(delta, (fo == 0) ? 0.1f : 0.001f);
            if (fo == 0) p.ballDia   = constrain(p.ballDia   + delta * step,  0.1f,  20.0f);
            if (fo == 1) p.deflection= constrain(p.deflection+ delta * step,  0.0f,   1.0f);
            drawProbeCfg3DScreen();

        } else if (currentPendantScreen == PSCREEN_PROBE_CFG_PLATE) {
            // 0=plateThick 1=plateWidth 2=plateOffX 3=plateOffY
            float step = probeDialStep(delta, 0.1f);
            if (fo == 0) p.plateThick = constrain(p.plateThick + delta * step,  0.1f,  50.0f);
            if (fo == 1) p.plateWidth = constrain(p.plateWidth + delta * step,  1.0f, 200.0f);
            if (fo == 2) p.plateOffX  = constrain(p.plateOffX  + delta * step,-50.0f,  50.0f);
            if (fo == 3) p.plateOffY  = constrain(p.plateOffY  + delta * step,-50.0f,  50.0f);
            drawProbeCfgPlateScreen();

        } else if (currentPendantScreen == PSCREEN_PROBE_Z) {
            // fo==0 → maxZTravel, fo==1 → retractDist
            // Whole-mm steps (or imperial equiv); accelerates to 10mm after rapid turns
            float base = pendantMachine.inInches ? (1.0f / 25.4f) : 1.0f;
            float step = probeDialStep(delta, base);
            if (fo == 0) p.maxZTravel  = constrain(p.maxZTravel  + delta * step,  1.0f, 200.0f);
            if (fo == 1) p.retractDist = constrain(p.retractDist + delta * step,  1.0f,  50.0f);
            drawProbeZScreen();

        } else if (currentPendantScreen == PSCREEN_PROBE_CORNER) {
            // 0=cornerDepth 1=cornerOver 2=cornerRetXY
            float step = probeDialStep(delta, 0.1f);
            if (fo == 0) p.cornerDepth = constrain(p.cornerDepth + delta * step, 0.1f, 50.0f);
            if (fo == 1) p.cornerOver  = constrain(p.cornerOver  + delta * step, 0.1f, 20.0f);
            if (fo == 2) p.cornerRetXY = constrain(p.cornerRetXY + delta * step, 0.1f, 20.0f);
            drawProbeCornerScreen();

        } else if (currentPendantScreen == PSCREEN_PROBE_BORE) {
            // 0=boreDia 1=boreOffset
            float step = probeDialStep(delta, 0.1f);
            if (fo == 0) p.boreDia    = constrain(p.boreDia    + delta * step, 0.1f, 500.0f);
            if (fo == 1) p.boreOffset = constrain(p.boreOffset + delta * step, 0.1f,  50.0f);
            drawProbeBoreScreen();

        } else if (currentPendantScreen == PSCREEN_PROBE_BOSS) {
            // 0=bossDia 1=bossDepth 2=bossClear
            float step = probeDialStep(delta, 0.1f);
            if (fo == 0) p.bossDia   = constrain(p.bossDia   + delta * step, 0.1f, 500.0f);
            if (fo == 1) p.bossDepth = constrain(p.bossDepth + delta * step, 0.1f, 100.0f);
            if (fo == 2) p.bossClear = constrain(p.bossClear + delta * step, 0.1f,  50.0f);
            drawProbeBossScreen();
        }
        return;

    } else if (currentPendantScreen == PSCREEN_FEEDS_SPEEDS) {
        if (!pendantConnected) return;
        if (pendantFeeds.dialMode == 1) {
            // Feed override — 10% per detent via 10× fine steps
            int steps = abs(delta) * 10;
            for (int i = 0; i < steps; i++)
                fnc_realtime(delta > 0 ? FeedOvrFinePlus : FeedOvrFineMinus);
            updateFeedOverrideDisplay();
        } else if (pendantFeeds.dialMode == 2) {
            // Spindle override — 10% per detent via 10× fine steps
            int steps = abs(delta) * 10;
            for (int i = 0; i < steps; i++)
                fnc_realtime(delta > 0 ? SpindleOvrFinePlus : SpindleOvrFineMinus);
            updateSpindleOverrideDisplay();
        }
        return;
    } else if (currentPendantScreen == PSCREEN_FLUIDNC) {
        // Toggle display rotation. NVS write is deferred to exitFluidNC() —
        // a rapid spin would otherwise hammer flash with redundant writes.
        // The pending flag tells exitFluidNC() that the rotation differs from
        // what's stored on flash and a putInt() is required.
        static unsigned long lastRotationMs = 0;
        if (millis() - lastRotationMs > 300) {
            int newRot = (pendantMachine.rotation == 2) ? 0 : 2;
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                pendantMachine.rotation        = newRot;
                pendantMachine.displayRotation = (newRot == 2) ? "Normal" : "Upside Down";
                xSemaphoreGive(stateMutex);
            }
            display.setRotation(newRot);
            pendantMachine.rotationDirty = true;
            drawCurrentPendantScreen();
            lastRotationMs = millis();
        }
    }
}

void saveJogPrefs() {
    preferences.begin("pendant", false);
    preferences.putBool("jogFineMode", pendantJog.fineIncrements);
    preferences.putInt("jogSelInc",    pendantJog.selectedIncrement);
    preferences.end();
}

// ===== Sprite Periodic Update (Core 1, 100ms cadence) =====
static void updateCurrentScreenSprites() {
    switch (currentPendantScreen) {
        case PSCREEN_MAIN_MENU:
            updateMainMenuDisplay();
            break;
        case PSCREEN_JOG_HOMING:
            updateJogAxisDisplay();
            break;
        case PSCREEN_PROBING_WORK:
            updateWorkMachinePos();
            updateWorkAreaPos();
            break;
        case PSCREEN_FEEDS_SPEEDS:
            updateFeedsSpeedsTopDisplay();
            updateFeedOverrideDisplay();
            updateSpindleOverrideDisplay();
            break;
        case PSCREEN_SPINDLE_CONTROL:
            updateSpindleRPMDisplay();
            break;
        case PSCREEN_PROBE:            updateProbeScreen();       break;
        case PSCREEN_PROBE_Z:          updateProbeZScreen();      break;
        case PSCREEN_PROBE_CORNER:     updateProbeCornerScreen(); break;
        case PSCREEN_PROBE_BORE:       updateProbeBoreScreen();   break;
        case PSCREEN_PROBE_BOSS:       updateProbeBossScreen();   break;
        case PSCREEN_STATUS:
            updateStatusMachineStatus();
            updateStatusCurrentFile();
            updateStatusAxisPositions();
            updateStatusFeedSpindle();
            break;
        case PSCREEN_FLUIDNC:
            updateFluidNCDisplay();
            break;
        case PSCREEN_WIFI_SETUP:
            updateWiFiSetupDisplay();
            break;
        case PSCREEN_SD_CARD:
            updateSDCardFileList();
            break;
        case PSCREEN_MACROS:
            updateMacrosFileList();
            break;
        default:
            break;
    }
    // Refresh title-bar icons on every periodic tick — cheap direct draw.
    // The title bar is never occupied by sprites so these are always safe to call.
    drawWiFiIcon();
    drawBatteryIcon();
}

// ===== Static controller config items =====
// All read once on the connection edge (HwEvent::CONNECTED) and cached.
// Screens read the cached values directly — no per-screen UART round-trips.
//   $30, $31      — spindle max / min RPM
//   $110          — X-axis max rate (jog feed cap)
//   $130-$133     — per-axis max travel (used to clamp per-tick jog distance)
// Values land in pendantMachine / pendantJog via PendantScene::reDisplay() callbacks.
static IntConfigItem spindleMaxItem ("$30");
static IntConfigItem spindleMinItem ("$31");
static IntConfigItem jogMaxRateItem ("$110");
static IntConfigItem jogMaxTravelX  ("$130");
static IntConfigItem jogMaxTravelY  ("$131");
static IntConfigItem jogMaxTravelZ  ("$132");
static IntConfigItem jogMaxTravelA  ("$133");
static IntConfigItem jogHomingDirMask("$23");   // homing direction invert mask (envelope sign, per axis)

// Called from loop_pendant() when HwEvent::CONNECTED arrives.
// FluidNC version, IP address, WiFi SSID arrive automatically via [VER:] / status
// callbacks once a connection is established — no explicit query required.
// Helper: send a settings query without waiting for the "ok" ack.
//
// The library's fnc_send_line() spins on Core 1 waiting for the *previous*
// command's ack at the START of every call, up to a 2 s timeout per call.
// Calling it 7 times in a row over WiFi (where TCP latency can briefly stall
// FluidNC's reply path) can therefore block Core 1 for up to 14 s — well
// past the 5 s loop-task watchdog.  The captured Core 0 stage being 101
// (drain) was a red herring; the watchdog was actually firing because the
// Arduino loop task on Core 1 hadn't returned to the framework in time.
//
// The fix: bypass fnc_send_line's ack-wait entirely for these read-only
// settings queries.  We just put the bytes on the wire and let the existing
// GrblParser state machine parse the responses asynchronously (it already
// does — that's how it populates the ConfigItem values).  Nothing else
// depends on a synchronous ack here.
static void sendQueryRaw(const char* s) {
    // Hold the TX-line lock so this multi-byte query can't interleave in the
    // WiFi TX ring with a line command being pushed from the other core.
    bool locked = txLineLock();
    while (*s) fnc_putchar((uint8_t)*s++);
    fnc_putchar('\n');
    if (locked) txLineUnlock();
}

static void requestControllerConfig() {
    extern uint32_t rtcCore1Stage;   // defined in ardmain.cpp
    rtcCore1Stage = 200;       // requestControllerConfig start
    sendQueryRaw("$30");       // spindle max RPM
    sendQueryRaw("$31");       // spindle min RPM
    sendQueryRaw("$110");      // jog max feed rate
    sendQueryRaw("$130");      // X travel
    sendQueryRaw("$131");      // Y travel
    sendQueryRaw("$132");      // Z travel
    sendQueryRaw("$133");      // A travel
    sendQueryRaw("$23");       // homing direction mask (per-axis envelope sign)
    rtcCore1Stage = 208;       // requestControllerConfig done
}

// Called from enterSpindleControl() — defensive re-fetch of $30/$31. Restores
// the v1.5.5 behaviour where the spindle screen always sees fresh values, in
// case the connect-edge fetch was dropped (the user reported max/min reverting
// to defaults after Start/Stop). Cheap: just two short UART queries on entry.
void requestSpindleConfig() {
    if (!pendantConnected) return;
    spindleMaxItem.init();
    spindleMinItem.init();
}

// ===== Macro request — reads preferences.json (then macrocfg.json fallback) via UART =====
// Macros are NOT static config — they can change as the user edits FluidNC's
// preferences. Loaded on macros-screen entry, with a Refresh button to re-fetch.
void requestMacros() {
    pendantMacros.loading     = true;
    pendantMacros.count       = 0;
    pendantMacros.selected    = -1;
    pendantMacros.loadFailed  = false;
    pendantMacros.loadStartMs = millis();   // arm the UI loading deadline

    // Clear the WebSocket JSON-parser latches before every macros fetch.  These
    // can stay stuck `true` if a previous file/JSON transfer (e.g. an SD-card
    // listing) dropped mid-stream — and while latched, handle_other() shovels
    // every incoming raw line into the JSON parser, corrupting its state.  Safe
    // to clear here: nothing is mid-transfer at macros-screen entry.
    g_expecting_json    = false;
    g_json_accumulating = false;


#ifdef USE_WIFI
    if (comms_active_mode() == COMMS_MODE_WIFI) {
        // Over WiFi, fetch the macros file via HTTP (like FluidNC's WebUI).  The
        // WebSocket $File/SendJSON path truncates/drops on the large
        // preferences.json reply; HTTP is reliable.
        request_macros_http();
        return;
    }
#endif
    request_macros();  // UART: $File/SendJSON chain (preferences.json → macrocfg.json)
}

// ===== PendantScene: bridges FluidNC callbacks → pendantMachine (Core 0) =====
class PendantScene : public Scene {
public:
    PendantScene() : Scene("Pendant") {}

    void onDROChange() override {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            pendantMachine.numAxes       = n_axes;
            // posX/Y/Z/A = work pos (DRO values, used by all position displays)
            pendantMachine.posX          = myAxes[0] / 10000.0f;
            pendantMachine.posY          = (n_axes > 1) ? myAxes[1] / 10000.0f : 0.0f;
            pendantMachine.posZ          = (n_axes > 2) ? myAxes[2] / 10000.0f : 0.0f;
            pendantMachine.posA          = (n_axes > 3) ? myAxes[3] / 10000.0f : 0.0f;
            // workX/Y/Z/A = machine pos (absolute, used by Work Area screen)
            pendantMachine.workX         = myMachineAxes[0] / 10000.0f;
            pendantMachine.workY         = (n_axes > 1) ? myMachineAxes[1] / 10000.0f : 0.0f;
            pendantMachine.workZ         = (n_axes > 2) ? myMachineAxes[2] / 10000.0f : 0.0f;
            pendantMachine.workA         = (n_axes > 3) ? myMachineAxes[3] / 10000.0f : 0.0f;
            pendantMachine.feedRate      = (int)myFeed;
            pendantMachine.spindleRPM    = (int)mySpeed;
            pendantMachine.feedOverride  = (int)myFro;
            pendantMachine.spindleOverride = (int)mySro;
            pendantMachine.currentFile = (myFile && *myFile) ? myFile : "";
            pendantMachine.jobPercent  = (int)myPercent;
            // Refresh the live status on EVERY report.  onStateChange() only
            // fires on a state *change*, and its `state != new_state` guard gets
            // skipped when `state` and `my_state_string` desync — e.g. a
            // [MSG:RST] sets state=Disconnected without clearing my_state_string,
            // after which identical "Idle" reports no longer trip onStateChange.
            // That left the Main Menu / Status bar stuck on a stale "N/C" while
            // the link was actually fine (FluidNC page still showed Connected,
            // SD refresh still worked).  Copying my_state_string here can't get
            // stuck; the "N/C" sentinel is filtered so it never displays.
            if (my_state_string && my_state_string[0] &&
                strcmp(my_state_string, "N/C") != 0) {
                pendantMachine.status = my_state_string;
            }
            xSemaphoreGive(stateMutex);
        }

        // While homing, track WHICH axis is actively moving so the jog/homing
        // screen's big DRO shows the axis currently being homed.  FluidNC homes
        // axes one at a time (and, for $H, in sequence) but doesn't announce
        // which — however only the active axis's machine position changes
        // between status reports, so the axis with the largest delta is the one
        // homing right now.  This walks "Home All" through the axes live.
        //
        // Crucially this drives the TRANSIENT pendantJog.homingAxis, never
        // selectedAxis — so when homing ends we clear homingAxis and the big DRO
        // returns to the user's jog-button-selected axis automatically.
        {
            static int32_t prevMachine[4] = { 0, 0, 0, 0 };
            static bool    prevValid      = false;
            static bool    wasHoming      = false;
            bool nowHoming = (state == Homing);

            if (nowHoming && prevValid) {
                int     bestAxis  = -1;
                int32_t bestDelta = 0;
                for (int i = 0; i < n_axes && i < 4; i++) {
                    int32_t d = myMachineAxes[i] - prevMachine[i];
                    if (d < 0) d = -d;
                    if (d > bestDelta) { bestDelta = d; bestAxis = i; }
                }
                // 0.05 mm (500 in 1/10000 units) ignores measurement jitter.
                if (bestAxis >= 0 && bestDelta > 500) {
                    pendantJog.homingAxis = bestAxis;
                }
            }
            // Homing just finished → drop back to the jog-selected axis.
            if (wasHoming && !nowHoming) {
                pendantJog.homingAxis = -1;
            }
            wasHoming = nowHoming;
            for (int i = 0; i < 4; i++) prevMachine[i] = myMachineAxes[i];
            prevValid = true;
        }

        // Promote to "synced" once the link is up and the post-connect config
        // queries have had a moment to reply.  onDROChange() fires on EVERY
        // status report, so simply reaching here proves data is flowing — we
        // deliberately DON'T gate on onStateChange(), which only fires on a
        // state *change* and is silent when the machine is already Idle at the
        // moment we connect (that left "Connecting" stuck).  Until promoted the
        // main menu / status screen show "Connecting" (both WiFi and wired).
        if (pendantConnected && !pendantSynced &&
            syncConnectMs != 0 && (millis() - syncConnectMs) >= 800) {
            pendantSynced = true;
        }

        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }

    void onStateChange(state_t /*newState*/) override {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            pendantMachine.status           = my_state_string;
            pendantMachine.connectionStatus = "Connected";
            pendantMachine.inInches         = inInches;
            if (wifi_ip.length())   pendantMachine.ipAddress = String(wifi_ip.c_str());
            if (wifi_ssid.length()) pendantMachine.wifiSSID  = String(wifi_ssid.c_str());
            xSemaphoreGive(stateMutex);
        }
        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }

    void onFilesList() override {
        // Called from Core 0 when JSON/file-list parsing completes.
        // Routes to macros or SD card depending on which screen requested data.

        // Clear the loading flag FIRST, OUTSIDE the mutex.  `loading` is a plain
        // bool (atomic on Xtensa), and it must clear even if the brief
        // stateMutex take below fails under Core 1 UI contention.  Previously
        // loading=false lived inside the mutex, so a single missed acquisition
        // (the 10 ms timeout expiring while Core 1 held the lock during a
        // redraw) left the screen stuck on "Loading…" forever even though the
        // parse had completed.  This was THE remaining SD/macros bug after the
        // raw-JSON routing fix — the diagnostic showed fl>0 (parse done) yet
        // the screen still said Loading.
        if (currentPendantScreen == PSCREEN_MACROS) { pendantMacros.loading = false; pendantMacros.loadFailed = false; }
        else                                        { pendantSdCard.loading = false; pendantSdCard.loadFailed = false; }

        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (currentPendantScreen == PSCREEN_MACROS) {
                // Populate from the 'macros' vector filled by FileParser listeners
                pendantMacros.count      = 0;
                pendantMacros.cacheValid = true;  // mark cache warm for re-entry
                for (auto* m : macros) {
                    if (pendantMacros.count >= 20) break;
                    if (m->name.empty()) continue;
                    pendantMacros.content[pendantMacros.count]  = String(m->name.c_str());
                    pendantMacros.filename[pendantMacros.count] = String(m->filename.c_str());
                    pendantMacros.count++;
                }
            } else {
                // SD card file list from $Files/ListGCode
                pendantSdCard.fileCount    = 0;
                pendantSdCard.scrollOffset = 0;
                for (auto& fi : fileVector) {
                    if (!fi.isDir() && pendantSdCard.fileCount < 20) {
                        pendantSdCard.files[pendantSdCard.fileCount++] = String(fi.fileName.c_str());
                    }
                }
            }
            xSemaphoreGive(stateMutex);
        }
        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }

    void onError(const char* /*errstr*/) override {
        // Macros fetch finished with nothing to show.  Distinguish the two cases
        // so the message is accurate: if the file WAS served (HTTP 200) it just
        // has no macros configured → "No macros found"; if no attempt reached it
        // → a real load failure → "Couldn't load — tap Refresh".  (UART mode
        // leaves g_macros_http_served false, so it reports a load failure, which
        // matches its $File chain having produced no usable reply.)
        if (currentPendantScreen == PSCREEN_MACROS) {
            pendantMacros.loading    = false;
            pendantMacros.count      = 0;
#ifdef USE_WIFI
            pendantMacros.loadFailed = !g_macros_http_served;
#else
            pendantMacros.loadFailed = true;
#endif
            if (hwEventQueue) {
                HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
                xQueueSend(hwEventQueue, &ev, 0);
            }
        }
    }

    void reDisplay() override {
        // Copy any newly-received config values into pendantMachine / pendantJog
        if (spindleMaxItem.known() || spindleMinItem.known()) {
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (spindleMaxItem.known()) pendantMachine.spindleMaxRPM = spindleMaxItem.get();
                if (spindleMinItem.known()) pendantMachine.spindleMinRPM = spindleMinItem.get();
                xSemaphoreGive(stateMutex);
            }
        }
        if (jogMaxRateItem.known()) {
            int rate = jogMaxRateItem.get();
            if (rate > 0) pendantJog.maxFeedRate = rate;
        }
        // $130-$133: per-axis max travel (mm), used as a per-tick distance clamp
        if (jogMaxTravelX.known()) { int v = jogMaxTravelX.get(); if (v > 0) pendantJog.maxTravel[0] = v; }
        if (jogMaxTravelY.known()) { int v = jogMaxTravelY.get(); if (v > 0) pendantJog.maxTravel[1] = v; }
        if (jogMaxTravelZ.known()) { int v = jogMaxTravelZ.get(); if (v > 0) pendantJog.maxTravel[2] = v; }
        if (jogMaxTravelA.known()) { int v = jogMaxTravelA.get(); if (v > 0) pendantJog.maxTravel[3] = v; }
        // $23 homing direction mask — 0 is a valid value (all axes home +), so key
        // off known() rather than a >0 guard.  Drives the per-axis jog envelope sign.
        if (jogHomingDirMask.known()) pendantJog.homingDirMask = jogHomingDirMask.get();

        // Macro list is populated in onFilesList() when $File/SendJSON response arrives
        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }
};

static PendantScene pendantScene;

// Called by GrblParser when a [VER:] report arrives from FluidNC
extern "C" void show_versions(const char* /*grbl_version*/, const char* fluidnc_version) {
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        pendantMachine.fluidNCVersion = fluidnc_version;
        xSemaphoreGive(stateMutex);
    }
}

// ===== Core 0 Hardware Task =====
extern uint32_t rtcLastBootStage;   // Core 0 (pendant_hw_task) — defined in ardmain.cpp
extern uint32_t rtcCore1Stage;      // Core 1 (Arduino loop task)
extern uint32_t rtcCore0Iters;      // pendant_hw_task iteration count
extern uint32_t rtcCore1Iters;      // loop_pendant iteration count

// NVS-backed diagnostic checkpoint.  Written periodically from Core 0; read
// once on boot.  Survives power-cycles (unlike RTC memory), so a freeze that
// requires manual power-off still leaves diagnostic evidence behind.
static uint32_t _minHeapSinceBoot = 0xFFFFFFFFu;

static void writeDiagCheckpoint() {
    uint32_t nowHeap = ESP.getFreeHeap();
    if (nowHeap < _minHeapSinceBoot) _minHeapSinceBoot = nowHeap;
    Preferences prefs;
    if (!prefs.begin("diag", false)) return;
    prefs.putUInt("c0_iters", rtcCore0Iters);
    prefs.putUInt("c1_iters", rtcCore1Iters);
    prefs.putUInt("c0_stage", rtcLastBootStage);
    prefs.putUInt("c1_stage", rtcCore1Stage);
    prefs.putUInt("min_heap", _minHeapSinceBoot);
    prefs.putUInt("now_heap", nowHeap);
    prefs.end();
}

// Called once at start of pendant_hw_task to load the PREVIOUS boot's last
// NVS checkpoint into globals so the WiFi setup screen can show them.
uint32_t nvsPrevIter0   = 0;
uint32_t nvsPrevIter1   = 0;
uint32_t nvsPrevMinHeap = 0;
uint32_t nvsPrevNowHeap = 0;
static void readDiagCheckpoint() {
    Preferences prefs;
    if (!prefs.begin("diag", true)) return;     // read-only
    nvsPrevIter0   = prefs.getUInt("c0_iters", 0);
    nvsPrevIter1   = prefs.getUInt("c1_iters", 0);
    nvsPrevMinHeap = prefs.getUInt("min_heap", 0);
    nvsPrevNowHeap = prefs.getUInt("now_heap", 0);
    prefs.end();
}
// ────────────────────────────────────────────────────────────────────────────
// Core 0 COMMS task — owns every byte that crosses the network/UART boundary.
//
// Rationale: the ESP-IDF WiFi driver and lwIP TCP/IP stack are pinned to Core
// 0 by default.  Putting our byte-level work on the same core eliminates
// cross-core data movement (TCP RX → ring buffer → parser → callbacks all
// happen on Core 0) and means the WiFi driver doesn't have to wake up our
// task on the other core just to feed bytes to it.
//
// Application work (encoder, buttons, battery, UI) is on Core 1 via
// pendant_hw_task and the Arduino loop task, so this task stays focused
// on comms.
// ────────────────────────────────────────────────────────────────────────────
void pendant_comms_task(void* /*pvParameters*/) {
    dbg_println("PendantComms task started on Core 0");
    rtcLastBootStage = 4;     // stage 4: comms task entered

    // Load previous-boot diagnostic snapshot from NVS for the WiFi Setup
    // screen to display.  Survives across power-cycles, so freezes that
    // forced a manual power-off still leave evidence behind.
    readDiagCheckpoint();
    unsigned long lastDiagCheckpointMs = millis();

    // Pick comms backend (UART or WiFi) and initialise only that one.
    // wifi_init() spins up the radio when WiFi is selected; in UART mode
    // it is never called and the WiFi radio stays cold.  After this call
    // the hot path is a single indirect function call per byte.
    comms_init();
    dbg_printf("Comms: active transport = %s\n", comms_mode_name());
    rtcLastBootStage = 5;     // stage 5: comms_init done

    // Send first $? immediately — fnc_is_connected() uses a 'starting' flag
    // so the very first call fires the ping right away.
    fnc_is_connected();
    unsigned long lastPingMs = millis();

    // Demo-mode guard: only declare "connected" (and fire CONNECTED events) once at
    // least one UART byte has arrived from the controller.  fnc_is_connected() is
    // time-based and returns true ~200 ms after boot even with no controller attached.
    // Without this flag every boot triggers requestControllerConfig() on Core 1, which
    // calls fnc_send_line() 7 times — each one busy-waits up to 2 s for an ack that
    // never arrives, blocking the UI loop for 10–16 s and making the device appear
    // completely unresponsive to touch.
    bool rxEverSeen = false;

    // ── Physical buttons live on Core 0 (this task) ──────────────────────────
    // Moved here from pendant_hw_task (Core 1) deliberately.  Two reasons:
    //   1. Robustness: reset / feed-hold / cycle-start / power-off must work
    //      even if the Core 1 UI loop is wedged (drawing, touch, a stuck
    //      scheduled action, etc.).  Core 0 owns comms and is the task least
    //      likely to stall, so critical controls belong here.
    //   2. Latency: the red-button post-reset "$X" is an ack-waiting send.
    //      Issued from Core 1 it could only spin waiting for Core 0 to clear
    //      _ackwait across cores (slow / sometimes seconds).  On Core 0 it
    //      self-services through ws_getchar's transport pump in a round-trip.
    // The encoder and battery sampling stay on Core 1 (jog uses non-blocking
    // sends, and battery is not time-critical).
    unsigned long btnLastDebounce[3] = { 0, 0, 0 };
    bool          btnLastRaw[3]       = { true, true, true };
    bool          btnState[3]         = { true, true, true };
    bool          btnHandled[3]       = { false, false, false };
    const int     btnPins[3]          = { red_button_pin, dial_button_pin, green_button_pin };
    bool          redResetPending     = false;
    unsigned long redResetMs          = 0;
    bool          redHolding          = false;
    unsigned long redHoldStartMs      = 0;
    // Power-off fallback: when the 5 s long-press fires we post POWER_OFF to
    // Core 1 (which draws the "powering off" screen, then deep-sleeps).  If
    // Core 1 is wedged and doesn't sleep, Core 0 force-sleeps after a deadline
    // so the long-press ALWAYS powers the unit down.
    bool          powerOffRequested   = false;
    unsigned long powerOffDeadlineMs  = 0;

    for (;;) {
        // Granular boot-stage markers — each step writes its number so a
        // post-crash boot can show exactly which step was blocking when the
        // watchdog fired.  100 = "entering loop", 101..108 = sub-steps.
        //   100 = top of iteration
        //   101 = comms_poll done
        //   102 = byte drain done
        //   103 = fnc_is_connected / ping done
        //   104 = encoder read done
        //   105 = button checks done
        //   106 = battery sample done
        //   107 = WiFi state cache done
        //   108 = vTaskDelay about to run (end of iteration)
        rtcLastBootStage = 100;
        rtcCore0Iters++;       // iteration counter — distinguishes "iterating" from "stuck"

        // Comms backend service hook.  In UART mode this is a no-op.  In WiFi
        // mode it refills the TCP→RX ring buffer, handles reconnects, and
        // drives the AP HTTP server during captive-portal setup.  Always
        // runs on Core 0 (this task), so the WiFi RX ring buffer is never
        // touched from two cores.
        comms_poll();
        rtcLastBootStage = 101;

        // Drain ALL available bytes in one task cycle (UART or WiFi ring buffer).
        // The original fnc_poll() reads exactly 1 byte per call, so at 2ms/cycle
        // the old single call gave only ~500 B/s — enough for normal status reports
        // but far too slow for large JSON files (e.g. preferences.json can be 10+ KB,
        // taking 20+ seconds to receive at 500 B/s).
        // collect() and fnc_getchar() are both exported from GrblParserC.h.
        // poll_extra() (debug serial forwarding) is called once after the drain.
        {
            // Bound the drain per task tick so a burst can't keep us spinning
            // here for tens of ms (the loop also debounces buttons, reads the
            // encoder, feeds the watchdog).  Budget is one full RX ring (8 KB):
            // big enough to absorb a whole FluidNC send burst — e.g. the macros
            // preferences.json reply — in a single tick so it can't back up and
            // overflow the ring, but still bounded.  512 was too small: a large
            // reply arrived faster than it drained, the ring overflowed, and
            // the JSON corrupted (macros never loaded; the smaller SD listing
            // squeaked through).
            int c;
            int budget = 8192;
            while (budget-- > 0 && (c = fnc_getchar()) >= 0) {
                collect((uint8_t)c);
                rxEverSeen = true;  // real controller data observed (UART path)
            }
            poll_extra();
        }
        // WiFi/WebSocket path feeds collect() directly from onWsEvent, so the
        // drain loop above never runs for it and never latches rxEverSeen.
        // fnc_rx_ever_seen() is set transport-agnostically in update_rx_time()
        // (called on every UART byte AND every WebSocket frame), so OR it in
        // here.  Without this the WiFi connection never "completes": real data
        // flows and the version even updates, but pendantConnected stays false
        // because its transition is gated on rxEverSeen.
        if (fnc_rx_ever_seen()) rxEverSeen = true;
        rtcLastBootStage = 102;

        // Drive ping + connection state from Core 0.
        // Ping interval is adaptive: slow down to 1000ms while the machine is Running
        // so the $? realtime byte doesn't add UART load during active motion.
        // When idle/stopped/alarm, keep 200ms for snappy connection detection.
        unsigned long nowMs    = millis();
        bool          running  = pendantMachine.status.startsWith("Run");
        unsigned long pingInterval = running ? 1000UL : 200UL;
        if (nowMs - lastPingMs >= pingInterval) {
            bool connected = fnc_is_connected();
            // Gate pendantConnected on rxEverSeen: suppress the spurious "connected"
            // transition that fnc_is_connected() produces at startup with no controller.
            // A real controller sends bytes within ms of boot; no bytes = demo mode.
            // Allow the false→false and true→false (disconnect) paths through always
            // so a live connection loss is never masked.
            if (connected != pendantConnected && (rxEverSeen || !connected)) {
                pendantConnected = connected;
                // Every edge (connect OR disconnect) forces a fresh resync, so
                // the main menu shows "Connecting" until live state flows again.
                pendantSynced = false;
                syncConnectMs = connected ? millis() : 0;
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    pendantMachine.connectionStatus = connected ? "Connected" : "N/C";
                    xSemaphoreGive(stateMutex);
                }
                // On connect edge, ask Core 1 to fetch all static controller config.
                // Reconnects auto-refresh because the edge fires again on each transition.
                if (connected && hwEventQueue) {
                    HwEvent ev = { HwEvent::CONNECTED, 0 };
                    xQueueSend(hwEventQueue, &ev, 0);
                }
            }
            lastPingMs = nowMs;
        }
        rtcLastBootStage = 103;

        // ── Physical buttons (Core 0) ────────────────────────────────────────
        // Debounce + realtime commands + post-reset $X + long-press power-off.
        // Realtime bytes (Reset/FeedHold/CycleStart) and the $X line are sent
        // from this task, so they reach FluidNC even if Core 1's UI is stuck,
        // and the ack-waiting $X self-services via the transport pump.
        {
            unsigned long bnow = millis();
            for (int i = 0; i < 3; i++) {
                if (btnPins[i] < 0) continue;
                bool raw = (digitalRead(btnPins[i]) == HIGH);  // HIGH = not pressed
                if (raw != btnLastRaw[i]) btnLastDebounce[i] = bnow;
                btnLastRaw[i] = raw;

                if ((bnow - btnLastDebounce[i]) > 30) {
                    bool pressed = !raw;
                    if (pressed && btnState[i] && !btnHandled[i]) {
                        btnHandled[i] = true;
                        switch (i) {
                            case 0:  // Red → soft reset; $X follows 500 ms later
                                fnc_realtime(Reset);
                                redResetPending = true;
                                redResetMs      = bnow;
                                redHolding      = true;
                                redHoldStartMs  = bnow;
                                break;
                            case 1:  // Dial → pause motion
                                fnc_realtime(FeedHold);
                                break;
                            case 2:  // Green → start / resume
                                fnc_realtime(CycleStart);
                                break;
                        }
                        if (hwEventQueue) {
                            static const HwEvent::Type types[] = {
                                HwEvent::BUTTON_RED, HwEvent::BUTTON_YELLOW, HwEvent::BUTTON_GREEN
                            };
                            HwEvent ev = { types[i], 0 };
                            xQueueSend(hwEventQueue, &ev, 0);
                        }
                    }
                    if (!pressed) {
                        btnHandled[i] = false;
                        if (i == 0) redHolding = false;  // released before threshold
                    }
                    btnState[i] = !pressed;
                }
            }

            // Post-reset $X, 500 ms after the Red press.  On Core 0 this
            // ack-waiting send self-services via ws_getchar's pump.
            if (redResetPending && (millis() - redResetMs >= 500)) {
                send_line("$X");
                redResetPending = false;
            }

            // Red long-press (5 s) → power off.  Post the event so Core 1 can
            // draw the shutdown screen, and arm a force-sleep fallback.
            if (redHolding && (millis() - redHoldStartMs >= 5000)) {
                redHolding      = false;
                redResetPending = false;
                // Gracefully close the WebSocket HERE, on Core 0, before either
                // core deep-sleeps — sends a CLOSE frame so FluidNC frees the
                // channel immediately and latches a flag so wifi_poll() won't
                // reopen it during the ~2.5 s window before sleep.  Safe to
                // touch _wsClient: we're on Core 0 and not inside its loop().
                #ifdef USE_WIFI
                if (comms_active_mode() == COMMS_MODE_WIFI) {
                    wifi_graceful_disconnect();
                }
                #endif
                if (hwEventQueue) {
                    HwEvent ev = { HwEvent::POWER_OFF, 0 };
                    xQueueSend(hwEventQueue, &ev, 0);
                }
                powerOffRequested  = true;
                powerOffDeadlineMs = millis() + 2500;  // Core 1 should sleep first
            }
            // Fallback: if Core 1 didn't deep-sleep within the deadline (UI
            // wedged), power down from Core 0 so the long-press never fails.
            if (powerOffRequested && (int32_t)(millis() - powerOffDeadlineMs) >= 0) {
                powerOffRequested = false;
                dbg_println("Power-off fallback from Core 0 (UI did not sleep)");
                deep_sleep(0);  // never returns
            }
        }

        // Active status polling for WiFi/WebSocket.
        //
        // fnc_is_connected()'s built-in poll only fires every ping_interval_ms
        // (4 s) and only when nothing else has been received — a cadence tuned
        // for UART, where FluidNC's serial channel auto-reports continuously so
        // the pendant rarely needs to ask.  Over WebSocket we can't rely on the
        // controller volunteering status on its own (the per-channel auto-report
        // isn't guaranteed to be active), which left the DRO / machine-state
        // frozen at its initial "N/C" until some other command happened to
        // provoke a reply.  So in WiFi mode we explicitly request a status
        // report ('?') every 250 ms.  '?' is a realtime byte: it's enqueued on
        // the TX ring and shipped by tx_drain() on Core 0, never blocks, and
        // FluidNC answers within a round-trip.  This does not touch any FluidNC
        // setting (unlike $Report/Interval), so the user's machine config is
        // left exactly as they have it.
        #ifdef USE_WIFI
        if (comms_active_mode() == COMMS_MODE_WIFI && websocket_is_connected()) {
            static unsigned long lastWsStatusPoll = 0;
            if (nowMs - lastWsStatusPoll >= 250) {
                lastWsStatusPoll = nowMs;
                fnc_realtime(StatusReport);   // '?'
            }
        }
        #endif

        // Drain the pending-nowait counter if no acks have arrived recently.
        // Self-heals from the rare case where an atomic TCP send fails before
        // delivering a queued command (so the expected ok will never come)
        // and would otherwise leave the jog throttle stuck high.
        nowait_pending_decay();

        // WiFi state cache — sample on Core 0 (the task that owns the WiFi
        // state machine) and publish to pendantMachine so Core 1's UI can
        // read without touching the WiFi.h API across cores.
        #ifdef USE_WIFI
        static unsigned long lastWifiSampleMs = 0;
        if (comms_active_mode() == COMMS_MODE_WIFI &&
            (millis() - lastWifiSampleMs) >= 500) {
            int  bars = wifi_signal_bars();   // reads WiFi.RSSI() — safe on Core 0
            bool ap   = wifi_in_ap_mode();
            pendantMachine.wifiSignalBars = bars;
            pendantMachine.wifiInApMode   = ap;
            lastWifiSampleMs = millis();
        }
        #endif
        rtcLastBootStage = 107;

        // Diagnostic checkpoint to NVS every 30 s.  Cheap (~10ms write),
        // infrequent (~2880 writes/day → ~1+ year NVS sector life with
        // wear-leveling).  Captures the last healthy state before a freeze
        // that requires manual power-off and wipes RTC memory.
        if (millis() - lastDiagCheckpointMs >= 30000) {
            writeDiagCheckpoint();
            lastDiagCheckpointMs = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Core 1 HARDWARE task — encoder, buttons, battery.  No network/UART I/O.
//
// All comms work (byte send/receive, WiFi state, ping) is on Core 0 in
// pendant_comms_task above.  This task only polls things that are
// physically attached to the pendant itself and posts HwEvents to the
// queue for loop_pendant to handle.  Lives on Core 1 alongside the
// Arduino loop task — they share Core 1 round-robin at priority 1.
// ────────────────────────────────────────────────────────────────────────────
void pendant_hw_task(void* /*pvParameters*/) {
    dbg_println("PendantHw task started on Core 1");

    int16_t       lastEncCount    = get_encoder();
    // NOTE: physical button handling (red/dial/green debounce, soft-reset,
    // post-reset $X, long-press power-off) now lives in pendant_comms_task on
    // Core 0 so it stays responsive and robust even if this Core 1 task or the
    // UI loop stalls.  This task handles only the encoder and battery now.

    // Take one ADC sample immediately so Core 1 has a valid reading on the
    // very first drawTitle() call.  Charging status is low-priority; the
    // first read happens via the 60-second timer.
    {
        int pct = battery_level();
        int mv  = battery_millivolts();
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            pendantMachine.batteryPercent = pct;
            pendantMachine.batteryMv      = mv;
            xSemaphoreGive(stateMutex);
        }
    }
    unsigned long lastBatteryMs  = millis();
    unsigned long lastChargingMs = millis();

    for (;;) {
        // Encoder delta via PCNT.  Wired in 4x quadrature, so one physical
        // detent = 4 raw counts.  Accumulate until a full detent so slow
        // turns spread across multiple 5ms ticks still produce the correct
        // step count.
        {
            static int32_t encAccumulator = 0;
            int16_t encCount = get_encoder();
            int16_t rawDelta = encCount - lastEncCount;
            if (rawDelta != 0) {
                lastEncCount = encCount;
                encAccumulator += rawDelta;
                int32_t steps = encAccumulator / 4;
                if (steps != 0) {
                    encAccumulator -= steps * 4;
                    if (hwEventQueue) {
                        HwEvent ev = { HwEvent::ENCODER_DELTA, steps };
                        xQueueSend(hwEventQueue, &ev, 0);
                    }
                }
            }
        }

        // (Physical buttons are handled on Core 0 in pendant_comms_task.)

        // Battery voltage every 5 s (ADC read, no bus contention).
        if (millis() - lastBatteryMs >= 5000) {
            int pct = battery_level();
            int mv  = battery_millivolts();
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                pendantMachine.batteryPercent = pct;
                pendantMachine.batteryMv      = mv;
                xSemaphoreGive(stateMutex);
            }
            lastBatteryMs = millis();
        }

        // Charging status every 3 s — now a battery-VOLTAGE-TREND inference
        // (battery_charging()), not an IP5306 register read: the PMIC's charge
        // bits don't track reality on these boards.  Cheap ADC-only read; the
        // function holds off for a post-boot settling window then smooths the
        // trend internally, so the 3 s cadence just feeds it samples and the
        // icon settles within a couple of minutes (and won't false-trip at boot).
        if (millis() - lastChargingMs >= 3000) {
            bool charging = battery_charging();
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                pendantMachine.batteryCharging = charging;
                xSemaphoreGive(stateMutex);
            }
            lastChargingMs = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(2));  // 2ms → ~500 Hz button/encoder polling
    }
}

// ===== Public Interface =====
void setup_pendant() {
    // Release version shown on the FluidNC screen — maintained in
    // cnc_pendant_config.h (FIRMWARE_VERSION), bumped per release alongside
    // CHANGELOG.md.  Decoupled from git_info, which only tracks the latest git
    // tag and so lags between tagged releases.
    pendantMachine.fluidDialVersion = FIRMWARE_VERSION;

    // Load probe settings from NVS (must come before screen enter)
    loadProbeSettings();

    // Load saved display rotation and jog preferences
    preferences.begin("pendant", false);
    int  savedRotation = preferences.getInt("rotation",    2);
    bool savedFineInc  = preferences.getBool("jogFineMode", true);
    int  savedSelInc   = preferences.getInt("jogSelInc",    1);
    preferences.end();

    pendantJog.fineIncrements    = savedFineInc;
    // Clamp saved index to a valid increment slot — guards against a corrupted
    // NVS entry indexing past the 4-element increment table.
    pendantJog.selectedIncrement = constrain(savedSelInc, 0, 3);

    pendantMachine.rotation        = savedRotation;
    pendantMachine.displayRotation = (savedRotation == 2) ? "Normal" : "Upside Down";
    display.setRotation(savedRotation);

    dbg_printf("Pendant rotation: %s (%d)\n",
               pendantMachine.displayRotation.c_str(), savedRotation);

    // Force Disconnected state so the first real status report always triggers
    // the connection setup ($RI=200, $G, $I) — even if FluidNC is already running.
    set_disconnected_state();

    // Register scene so FluidNC callbacks update pendantMachine
    activate_scene(&pendantScene);

    // Enter initial screen (allocates sprites)
    callScreenEnter(currentPendantScreen);
    drawCurrentPendantScreen();

    dbg_println("CNC Pendant UI ready (Core 1)");
}

void loop_pendant() {
    // Core 1 stage map (in loop_pendant):
    //   1   entered loop_pendant
    //   2   action() callback done
    //   3   about to process queue (or queue empty)
    //   4   inside CONNECTED handler / requestControllerConfig
    //   5   inside STATE_UPDATE handler (updateCurrentScreenSprites)
    //   6   inside GREEN handler (SD-card run)
    //   7   inside POWER_OFF handler
    //   8   queue dispatch done
    //   9   periodic sprite refresh done
    //   10  touch handling done — about to return from loop_pendant
    rtcCore1Stage = 1;     // entered loop_pendant
    rtcCore1Iters++;       // iteration counter

    // Execute any action deferred by schedule_action() in FileParser / Scene code.
    // In the original FluidDial this runs inside dispatch_events(); we replicate
    // just that one step here so macro file requests (and their fallbacks) fire.
    extern ActionHandler action;  // Scene.cpp
    if (action) {
        ActionHandler a = action;
        action          = nullptr;
        a();
    }
    rtcCore1Stage = 2;     // action callback done

    // Periodic sprite-refresh timestamp. Declared here so the STATE_UPDATE
    // queue handler can reset it after a queue-driven sprite refresh — that
    // coalesces the periodic 100 ms tick with the event-driven update so we
    // don't redraw twice in quick succession when DRO updates arrive.
    static unsigned long lastSpriteUpdate = 0;

    rtcCore1Stage = 3;     // about to process queue

    // Process hardware events from Core 0
    HwEvent ev;
    while (xQueueReceive(hwEventQueue, &ev, 0) == pdTRUE) {
        switch (ev.type) {
            case HwEvent::ENCODER_DELTA:
                // Discard dial movement while asleep (touch-only wake; never jog
                // blind, and don't let queued detents fire a burst on wake).
                if (currentPendantScreen != PSCREEN_SLEEP) {
                    handleEncoderDelta(ev.value);
                    lastActivityMs = millis();
                }
                break;
            case HwEvent::BUTTON_RED:
                lastActivityMs = millis();
                break;
            case HwEvent::BUTTON_YELLOW:
                lastActivityMs = millis();
                break;
            case HwEvent::BUTTON_GREEN:
                lastActivityMs = millis();
                rtcCore1Stage = 6;     // inside GREEN handler
                // If a file has been loaded via the SD card Load button, run it now
                if (pendantSdCard.loadedFile.length() > 0 && pendantConnected) {
                    String cmd = "$SD/Run=" + pendantSdCard.loadedFile;
                    send_line(cmd.c_str());
                    pendantSdCard.loadedFile = "";
                    navigateTo(PSCREEN_STATUS);
                }
                break;
            case HwEvent::STATE_UPDATE:
                rtcCore1Stage = 5;     // inside STATE_UPDATE handler
                // Use the sprite-only update path to avoid fillScreen flicker.
                // Full drawXxxScreen() is only called on initial entry or user touch.
                updateCurrentScreenSprites();
                lastSpriteUpdate = millis();   // suppress duplicate periodic tick
                break;
            case HwEvent::CONNECTED:
                rtcCore1Stage = 4;     // inside CONNECTED handler
                // Connection edge: snapshot all static controller config so screens
                // never have to round-trip the UART on entry.
                requestControllerConfig();
                break;

            case HwEvent::POWER_OFF:
                rtcCore1Stage = 7;     // inside POWER_OFF handler
                // Draw shutdown screen, dim backlight, then enter deep sleep.
                // Green button press wakes the device (full reboot — not a resume).
                display.fillScreen(COLOR_BACKGROUND);
                drawTitle("POWERING OFF");
                display.setTextSize(2);
                display.setTextColor(COLOR_GRAY_TEXT);
                {
                    const char* l1 = "Press red button";
                    const char* l2 = "to power on";
                    display.setCursor((240 - display.textWidth(l1)) / 2, 130);
                    display.print(l1);
                    display.setCursor((240 - display.textWidth(l2)) / 2, 158);
                    display.print(l2);
                }
                delay(1500);
                display.setBrightness(0);
                delay(100);
                deep_sleep(0);  // never returns — ESP32 resets on green-button wakeup
                break;
        }
    }
    rtcCore1Stage = 8;     // queue dispatch done

    // ── Screen sleep management (WiFi pendants only) ──────────────────────────
    // Only WiFi (battery) pendants sleep — wired pendants are powered from the
    // controller and have no battery, so they power down with it and there's
    // nothing to blank.  Eligible to blank when the CNC is Idle OR while the
    // pendant is still "Connecting" (not connected) — both are no-activity
    // states.  Any connected-but-busy state (Run/Jog/Hold/Home/Alarm/…) keeps
    // it awake and resets the idle clock, so it never blanks mid-job.
    if (comms_active_mode() == COMMS_MODE_WIFI) {
        bool sleepEligible = !pendantConnected || pendantMachine.status.startsWith("Idle");
        if (!sleepEligible) {
            lastActivityMs = millis();
        }
        if (currentPendantScreen == PSCREEN_SLEEP) {
            // Wake if the machine becomes active while asleep (e.g. a job is started
            // from the WebUI, or an alarm fires) so it's never hidden behind the blank.
            if (pendantConnected && !pendantMachine.status.startsWith("Idle")) {
                navigateTo(sleepReturnScreen);
            }
        } else if (currentPendantScreen != PSCREEN_WIFI_SETUP
                   && sleepEligible
                   && (millis() - lastActivityMs >= SLEEP_TIMEOUT_MS)) {
            sleepReturnScreen = currentPendantScreen;
            navigateTo(PSCREEN_SLEEP);   // enterSleep() turns the backlight off
        }
    }

    // Periodic sprite refresh (100ms) — only fires if STATE_UPDATE didn't already
    // redraw.  Skipped while asleep (nothing visible; full redraw happens on wake).
    if (currentPendantScreen != PSCREEN_SLEEP && millis() - lastSpriteUpdate >= 100) {
        updateCurrentScreenSprites();
        lastSpriteUpdate = millis();
    }
    rtcCore1Stage = 9;     // periodic sprite refresh done

    // Touch input (200ms debounce).  swallowTouchUntilRelease guards the wake
    // touch: after a wake we ignore touches until the finger lifts, so a held
    // press/drag can't carry into a button on the restored screen.
    lgfx::touch_point_t tp;
    if (!display.getTouch(&tp)) {
        swallowTouchUntilRelease = false;          // finger lifted — re-arm dispatch
    } else if (!swallowTouchUntilRelease) {
        static unsigned long lastTouch = 0;
        if (millis() - lastTouch > 200) {
            lastActivityMs = millis();             // any touch counts as activity
            handlePendantTouch(tp.x, tp.y);        // on SLEEP → handleSleepTouch wakes
            lastTouch = millis();
        }
    }
    rtcCore1Stage = 10;    // loop_pendant about to return
}
