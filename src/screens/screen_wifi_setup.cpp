#include "pendant_shared.h"
#include "screen_wifi_setup.h"
#include "../Comms.h"             // comms_active_mode(), transport_force_*()

#ifdef USE_WIFI
#include "../WiFiConnection.h"    // status / signal / AP-config helpers (WiFi-only)
#endif

#include <Esp.h>
#include <Preferences.h>

// ── Layout (all direct display.* calls — no sprites, no heap dependency) ──────
//
// Transport selection is MANUAL:
//   • NVS key "tport_force" stores UART (default) or WiFi.
//   • The mode banner at the top is tappable — tapping toggles the selection
//     and restarts so the next boot picks up the new transport.
//
// There is no hardware autodetect — the user makes the choice once and it
// sticks across reboots and firmware updates (NVS is preserved by the
// Update path of the web installer).
//
//  y=  0–35   title bar       (drawTitle)
//  y= 40–98   mode banner     (live transport + override; tappable)
//  y=106–221  status panel    (WiFi status, or UART explanation)
//  y=228–264  action button   (Reconfigure WiFi — WiFi mode only)
//  y=272–312  back button

static constexpr int PNL_MODE_X   = 5;
static constexpr int PNL_MODE_Y   = 40;
static constexpr int PNL_MODE_W   = 230;
static constexpr int PNL_MODE_H   = 58;

static constexpr int PNL_STAT_X   = 5;
static constexpr int PNL_STAT_Y   = 106;
static constexpr int PNL_STAT_W   = 230;
static constexpr int PNL_STAT_H   = 116;

static constexpr int BTN_ACT_X    = 5;
static constexpr int BTN_ACT_Y    = 230;
static constexpr int BTN_ACT_W    = 230;
static constexpr int BTN_ACT_H    = 36;

static constexpr int BTN_BACK_X   = 5;
static constexpr int BTN_BACK_Y   = 272;
static constexpr int BTN_BACK_W   = 230;
static constexpr int BTN_BACK_H   = 40;

// ── Mode banner ───────────────────────────────────────────────────────────────
// Shows the LIVE comms transport (whatever comms_init picked this boot) plus
// the persistent override setting underneath.  The whole panel is a tap target
// that cycles the override.  Drawn once per screen entry — the values are
// fixed for the boot.
static void drawModeBanner(bool uartMode) {
    display.fillRoundRect(PNL_MODE_X, PNL_MODE_Y, PNL_MODE_W, PNL_MODE_H,
                          5, COLOR_DARKER_BG);
    display.drawRoundRect(PNL_MODE_X, PNL_MODE_Y, PNL_MODE_W, PNL_MODE_H,
                          5, COLOR_CYAN);                          // tappable hint

    // Header row: live transport + small "TAP TO CHANGE" cue
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(PNL_MODE_X + 5, PNL_MODE_Y + 5);
    display.print("TRANSPORT");
    const char* cue = "TAP";
    display.setTextColor(COLOR_CYAN);
    display.setCursor(PNL_MODE_X + PNL_MODE_W - 5 - display.textWidth(cue),
                      PNL_MODE_Y + 5);
    display.print(cue);

    // Big live mode label
    display.setTextColor(uartMode ? COLOR_ORANGE : COLOR_GREEN);
    display.setTextSize(2);
    display.setCursor(PNL_MODE_X + 5, PNL_MODE_Y + 18);
    display.print(uartMode ? "UART cable" : "WiFi");

    // Subtitle: hint that the banner is tappable to switch transport
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(PNL_MODE_X + 5, PNL_MODE_Y + 44);
    display.print("Tap to switch transport");
}

// ── Status panel ──────────────────────────────────────────────────────────────
// Redraws only the status rectangle — called every 100 ms so WiFi state
// (signal bars, connection status, AP/STA mode) stays current.
// Track minimum free heap ever observed since boot — if this number drops
// over time, there's a memory leak; if it's stable, freezes aren't heap.
static uint32_t _minHeapEverSeen = 0xFFFFFFFFu;
static void sampleMinHeap() {
    uint32_t now = ESP.getFreeHeap();
    if (now < _minHeapEverSeen) _minHeapEverSeen = now;
}

static void redrawStatusPanel() {
    sampleMinHeap();  // cheap; safe to call every 100ms tick

    display.fillRoundRect(PNL_STAT_X, PNL_STAT_Y, PNL_STAT_W, PNL_STAT_H,
                          5, COLOR_DARKER_BG);

#ifdef USE_WIFI
    bool uartMode = (comms_active_mode() == COMMS_MODE_UART);

    if (uartMode) {
        // ── UART mode summary ──────────────────────────────────────────────
        display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
        display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 8);
        display.print("Talking to FluidNC via");
        display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 21);
        display.print("the UART (RJ12) cable.");

        display.setTextColor(COLOR_GRAY_TEXT);
        display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 45);
        display.print("Baud:");
        display.setTextColor(COLOR_ORANGE);
        display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 58);
        display.print(pendantMachine.baudRate);

        display.setTextColor(COLOR_GRAY_TEXT);
        display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 45);
        display.print("Port:");
        display.setTextColor(COLOR_CYAN);
        display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 58);
        display.print(pendantMachine.port);

        display.setTextColor(COLOR_GRAY_TEXT);
        display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 82);
        display.print("Tap the banner above to");
        display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 95);
        display.print("change transport.  Last:");
        {
            bool healthy = (lastResetReason == ESP_RST_POWERON ||
                            lastResetReason == ESP_RST_SW);
            display.setTextColor(healthy ? COLOR_GRAY_TEXT : COLOR_ORANGE);
            char buf[24];
            snprintf(buf, sizeof(buf), " %s/%u:%u",
                     resetReasonName(lastResetReason),
                     (unsigned)capturedBootStage,
                     (unsigned)capturedCore1Stage);
            display.print(buf);
        }

    } else {
        // ── WiFi mode status ───────────────────────────────────────────────
        // Read cached AP-mode / signal-bars from pendantMachine (sampled on
        // Core 0).  The wifi_*() string / config getters are still called
        // directly — they only read static C-string pointers, which is safe.
        bool        apMode  = pendantMachine.wifiInApMode;
        const char* status  = wifi_status_str();
        const char* errMsg  = wifi_last_error();
        WiFiConfig  cfg     = wifi_active_config();
        int         bars    = pendantMachine.wifiSignalBars;
        if (bars < 0) bars = 0;

        bool connected = websocket_is_connected();
        display.setTextSize(1);
        display.setTextColor(COLOR_GRAY_TEXT);
        display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 6);
        display.print("STATUS");
        display.setTextColor(connected ? COLOR_GREEN : COLOR_ORANGE);
        display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 18);
        display.print(status);

        if (errMsg && *errMsg) {
            display.setTextColor(COLOR_RED);
            display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 31);
            display.print(errMsg);
        }

        if (apMode) {
            display.setTextColor(COLOR_GRAY_TEXT);
            display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 50);
            display.print("Connect phone to WiFi:");
            display.setTextColor(COLOR_CYAN);
            display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 63);
            display.print(wifi_ap_ssid());

            display.setTextColor(COLOR_GRAY_TEXT);
            display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 82);
            display.print("Then open browser:");
            display.setTextColor(COLOR_CYAN);
            display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 95);
            display.print("192.168.4.1");

        } else {
            // STA: SSID + signal / FluidNC IP
            String barStr;
            for (int i = 0; i < 4; i++) barStr += (i < bars) ? "|" : ".";

            display.setTextSize(1);
            display.setTextColor(COLOR_GRAY_TEXT);
            display.setCursor(PNL_STAT_X + 5,   PNL_STAT_Y + 50);
            display.print("NETWORK");
            display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 50);
            display.print("SIGNAL");

            display.setTextColor(COLOR_CYAN);
            display.setCursor(PNL_STAT_X + 5,   PNL_STAT_Y + 63);
            display.print(cfg.valid ? cfg.ssid : "---");
            display.setTextColor(bars > 0 ? COLOR_GREEN : COLOR_GRAY_TEXT);
            display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 63);
            display.print(barStr.c_str());

            display.setTextColor(COLOR_GRAY_TEXT);
            display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 85);
            display.print("FluidNC IP");
            display.setTextColor(COLOR_CYAN);
            display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 98);
            display.print(cfg.valid ? cfg.fluidnc_ip : "---");
        }

        // Last reset reason + stages reached on previous boot.  Format is
        // RESET/CORE0:CORE1 — so we can tell whether the previous crash was
        // caused by something on Core 0 (pendant_hw_task) or Core 1 (the
        // Arduino loop task running screens + connect-time config fetches).
        bool healthy = (lastResetReason == ESP_RST_POWERON ||
                        lastResetReason == ESP_RST_SW);
        display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
        display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 85);
        display.print("Reset c0/c1");
        display.setTextColor(healthy ? COLOR_GRAY_TEXT : COLOR_ORANGE);
        display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 98);
        char buf[32];
        unsigned i0 = capturedCore0Iters > 999 ? 999 : capturedCore0Iters;
        unsigned i1 = capturedCore1Iters > 999 ? 999 : capturedCore1Iters;
        snprintf(buf, sizeof(buf), "%s %u:%u",
                 resetReasonName(lastResetReason),
                 (unsigned)capturedBootStage,
                 (unsigned)capturedCore1Stage);
        display.print(buf);
        display.setCursor(PNL_STAT_X + 118, PNL_STAT_Y + 108);
        char buf2[24];
        snprintf(buf2, sizeof(buf2), "i %u/%u", i0, i1);
        display.print(buf2);
    }

    // Live free-heap monitor — shown at bottom of status panel in WiFi mode.
    // Format: "Heap NNK (min NNK)" current and lowest-ever-this-boot.
    // If 'min' keeps dropping over many seconds while you're not navigating
    // screens, something is leaking memory.
    if (!uartMode) {
        uint32_t nowHeap = ESP.getFreeHeap();
        char hbuf[32];
        snprintf(hbuf, sizeof(hbuf), "Heap %uK (min %uK)",
                 (unsigned)(nowHeap / 1024),
                 (unsigned)(_minHeapEverSeen / 1024));
        display.setTextColor(nowHeap < 30000 ? COLOR_RED
                            : nowHeap < 60000 ? COLOR_ORANGE
                                              : COLOR_GRAY_TEXT);
        display.setTextSize(1);
        display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 108);
        display.print(hbuf);
    }

    // Previous-boot NVS checkpoint — only shown if there's a meaningful
    // value (>0) AND the previous reset wasn't a clean POWER_ON (which
    // doesn't tell us anything new).  Format: "Last NVS: i N/N hp NK".
    // Persists across cold boots, so after a hang+power-cycle this is the
    // only place that retains the last-known-good iteration count + heap.
    if (nvsPrevIter0 > 0 || nvsPrevIter1 > 0) {
        char nbuf[40];
        snprintf(nbuf, sizeof(nbuf), "NVS i %u/%u hp %uK",
                 (unsigned)nvsPrevIter0,
                 (unsigned)nvsPrevIter1,
                 (unsigned)(nvsPrevMinHeap / 1024));
        display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
        display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 118);
        display.print(nbuf);
    }
#else
    display.setTextColor(COLOR_GRAY_TEXT); display.setTextSize(1);
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 20);
    display.print("WiFi not compiled in.");
    display.setCursor(PNL_STAT_X + 5, PNL_STAT_Y + 35);
    display.print("Build with -DUSE_WIFI.");
#endif
}

// ── Screen lifecycle ──────────────────────────────────────────────────────────
void enterWiFiSetup() {
    // Free shared sprites — this screen draws everything directly to the display.
    spriteStatusBar.deleteSprite();
    spriteAxisDisplay.deleteSprite();
    spriteValueDisplay.deleteSprite();
    spriteFileDisplay.deleteSprite();
}

void exitWiFiSetup() {
    // Nothing to clean up — no sprites allocated.
}

// ── Full redraw ───────────────────────────────────────────────────────────────
void drawWiFiSetupScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("WIFI SETUP");

#ifdef USE_WIFI
    bool uartMode = (comms_active_mode() == COMMS_MODE_UART);
    bool apMode   = wifi_in_ap_mode();
#else
    bool uartMode = true;
    bool apMode   = false;
#endif

    drawModeBanner(uartMode);
    redrawStatusPanel();

    // ── Action button (WiFi mode only) ─────────────────────────────────────
#ifdef USE_WIFI
    if (!uartMode) {
        if (apMode) {
            drawButton(BTN_ACT_X, BTN_ACT_Y, BTN_ACT_W, BTN_ACT_H,
                       "Cancel AP Setup", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
        } else {
            drawButton(BTN_ACT_X, BTN_ACT_Y, BTN_ACT_W, BTN_ACT_H,
                       "Reconfigure WiFi", COLOR_ORANGE, COLOR_BACKGROUND, 2);
        }
    }
#endif

    drawButton(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H,
               "< Back", COLOR_BLUE, COLOR_WHITE, 2);
}

// ── Periodic update (100 ms) — refreshes status panel only ───────────────────
void updateWiFiSetupDisplay() {
    if (currentPendantScreen != PSCREEN_WIFI_SETUP) return;
    redrawStatusPanel();
}

// ─── Shared restart splash ────────────────────────────────────────────────────
static void showRestartSplash(const char* line1, const char* line2 = nullptr,
                               const char* line3 = nullptr) {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("WIFI SETUP");
    display.setTextSize(2);
    display.setTextColor(COLOR_WHITE);
    int y = 135;
    auto centre = [&](const char* s, uint16_t col = COLOR_WHITE) {
        display.setTextColor(col);
        display.setCursor((240 - display.textWidth(s)) / 2, y);
        display.print(s);
        y += 26;
    };
    centre(line1);
    if (line2) centre(line2);
    if (line3) centre(line3, COLOR_CYAN);
    display.setTextColor(COLOR_GRAY_TEXT);
    const char* sub = "Restarting...";
    display.setCursor((240 - display.textWidth(sub)) / 2, y + 8);
    display.print(sub);
}

// Toggle the transport selection: UART ↔ WiFi.  Writes NVS and restarts so
// the next boot picks up the new selection via comms_init().
static void cycleTransportOverride() {
    // Flip the CURRENTLY ACTIVE transport (which may have come from the
    // hardware autodetect default, not just a stored override), so the toggle
    // always points the opposite way from what's running right now.
    TransportForce next = (comms_active_mode() == COMMS_MODE_WIFI)
                          ? TFORCE_UART
                          : TFORCE_WIFI;
    set_transport_force(next);
    const char* msg = (next == TFORCE_WIFI) ? "Switching to WiFi"
                                             : "Switching to UART";
    showRestartSplash(msg, "Saved to flash.");
    delay(1500);
    ESP.restart();
}

// ── Touch handler ─────────────────────────────────────────────────────────────
void handleWiFiSetupTouch(int x, int y) {
    // Back
    if (isTouchInBounds(x, y, BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H)) {
        currentPendantScreen = PSCREEN_FLUIDNC;
        return;
    }

    // Mode banner — tap to cycle transport override (Auto / UART / WiFi)
    if (isTouchInBounds(x, y, PNL_MODE_X, PNL_MODE_Y, PNL_MODE_W, PNL_MODE_H)) {
        cycleTransportOverride();
        return;
    }

#ifdef USE_WIFI
    bool uartMode = (comms_active_mode() == COMMS_MODE_UART);
    bool apMode   = wifi_in_ap_mode();

    // In UART mode no further interactive elements (override is via banner above).
    if (uartMode) return;

    // Action button — Reconfigure WiFi / Cancel AP Setup
    if (isTouchInBounds(x, y, BTN_ACT_X, BTN_ACT_Y, BTN_ACT_W, BTN_ACT_H)) {
        if (apMode) {
            wifi_stop_ap();
            currentPendantScreen = PSCREEN_FLUIDNC;
            return;
        }
        // Clear saved credentials so the next boot starts the AP captive portal.
        Preferences prefs;
        prefs.begin("fluidwifi", false);
        prefs.remove("ssid");
        prefs.remove("pass");
        prefs.remove("ip");
        prefs.end();
        showRestartSplash("Credentials cleared.", "Connect to WiFi:", wifi_ap_ssid());
        delay(2200);
        ESP.restart();
        return;
    }
#endif
}
