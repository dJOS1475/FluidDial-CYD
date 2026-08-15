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
#include "screens/screen_connection.h"
#include "screens/screen_espnow.h"

#include "Comms.h"
#ifdef USE_ESPNOW
// Display-side status queries only (link state, RSSI) for the title-bar icon.
// All byte-level I/O goes through the comms facade.
#include "PeerLink.h"
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
// Lines longer than GrblParser's line buffer, truncated to avoid overrunning it.
// Non-zero means a reply was too long to parse (see the drain loop).

// ===== Connection State =====
// Driven exclusively by fnc_is_connected() on Core 0 (backed by update_rx_time() per UART byte).
volatile bool pendantConnected = false;

// Sync tracking (all touched only on Core 0: the connection edge logic and the
// parser scene callbacks both run there).  pendantSynced gates the main menu's
// "Connecting" indicator — see pendant_shared.h.
volatile bool        pendantSynced  = false;
static unsigned long syncConnectMs  = 0;      // millis() at the connect edge

// ── Continuous-jog (MPG-style) dial-stop tracking ─────────────────────────────
// A rapid run of dial ticks is treated as a continuous jog; when the dial stops
// (no tick for JOG_STOP_MS) the periodic loop sends a real-time JogCancel so
// motion halts at once instead of coasting through the queued G91 moves — like a
// full-size handwheel.  A single deliberate detent stays below the "continuous"
// threshold, so it is never cancelled and completes its full commanded distance.
// All of these are touched only on Core 1 (pendant_comms_task), so no mutex.
static const unsigned long JOG_CONTINUOUS_MS = 100;   // ticks closer than this = a spin
static const unsigned long JOG_STOP_MS       = 150;   // ceiling — slowest spin keeps full margin
static const unsigned long JOG_STOP_MIN_MS   = 60;    // floor — fastest spin still needs jitter room
static unsigned long jogLastTickMs  = 0;
static unsigned long jogTickEmaMs   = 0;      // EMA of the inter-detent gap while spinning
static bool          jogContinuous  = false;
static int           jogRapidCount  = 0;
static bool          jogForceReseed = false;  // set on cancel → soft-limit predMm re-seeds

// How much dial silence means "the wheel stopped".
//
// A fixed timeout has to be sized for the SLOWEST spin — gaps can legitimately run
// up to JOG_CONTINUOUS_MS, so anything under ~150 ms risks firing between two
// detents the user is still turning (cancelling a jog they're actively commanding,
// which is worse than coasting).  But that worst-case margin was then paid on every
// spin: a fast spin with 20 ms gaps waited the full 150 ms to notice it had stopped.
//
// Scaling the timeout by the observed tick rate keeps the margin proportional rather
// than absolute.  A slow spin still gets the old 150 ms; a fast spin stops in 60 ms.
// Self-tuning, so it also adapts to different encoders and to how hard a given user
// spins.  Latency is only half the overshoot — the rest is FluidNC's decel ramp
// ($120/$121/$122), which no pendant-side change can shrink.
//
// KNOWN TRADE-OFF: because JOG_STOP_MIN_MS (60) is below JOG_CONTINUOUS_MS (100), a
// sustained fast spin followed by an ABRUPT pause longer than the window — but still
// short enough to count as one spin — cancels and then restarts on the next detent.
// Smooth deceleration does NOT trigger this (the EMA tracks it); it needs a >2.5x
// step change in one detent.  That behaviour is arguably correct for a handwheel —
// stop turning, motion stops — but if it feels like a stutter in use, raising
// JOG_STOP_MIN_MS to 105 (just above JOG_CONTINUOUS_MS) makes false cancels
// impossible by construction, at the cost of a 105 ms floor instead of 60 ms.
static inline unsigned long jogStopTimeoutMs() {
    if (jogTickEmaMs == 0) return JOG_STOP_MS;      // no measurement yet — be conservative
    unsigned long t = (jogTickEmaMs * 5) / 2;        // 2.5x the average gap
    if (t < JOG_STOP_MIN_MS) t = JOG_STOP_MIN_MS;
    if (t > JOG_STOP_MS)     t = JOG_STOP_MS;
    return t;
}

// ── Paced feed/spindle override stepper ───────────────────────────────────────
// FluidNC overrides are relative-only (reset / ±10% coarse / ±1% fine real-time
// bytes); an absolute target is reached by stepping.  Firing all the steps in a
// tight loop floods a Modbus VFD's command queue (→ "VFD Queue Full", wrong
// landing) and can starve FluidNC's network task into a watchdog reboot.  So we
// store a TARGET and walk toward it from the current REPORTED value — coarse then
// fine — one real-time byte every OVR_STEP_MS from the periodic loop.  Uses only
// realtime bytes, so it works during a running job; anchoring off the reported
// value means no reset-to-100% spike and the fewest possible steps.
static const unsigned long OVR_STEP_MS = 60;   // min gap between override bytes
struct OvrStepper {
    int           target     = -1;    // -1 = idle (no ramp in progress)
    int           commanded  = 100;   // running estimate of the % last commanded
    unsigned long lastSendMs = 0;
    bool          active     = false;
};
static OvrStepper feedOvr;
static OvrStepper spindleOvr;

static void ovrSetTarget(OvrStepper& s, int reported, int target) {
    target = constrain(target, 10, 200);
    if (!s.active) { s.commanded = constrain(reported, 10, 200); s.active = true; }
    s.target = target;
}
void overrideSetFeedTarget(int pct)    { ovrSetTarget(feedOvr,    pendantMachine.feedOverride,    pct); }
void overrideSetSpindleTarget(int pct) { ovrSetTarget(spindleOvr, pendantMachine.spindleOverride, pct); }

// One paced step toward the target: coarse (±10%) while ≥10 away, else fine (±1%).
static void ovrStep(OvrStepper& s, unsigned long now, realtime_cmd_t cP,
                    realtime_cmd_t cM, realtime_cmd_t fP, realtime_cmd_t fM) {
    if (s.target < 0) return;
    if (s.commanded == s.target) { s.target = -1; s.active = false; return; }
    if (now - s.lastSendMs < OVR_STEP_MS) return;
    s.lastSendMs = now;
    int gap = s.target - s.commanded;
    if      (gap >=  10) { fnc_realtime(cP); s.commanded += 10; }
    else if (gap <= -10) { fnc_realtime(cM); s.commanded -= 10; }
    else if (gap >   0)  { fnc_realtime(fP); s.commanded += 1;  }
    else                 { fnc_realtime(fM); s.commanded -= 1;  }
}

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
// Did the CURRENT begin/endPanelSprite pair actually render into the scratch?
// endPanelSprite() must not push when beginPanelSprite() handed back the display
// instead: the scratch still holds the LAST panel that used it (typically from a
// different screen), so pushing it would paint that stale image straight over
// the panel just drawn — a rapid alternation between two unrelated panels.
static bool        scratchInUse = false;

// Largest panel ANY screen draws.  This must be the true maximum across every
// screen, not the common case: the FluidNC panels are 230x70 and the
// Status/Jog/Menu DRO panels are 230x65.  Sizing this to the common case meant
// the taller panels grew the scratch at runtime — see beginPanelSprite().
//
// The Connection screen's LINK panel is 116 px tall and deliberately does NOT
// set the size here: at 16 bpp that would be 53 KB of heap the radio needs, and
// when the allocation lost the panel silently direct-drew and flickered.  It
// renders as two bands instead.  Raise this only if a panel cannot be banded.
#define PANEL_SCRATCH_W 230
#define PANEL_SCRATCH_H 70

void releasePanelSprites() {
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteStatusBar.deleteSprite();
    spriteFileDisplay.deleteSprite();
    // NOTE: spritePanelScratch is deliberately NOT freed here.  It is a single
    // ~30 KB contiguous allocation, and screens call this on every enter/exit —
    // so freeing it meant re-allocating 30 KB after every screen change.  With
    // the ESP-NOW radio running the heap is smaller and more fragmented than on
    // UART, that re-allocation intermittently FAILS, and beginPanelSprite()
    // then silently falls back to drawing straight to the display — which is
    // exactly the "much more flicker on ESP-NOW than UART" symptom.  Allocated
    // once by initPanelScratch() at boot and held for the life of the process.
}

// Allocate the shared panel scratch once, AFTER the radio is up.
//
// Ordering matters and it is not the obvious way round.  This used to run
// BEFORE comms_init() so the buffer got an unfragmented heap — but the scratch
// is cosmetic and the radio is not.  At 230x116 it wants 53 KB, and taking that
// first starved esp_now_init() / the WiFi stack: the radio silently failed to
// come up, so a paired machine never reconnected and pairing never saw a
// discovery packet.  The radio claims what it needs first; the scratch then
// takes the largest size still available.
//
// The ladder degrades gracefully: the full size fits every panel, 70 covers
// everything except the Connection LINK panel, 65 covers the DRO panels.  A
// panel bigger than whatever we got direct-draws (flicker) instead of failing.
void initPanelScratch() {
    if (spritePanelScratch.getBuffer()) return;

    static const int kHeights[] = { PANEL_SCRATCH_H, 70, 65 };
    for (int h : kHeights) {
        spritePanelScratch.setColorDepth(16);
        spritePanelScratch.createSprite(PANEL_SCRATCH_W, h);
        if (spritePanelScratch.getBuffer()) {
            scratchW = PANEL_SCRATCH_W;
            scratchH = h;
            dbg_printf("Panel scratch: %dx%d @16bpp (%d bytes), free heap %u\n",
                       PANEL_SCRATCH_W, h, PANEL_SCRATCH_W * h * 2,
                       (unsigned)ESP.getFreeHeap());
            return;
        }
        spritePanelScratch.deleteSprite();   // release any partial state
    }
    scratchW = scratchH = 0;
    dbg_printf("Panel scratch: alloc FAILED (free heap %u) — direct-draw, expect flicker\n",
               (unsigned)ESP.getFreeHeap());
}

// Shared-scratch panel helpers.  Instead of allocating/freeing a sprite on
// every panel every frame (heap churn + fragmentation), or holding a separate
// persistent buffer per panel (too much RAM at 16-bit), these draw every panel
// into ONE 16-bit scratch sprite and push only the panel's w×h region via a
// destination clip rect.  It is allocated ONCE at boot at the largest size any
// panel needs and is never grown, shrunk or freed — zero heap churn, ever.  The
// old code grew it on demand, which meant deleteSprite() + createSprite() of
// ~32 KB at runtime; with the ESP-NOW radio up the heap is fragmented enough
// that this intermittently failed, and because the delete happened FIRST a
// failure destroyed the working buffer and forced every panel on every screen
// into the direct-draw path.  That is the "lots of flicker on ESP-NOW"
// symptom, and it is why it appeared on screens far from the one that grew it.
// 16-bit keeps the near-neutral COLOR_DARKER_BG panels true gray (8-bit rgb332
// crushed them green).  Never freed — see releasePanelSprites().
//
// beginPanelSprite() returns the graphics target and sets ox/oy to the draw
// origin: (0,0) into the scratch, or the on-screen panel origin (px,py) when the
// scratch can't allocate (direct-draw fallback — never blank).  Pair every call
// with endPanelSprite(), passing the SAME w/h/px/py.
LovyanGFX* beginPanelSprite(int w, int h, int& ox, int& oy, int px, int py) {
    // Too big for the scratch, or the boot allocation failed: draw straight to
    // the display.  Deliberately NO runtime reallocation — a panel that does not
    // fit is a compile-time mistake (raise PANEL_SCRATCH_H), and retrying a
    // 32 KB alloc on every frame would thrash the heap for the whole UI.
    if (!spritePanelScratch.getBuffer() || w > scratchW || h > scratchH) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            dbg_printf("Panel %dx%d exceeds scratch %dx%d (free heap %u) — "
                       "direct-draw fallback, expect flicker\n",
                       w, h, scratchW, scratchH, (unsigned)ESP.getFreeHeap());
        }
        ox = px; oy = py;
        scratchInUse = false;
        return (LovyanGFX*)&display;
    }
    ox = 0; oy = 0;
    scratchInUse = true;
    return (LovyanGFX*)&spritePanelScratch;
}

void endPanelSprite(int w, int h, int px, int py) {
    if (scratchInUse && spritePanelScratch.getBuffer()) {
        // Clip the destination so a larger scratch writes only the panel region.
        display.setClipRect(px, py, w, h);
        spritePanelScratch.pushSprite(px, py);
        display.clearClipRect();
    }
    scratchInUse = false;
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
    // Battery only exists on the wireless (mobile) pendant; a wired unit is
    // powered from the UART cable and has no cell to report.
    if (comms_active_mode() == COMMS_MODE_UART) return;
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
static void drawLinkIcon() {
    // Wired pendants have no radio — nothing to show.
    if (comms_active_mode() == COMMS_MODE_UART) return;

    static LGFX_Sprite spr(&display);
    if (!spr.getBuffer()) {
        spr.createSprite(22, 13);
        if (!spr.getBuffer()) return;
    }
    spr.fillSprite(COLOR_DARKER_BG);

    int bars = pendantMachine.linkSignalBars;
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
    spr.pushSprite(5, 11);
}

void drawTitle(String title) {
    display.fillRect(0, 0, 240, 35, COLOR_DARKER_BG);
    display.setTextColor(COLOR_TITLE);
    display.setTextSize(2);
    int16_t tw = display.textWidth(title.c_str());
    display.setCursor((240 - tw) / 2, 10);
    display.print(title);
    drawLinkIcon();     // overlay icon at top-left;  no-op on a wired pendant
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
        case PSCREEN_CONNECTION:       exitConnection();       break;
        case PSCREEN_ESPNOW_PAIR:      exitEspNowPair();      break;
        case PSCREEN_ESPNOW_MACHINES:  exitEspNowMachines();  break;
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
        case PSCREEN_CONNECTION:       enterConnection();       break;
        case PSCREEN_ESPNOW_PAIR:      enterEspNowPair();      break;
        case PSCREEN_ESPNOW_MACHINES:  enterEspNowMachines();  break;
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
        case PSCREEN_CONNECTION:       drawConnectionScreen();       break;
        case PSCREEN_ESPNOW_PAIR:      drawEspNowPairScreen();      break;
        case PSCREEN_ESPNOW_MACHINES:  drawEspNowMachinesScreen();  break;
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
        case PSCREEN_CONNECTION:       handleConnectionTouch(x, y);       break;
        case PSCREEN_ESPNOW_PAIR:      handleEspNowPairTouch(x, y);      break;
        case PSCREEN_ESPNOW_MACHINES:  handleEspNowMachinesTouch(x, y);  break;
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
        if (pendantJog.incDialMode) {
            // Coarse dial box — step through 10/50/100 mm (.5/2.0/4.0 in).  Clamped
            // rather than wrapping, like every other dial-driven field in the UI.
            int ci = constrain(pendantJog.coarseIdx + (delta > 0 ? 1 : -1), 0, JOG_COARSE_COUNT - 1);
            if (ci != pendantJog.coarseIdx) {
                pendantJog.coarseIdx = ci;
                jogApplyCoarseIncrement();   // slot 3 is selected, so refresh the live increment
                saveJogPrefs();
                redrawJogIncrementButtons();
                updateJogAxisDisplay();
            }
            return;
        }
        if (pendantJog.selectedAxis < 0) return;  // no axis selected — do nothing

        // Continuous-jog cadence — record EVERY detent here, BEFORE the flow-control
        // drop below.  A fast fine-increment spin generates far more jog sends than
        // FluidNC's planner can take, so many get dropped; if we timed only the
        // survivors the gaps between them would look "slow", the spin would never be
        // recognised as continuous, and the dial-stop JogCancel would never arm —
        // exactly why 1 mm and finer jogs kept coasting after the dial stopped.
        unsigned long now = millis();
        unsigned long interval = now - jogLastTickMs;
        jogLastTickMs = now;
        if (interval < JOG_CONTINUOUS_MS) {
            // Track the average gap so jogStopTimeoutMs() can scale the dial-stop
            // window to how fast the wheel is actually turning.  Clamped to >=1 ms
            // because 0 is the "not measured yet" sentinel.  1/4-weight EMA smooths
            // detent jitter without lagging a real speed change by more than a few
            // ticks.
            if (interval < 1) interval = 1;
            jogTickEmaMs = (jogTickEmaMs == 0) ? interval
                                               : (jogTickEmaMs * 3 + interval) / 4;
            if (++jogRapidCount >= 2) jogContinuous = true;
        } else {
            jogRapidCount = 1;
            jogContinuous = false;
            jogTickEmaMs  = 0;   // next spin re-measures from scratch
        }

        // Jog flow control: if FluidNC's motion planner is full (many sends in
        // flight without acks), skip SENDING this jog — the tick is already timed
        // above so the dial-stop watchdog stays accurate.  Last line of defence
        // against overflowing FluidNC's RX buffer / corrupting the command stream.
        if (pending_nowait_sends >= 6) return;

        // Send $J immediately per tick (like cyd_buttons) so FluidNC's planner buffer
        // stays populated and the deceleration ramp bridges the gap between ticks.
        // Time-based velocity scaling: fast turns send a proportionally larger distance.
        {
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
                    // has settled to Idle — always on first use, and right after a
                    // continuous-jog JogCancel flushed the queue (jogForceReseed),
                    // since predMm then holds distance that will never execute.
                    bool newBurst = (interval > 400) || (axis != lastAxis);
                    if (isnan(predMm[axis]) || jogForceReseed ||
                        (newBurst && pendantMachine.status.startsWith("Idle"))) {
                        predMm[axis] = mposMm;
                    }
                    jogForceReseed = false;
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

        // Dial inert while the confirm overlay is up.  Two reasons: the fields-only
        // redraw below would paint over the dialog (it doesn't repaint the overlay
        // the way a full screen draw did), and — more importantly — the values being
        // confirmed must not move underneath the prompt, or you'd confirm a probe
        // against a number you never saw.  Mirrors the calState guard below.
        if (p.confirmActive) return;

        if (currentPendantScreen == PSCREEN_PROBE) {
            // SCR0 shared fields: 0=probeRate 1=seekRate 2=retractDist 3=maxZTravel
            float step = probeDialStep(delta, (fo <= 1) ? 10.0f : 0.1f);
            if (fo == 0) p.probeRate   = constrain(p.probeRate   + delta * step, 10.0f, 3000.0f);
            if (fo == 1) p.seekRate    = constrain(p.seekRate    + delta * step, 10.0f, 3000.0f);
            if (fo == 2) p.retractDist = constrain(p.retractDist + delta * step,  0.1f,   50.0f);
            if (fo == 3) p.maxZTravel  = constrain(p.maxZTravel  + delta * step,  1.0f,  200.0f);
            updateProbeSharedFields();   // redraw only the field — no full-screen flash

        } else if (currentPendantScreen == PSCREEN_PROBE_CFG_3D) {
            if (p.calState != 0) { return; }   // dial inert while the cal overlay is up
            // 0=ballDia 1=deflection 2=calGaugeWidth
            float step = probeDialStep(delta, (fo == 1) ? 0.001f : 0.1f);
            if (fo == 0) p.ballDia       = constrain(p.ballDia       + delta * step,  0.1f,  20.0f);
            if (fo == 1) p.deflection    = constrain(p.deflection    + delta * step, -1.0f,   1.0f);  // signed
            if (fo == 2) p.calGaugeWidth = constrain(p.calGaugeWidth + delta * step,  1.0f, 300.0f);
            updateProbeCfg3DFields();

        } else if (currentPendantScreen == PSCREEN_PROBE_CFG_PLATE) {
            // 0=plateThick 1=plateWidth 2=plateOffX 3=plateOffY
            float step = probeDialStep(delta, 0.1f);
            if (fo == 0) p.plateThick = constrain(p.plateThick + delta * step,  0.1f,  50.0f);
            if (fo == 1) p.plateWidth = constrain(p.plateWidth + delta * step,  1.0f, 200.0f);
            if (fo == 2) p.plateOffX  = constrain(p.plateOffX  + delta * step,-50.0f,  50.0f);
            if (fo == 3) p.plateOffY  = constrain(p.plateOffY  + delta * step,-50.0f,  50.0f);
            updateProbeCfgPlateFields();

        } else if (currentPendantScreen == PSCREEN_PROBE_Z) {
            // fo==0 → maxZTravel, fo==1 → retractDist
            // Whole-mm steps (or imperial equiv); accelerates to 10mm after rapid turns
            float base = pendantMachine.inInches ? (1.0f / 25.4f) : 1.0f;
            float step = probeDialStep(delta, base);
            if (fo == 0) p.maxZTravel  = constrain(p.maxZTravel  + delta * step,  1.0f, 200.0f);
            if (fo == 1) p.retractDist = constrain(p.retractDist + delta * step,  1.0f,  50.0f);
            updateProbeZFields();

        } else if (currentPendantScreen == PSCREEN_PROBE_CORNER) {
            // 0=cornerDepth 1=cornerOver 2=cornerRetXY
            float step = probeDialStep(delta, 0.1f);
            if (fo == 0) p.cornerDepth = constrain(p.cornerDepth + delta * step, 0.1f, 50.0f);
            if (fo == 1) p.cornerOver  = constrain(p.cornerOver  + delta * step, 0.1f, 20.0f);
            if (fo == 2) p.cornerRetXY = constrain(p.cornerRetXY + delta * step, 0.1f, 20.0f);
            updateProbeCornerFields();

        } else if (currentPendantScreen == PSCREEN_PROBE_BORE) {
            // 0=boreDia 1=boreOffset
            float step = probeDialStep(delta, 0.1f);
            if (fo == 0) p.boreDia    = constrain(p.boreDia    + delta * step, 0.1f, 500.0f);
            if (fo == 1) p.boreOffset = constrain(p.boreOffset + delta * step, 0.1f,  50.0f);
            updateProbeBoreFields();

        } else if (currentPendantScreen == PSCREEN_PROBE_BOSS) {
            float step = probeDialStep(delta, 0.1f);
            if (p.bossRect) {
                // 0=X size (bossDia) 1=Y size (bossSizeY) 2=bossDepth 3=bossClear
                if (fo == 0) p.bossDia   = constrain(p.bossDia   + delta * step, 0.1f, 500.0f);
                if (fo == 1) p.bossSizeY = constrain(p.bossSizeY + delta * step, 0.1f, 500.0f);
                if (fo == 2) p.bossDepth = constrain(p.bossDepth + delta * step, 0.1f, 100.0f);
                if (fo == 3) p.bossClear = constrain(p.bossClear + delta * step, 0.1f,  50.0f);
            } else {
                // 0=bossDia 1=bossDepth 2=bossClear
                if (fo == 0) p.bossDia   = constrain(p.bossDia   + delta * step, 0.1f, 500.0f);
                if (fo == 1) p.bossDepth = constrain(p.bossDepth + delta * step, 0.1f, 100.0f);
                if (fo == 2) p.bossClear = constrain(p.bossClear + delta * step, 0.1f,  50.0f);
            }
            updateProbeBossFields();
        }
        return;

    } else if (currentPendantScreen == PSCREEN_FEEDS_SPEEDS) {
        if (!pendantConnected) return;
        if (pendantFeeds.dialMode == 1) {
            // Feed override — 10% per detent, applied by the paced stepper.
            int base = (feedOvr.target >= 0) ? feedOvr.target : pendantMachine.feedOverride;
            overrideSetFeedTarget(base + delta * 10);
            updateFeedOverrideDisplay();
        } else if (pendantFeeds.dialMode == 2) {
            // Spindle override — 10% per detent, applied by the paced stepper.
            int base = (spindleOvr.target >= 0) ? spindleOvr.target : pendantMachine.spindleOverride;
            overrideSetSpindleTarget(base + delta * 10);
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
    preferences.putInt("jogCoarseIdx", pendantJog.coarseIdx);
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
        case PSCREEN_PROBE_CFG_3D:     updateProbeCfg3DScreen();  break;
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
        case PSCREEN_ESPNOW_PAIR:
            updateEspNowPairDisplay();
            break;
        case PSCREEN_ESPNOW_MACHINES:
            updateEspNowMachinesDisplay();
            break;
        case PSCREEN_CONNECTION:
            updateConnectionDisplay();
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
    drawLinkIcon();
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
// Settings queries are sent with ConfigItem::request(), which registers the item
// for its reply and uses the NON-BLOCKING send.
//
// The library's fnc_send_line() spins on Core 1 waiting for the *previous*
// command's ack at the START of every call, up to a 2 s timeout per call.
// Calling it eight times in a row can block Core 1 for far longer than the 5 s
// loop-task watchdog allows.  These read-only queries need no synchronous ack —
// GrblParser parses the replies asynchronously and parse_dollar() feeds them
// back into the ConfigItems.

static void requestControllerConfig() {
    extern uint32_t rtcCore1Stage;   // defined in ardmain.cpp
    rtcCore1Stage = 200;       // requestControllerConfig start
    // These go through ConfigItem::request() rather than sendQueryRaw().
    //
    // sendQueryRaw() only put the query on the wire — it never registered the
    // item in configRequests, and parse_dollar() matches replies ONLY against
    // that list.  So the burst asked all eight questions and nothing could
    // consume the answers: $110/$130-$133/$23 were permanently !known(), and
    // their consumers silently kept the defaults (maxTravel stuck at {0,0,0,0},
    // maxFeedRate at 10000).  Registering also makes the retry below possible,
    // since an unanswered query is then simply one still in the list.
    spindleMaxItem.request();  // spindle max RPM ($30)
    spindleMinItem.request();  // spindle min RPM ($31)
    jogMaxRateItem.request();  // jog max feed rate ($110)
    jogMaxTravelX.request();   // X travel ($130)
    jogMaxTravelY.request();   // Y travel ($131)
    jogMaxTravelZ.request();   // Z travel ($132)
    jogMaxTravelA.request();   // A travel ($133)
    jogHomingDirMask.request();// homing direction mask ($23)
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


    // Both transports are byte streams, so there is a single path: the
    // $File/SendJSON chain (preferences.json → legacy macrocfg.json).
    request_macros();
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
        // Macros fetch finished with nothing to show.  Both transports use the
        // $File/SendJSON chain, and a chain that produced no usable reply is a
        // load failure → "Couldn't load — tap Refresh".
        if (currentPendantScreen == PSCREEN_MACROS) {
            pendantMacros.loading    = false;
            pendantMacros.count      = 0;
            pendantMacros.loadFailed = true;
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

    // Pick comms backend (UART or ESP-NOW) and initialise only that one.
    // espnow_init() spins up the radio when ESP-NOW is selected; in UART mode
    // it is never called and the WiFi radio stays cold.  After this call
    // the hot path is a single indirect function call per byte.
    comms_init();
    dbg_printf("Comms: active transport = %s (free heap %u)\n",
               comms_mode_name(), (unsigned)ESP.getFreeHeap());
    rtcLastBootStage = 5;     // stage 5: comms_init done

    // Panel scratch AFTER the radio, so the radio's allocations always win —
    // see initPanelScratch() for why this ordering is deliberate.
    initPanelScratch();

    // NOTE: no first-run screen routing here.  An earlier version forced
    // currentPendantScreen from this task when ESP-NOW had no stored pairing,
    // but Core 1 had already drawn the main menu — so the screen STATE and the
    // drawn CONTENT disagreed and the Connection panel painted itself over the
    // menu buttons.  The main menu's STATUS panel now reports the link state
    // directly ("Not paired" / "No link"), so there is nothing to route.

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
            // Bound a single line to just under GrblParser's REPORT_BUFFER_LEN
            // (1024) so collect() can never overrun its static buffer.
            static const int GRBL_LINE_MAX = 1000;
            static int       grblLineLen   = 0;
#ifdef USE_ESPNOW
            // Per-line routing state for the interleaved-reply demux below.
            static bool jsonLineStart   = true;
            static bool jsonLineIsReply = true;
#endif
            int c;
            int budget = 8192;
            while (budget-- > 0 && (c = fnc_getchar()) >= 0) {
#ifdef USE_ESPNOW
                // ESP-NOW (like the old WebSocket) receives $File/SendJSON and
                // $Files/ListGCode replies RAW — no "[JSON:...]" wrapper and no
                // newlines until the document ends.  GrblParser's collect()
                // buffers a line into a 1024-byte static with no bounds check,
                // so a ~10 KB reply overruns it by kilobytes and corrupts .bss.
                // While such a reply is in flight, stream the bytes straight to
                // the JSON parser, which needs no line framing.
                //
                // UART is deliberately untouched: FluidNC's UartChannel wraps
                // the reply and emits it in short lines, so collect() handles it
                // correctly and that path keeps working exactly as before.
                //
                // ROUTE BY LINE, NOT BLINDLY.  FluidNC sets a 200 ms auto-report
                // interval on an ESP-NOW channel the moment a peer pairs
                // (ESPNowChannel::refreshReportInterval ->
                // DEFAULT_REPORT_INTERVAL_MS = 200), so ~5 status reports a
                // second are INTERLEAVED into the very same byte stream as the
                // reply.  Streaming every byte to the JSON parser fed those
                // "<Idle|MPos:...>" lines into the document — which both killed
                // the parse ("Couldn't load" every time) and stopped the reports
                // ever reaching collect(), freezing the DRO and machine state.
                //
                // The reply is still newline-framed, so decide per line from its
                // first byte: '<' is a status report and '[' is a [MSG:...] line,
                // both of which belong to the normal parser; anything else is
                // reply payload.  The decision is made on the first character and
                // each byte is dispatched as it arrives, so nothing is buffered
                // and the 1024-byte overrun stays fixed.
                if (g_expecting_json && comms_active_mode() == COMMS_MODE_ESPNOW) {
                    if (jsonLineStart) {
                        jsonLineStart   = false;
                        jsonLineIsReply = (c != '<' && c != '[');
                    }
                    if (c == '\n') jsonLineStart = true;
                    if (jsonLineIsReply) {
                        json_stream_byte((char)c);
                        rxEverSeen = true;
                        continue;
                    }
                    // Controller traffic: fall through to the line parser below.
                } else {
                    jsonLineStart   = true;
                    jsonLineIsReply = true;
                }
#endif
                // HARD GUARD on GrblParser's line buffer.
                //
                // collect() appends into a REPORT_BUFFER_LEN (1024) static with
                // NO bounds check and only flushes on '\n'.  Anything that
                // delivers a longer line — a raw JSON reply over ESP-NOW is ~4-10
                // KB with no newlines — walks off the end of that buffer and
                // corrupts adjacent .bss.  Observed effect: status-report parsing
                // died permanently after the first oversized reply (the report
                // counter froze at 2), which is what left the pendant on
                // "Syncing" and the DRO stale.
                //
                // Truncating an over-long line loses that reply, but keeps the
                // parser and everything after it intact.
                if (c == '\n') {
                    grblLineLen = 0;
                    collect((uint8_t)c);
                } else if (grblLineLen < GRBL_LINE_MAX) {
                    ++grblLineLen;
                    collect((uint8_t)c);
                }
                // else: drop the remainder of this line — never feed collect()
                rxEverSeen = true;  // real controller data observed
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

        // Continuous-jog dial-stop watchdog: if the wheel was being spun and has
        // now been still for JOG_STOP_MS, cancel the jog so motion halts at once
        // (flushes the queued G91 moves) instead of coasting.  Harmless if no jog
        // is active — FluidNC ignores JogCancel when not jogging.
        if (jogContinuous && pendantConnected && (nowMs - jogLastTickMs > jogStopTimeoutMs())) {
            fnc_realtime(JogCancel);
            jogContinuous  = false;
            // Seed at 1, not 0, so resuming a spin re-arms on the same detent count
            // as starting one.  The tick handler's "gap too long" branch already
            // seeds 1, so a fresh spin arms on its 2nd detent; resetting to 0 here
            // made a resumed spin need 3, and a stop during that window coasted.
            jogRapidCount  = 1;
            jogTickEmaMs   = 0;
            jogForceReseed = true;   // predMm holds flushed distance — resync next tick
        }

        // Paced feed/spindle override ramp — one coarse/fine byte per OVR_STEP_MS.
        if (pendantConnected) {
            ovrStep(feedOvr,    nowMs, FeedOvrCoarsePlus,    FeedOvrCoarseMinus,    FeedOvrFinePlus,    FeedOvrFineMinus);
            ovrStep(spindleOvr, nowMs, SpindleOvrCoarsePlus, SpindleOvrCoarseMinus, SpindleOvrFinePlus, SpindleOvrFineMinus);
        }

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

#ifdef USE_ESPNOW
        // ── Startup retry ────────────────────────────────────────────────────
        // ESP-NOW loses data on the boot burst — RX loss is observed at close
        // range with full signal, so packets are being dropped after the radio
        // delivers them (ring/queue overflow), not lost over the air.  The
        // startup exchange was fire-and-forget: one status poll every 4 s and a
        // single one-shot config burst, with nothing checking either arrived.
        // Lose the reply and the pendant waits indefinitely — the "stuck on
        // Syncing until you home" fault, where homing merely happened to be the
        // next thing that provoked a reply.
        //
        // So verify the answers actually arrived and re-ask if not.  Both checks
        // stop as soon as their data is in, so steady-state traffic is unchanged.
        if (comms_active_mode() == COMMS_MODE_ESPNOW && pendantConnected) {
            // 1. Status: re-poll until a report is parsed (which sets
            //    pendantSynced).  Far faster than the 4 s ping while unsynced.
            extern void request_status_report();   // FluidNCModel.cpp
            static unsigned long lastSyncPollMs = 0;
            if (!pendantSynced && (nowMs - lastSyncPollMs) >= 500) {
                lastSyncPollMs = nowMs;
                request_status_report();
            }

            // 2. Settings queries: parse_dollar() erases each ConfigItem from
            //    configRequests the moment its reply lands, so a non-empty list
            //    IS the set of questions still unanswered — an exact
            //    completeness check, no per-item bookkeeping.  This covers the
            //    connect-edge burst ($30/$31/$110/$130-$133/$23) and the homing
            //    cycle/allow items together.  Re-send only what is outstanding.
            //
            //    Safe to iterate: replies are parsed on this same task, and
            //    send_line_nowait() does not pump the parser, so the vector
            //    cannot change underneath the loop.
            static unsigned long lastItemRetryMs = 0;
            static int           itemRetries     = 0;
            if (!configRequests.empty()) {
                if (itemRetries < 4 && (nowMs - lastItemRetryMs) >= 1500) {
                    lastItemRetryMs = nowMs;
                    ++itemRetries;
                    for (auto* item : configRequests) send_line_nowait(item->name());
                }
            } else {
                itemRetries = 0;       // all answered — arm for the next reconnect
            }

            // 3. $A: the alarm code behind an Alarm state.  awaiting_alarm is
            //    already the completeness flag — it clears when the reply is
            //    parsed — so a lost reply just needs the question re-asked.
            static unsigned long lastAlarmRetryMs = 0;
            static int           alarmRetries     = 0;
            if (awaiting_alarm) {
                if (alarmRetries < 4 && (nowMs - lastAlarmRetryMs) >= 1500) {
                    lastAlarmRetryMs = nowMs;
                    ++alarmRetries;
                    send_line_nowait("$A");
                }
            } else {
                alarmRetries = 0;      // answered (or no alarm) — arm for next time
            }

            // 4. $I: not a ConfigItem, so it needs its own check.  Its reply
            //    carries the firmware version AND the Mode/IP/SSID line; getting
            //    the version without the network line means the reply arrived
            //    only in part.  Bounded, because a controller genuinely off the
            //    WLAN reports "No Wifi" and will never fill those fields.
            static unsigned long lastCfgRetryMs = 0;
            static int           cfgRetries     = 0;
            const bool           cfgIncomplete  = wifi_ip.empty() || wifi_ssid.empty();
            if (cfgIncomplete && cfgRetries < 4 && (nowMs - lastCfgRetryMs) >= 2000) {
                lastCfgRetryMs = nowMs;
                ++cfgRetries;
                send_line_nowait("$I");
            } else if (!cfgIncomplete) {
                cfgRetries = 0;        // arm again for the next reconnect
            }
        }

        // Re-fetch the controller config when the RADIO link genuinely comes up.
        //
        // The connect edge above is driven by GrblParser's receive timing, which
        // keepalives satisfy while PeerLink is still Synchronizing — but
        // send_fragments() silently discards everything until the link reaches
        // Connected.  So the one-shot $G/$I/$A burst could be issued into that
        // window and vanish entirely: no version, no IP/SSID, no status, and no
        // retry, until some later command (homing) happened to be sent after the
        // link was up and everything arrived at once.  It is a race, which is why
        // the fault came and went.  Re-issuing on the real link-up edge closes it.
        if (comms_active_mode() == COMMS_MODE_ESPNOW) {
            static bool prevLinkUp = false;
            const bool  linkUp     = espnow_is_connected();
            if (linkUp && !prevLinkUp && hwEventQueue) {
                HwEvent ev = { HwEvent::CONNECTED, 0 };
                xQueueSend(hwEventQueue, &ev, 0);
            }
            prevLinkUp = linkUp;
        }
#endif
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
                #ifdef USE_ESPNOW
                // Close the radio link cleanly before sleeping so the
                // controller sees a departure rather than a silent timeout.
                // Does NOT unpair — the profile stays stored for next boot.
                if (comms_active_mode() == COMMS_MODE_ESPNOW) {
                    espnow_graceful_disconnect();
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
        // NOTE: v2.1.x polled '?' every 250 ms in WiFi mode because the
        // WebSocket transport ignored $Report/Interval.  UART and ESP-NOW both
        // honour it, so the controller pushes status on its own schedule and no
        // polling is needed — one less thing on the radio.

        // Safety net for a JSON reply that never completes.  g_expecting_json
        // routes incoming bytes to the JSON parser instead of the line parser,
        // so if a transfer is abandoned mid-document (controller reset, link
        // drop) the flag would latch and the pendant would go deaf to status
        // reports — appearing connected but frozen.  Clear it after a bounded
        // wait; the requesting screen already reports its own load failure.
        {
            static bool          jsonWasExpecting = false;
            static unsigned long jsonExpectSince  = 0;
            if (g_expecting_json && !jsonWasExpecting) jsonExpectSince = nowMs;
            jsonWasExpecting = g_expecting_json;
            if (g_expecting_json && (nowMs - jsonExpectSince) > 20000UL) {
                dbg_println("JSON reply never completed — releasing the parser");
                g_expecting_json    = false;
                g_json_accumulating = false;
                jsonWasExpecting    = false;
            }
        }

        // Drain the pending-nowait counter if no acks have arrived recently.
        // Self-heals from the rare case where an atomic TCP send fails before
        // delivering a queued command (so the expected ok will never come)
        // and would otherwise leave the jog throttle stuck high.
        nowait_pending_decay();

        // Wireless link-quality cache — sampled on Core 0 (the task that owns
        // the radio) and published to pendantMachine so Core 1's UI reads a
        // plain int instead of touching the radio API across cores.
        #ifdef USE_ESPNOW
        static unsigned long lastLinkSampleMs = 0;
        if (comms_active_mode() == COMMS_MODE_ESPNOW &&
            (millis() - lastLinkSampleMs) >= 500) {
            pendantMachine.linkSignalBars = espnow_signal_bars();
            lastLinkSampleMs = millis();
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
    int  savedCoarse   = preferences.getInt("jogCoarseIdx", 0);
    int  savedSelInc   = preferences.getInt("jogSelInc",    1);
    preferences.end();

    // Clamp saved indices to valid slots — guards against a corrupted NVS entry,
    // and against the pre-v2.1.10 layout where slot 3 was a fixed value and this
    // key did not exist (getInt then returns the 0 default, i.e. the 10 mm entry).
    pendantJog.coarseIdx         = constrain(savedCoarse, 0, JOG_COARSE_COUNT - 1);
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

    // ── Screen sleep management (wireless pendants only) ──────────────────────
    // Only battery pendants sleep — a wired unit is powered from the controller
    // and has no cell to conserve, so it powers down with it and there's nothing
    // to blank.  Eligible to blank when the CNC is Idle OR while the pendant is
    // still "Connecting" (not connected) — both are no-activity states.  Any
    // connected-but-busy state (Run/Jog/Hold/Home/Alarm/…) keeps it awake and
    // resets the idle clock, so it never blanks mid-job.
    if (comms_active_mode() != COMMS_MODE_UART) {
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
        } else if (currentPendantScreen != PSCREEN_CONNECTION
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
