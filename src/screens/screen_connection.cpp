#include "pendant_shared.h"
#include "screen_connection.h"
#include "screen_probe.h"          // PROBE_* palette — shared panel/field style
#include "../Comms.h"              // comms_active_mode(), transport_force_*()

#ifdef USE_ESPNOW
#include "../PeerLink.h"
#endif

#include <Esp.h>                   // ESP.restart()

// ── Connection screen (ESPNOW_SPEC.md §6a) ───────────────────────────────────
//
// Replaces the v2.1.x WiFi Setup screen.  With the WiFi backend removed the
// transport choice is binary, so it is two large buttons rather than a cycling
// toggle: what you can pick is visible without tapping anything.
//
// Selecting a transport writes NVS and reboots the pendant after a short
// countdown — comms_init() only runs once at boot, so the backend cannot be
// swapped live and there is no useful state to preserve across the change.

// TRANSPORT panel
static const int TP_Y   = 40, TP_H = 62;
static const int TBTN_Y = 55, TBTN_H = 40;

// LINK panel.  Extends down to just above the action row: the panel is the
// screen's actual content, so it gets the space rather than leaving a 28 px
// dead band above the nav button.
static const int LK_Y = 108, LK_H = 116;

// Action row sits directly above the bottom nav.
static const int ACT_Y = 232, ACT_H = 44;
static const int NAV_Y = 280, NAV_H = 38;

// Transport changes only take effect through comms_init(), which runs once at
// boot — so a change has to be followed by a restart.  Rather than leaving the
// user to work that out, the change arms a short countdown and the pendant
// reboots itself.  0 = no restart pending.
static const unsigned long RESTART_DELAY_MS = 5000;
static unsigned long restartAtMs = 0;

// Seconds still to run, 1..5 (never 0 — at zero we reboot instead of drawing).
static int restartSecsLeft() {
    if (!restartAtMs) return 0;
    const long remain = (long)(restartAtMs - millis());
    if (remain <= 0) return 0;
    return (int)((remain + 999) / 1000);   // round up so "5" shows for a full second
}

void enterConnection() {
    releasePanelSprites();
    restartAtMs = 0;
}

void exitConnection() {
    releasePanelSprites();
}

// Draws the LINK panel only — called by the periodic update so link state and
// signal track live without repainting the whole screen (the flicker lesson
// from v2.1.8: never fillScreen() on a periodic tick).
// Draws the whole panel relative to (ox, base), where base is where the panel's
// top edge lands in the current target.  Anything outside the target is clipped,
// so the same code renders either the full panel or a horizontal band of it.
static void drawLinkBody(LovyanGFX* g, int ox, int base) {
    g->fillRoundRect(ox, base, 230, LK_H, 4, PROBE_BG_PANEL);
    g->setTextSize(1);
    g->setTextColor(PROBE_C_LBLUE);
    g->setCursor(ox + 5, base + 3);
    g->print("LINK");

    if (comms_active_mode() == COMMS_MODE_UART) {
        // Wired: no radio, so show the serial parameters instead.
        g->setTextSize(2);
        g->setTextColor(PROBE_C_GREEN);
        g->setCursor(ox + 7, base + 18);
        g->print("Wired");

        g->setTextSize(1);
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 7, base + 44);
        g->print("Baud");
        g->setCursor(ox + 115, base + 44);
        g->print("Port");
        g->setTextColor(PROBE_C_BLUE);
        g->setCursor(ox + 7, base + 56);
        g->print(pendantMachine.baudRate);
        g->setCursor(ox + 115, base + 56);
        g->print(pendantMachine.port);

        g->setTextColor(PROBE_C_DIMBLUE);
        g->setCursor(ox + 7, base + 74);
        g->print("FluidNC over the RJ12 cable");
        return;
    }

#ifdef USE_ESPNOW
    // Wireless: state, peer identity, signal, and receive-path health.
    if (!espnow_radio_ready()) {
        // The radio itself never started — no amount of waiting will fix it, so
        // do not show "Reconnecting" and imply otherwise.
        g->setTextSize(2);
        g->setTextColor(PROBE_C_RED);
        g->setCursor(ox + 7, base + 18);
        g->print("Radio down");
        g->setTextSize(1);
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 7, base + 46);
        g->print("ESP-NOW failed to start.");
        g->setCursor(ox + 7, base + 58);
        g->print("Power-cycle the pendant; if it");
        g->setCursor(ox + 7, base + 70);
        g->print("persists, switch to UART.");
        return;
    }

    const bool paired = espnow_has_saved_pairing();
    const bool up     = espnow_is_connected();

    g->setTextSize(2);
    g->setTextColor(up      ? PROBE_C_GREEN
                  : paired  ? PROBE_C_YELLOW
                            : PROBE_C_RED);
    g->setCursor(ox + 7, base + 18);
    g->print(up ? "Connected" : (paired ? "Reconnecting" : "Not paired"));

    g->setTextSize(1);
    if (!paired) {
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 7, base + 46);
        g->print("Tap Pair New to add a machine");
        return;
    }

    ESPNowProfileInfo info;
    const int  idx = espnow_active_profile_index();
    const bool got = (idx >= 0) && espnow_get_profile((size_t)idx, info);

    g->setTextColor(COLOR_GRAY_TEXT);
    g->setCursor(ox + 7, base + 44);
    g->print("Machine");
    g->setTextColor(PROBE_C_BLUE);
    g->setCursor(ox + 7, base + 56);
    g->print(got && info.hostname[0] ? info.hostname : "(unnamed)");

    if (got) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X ch%u",
                 info.mac[0], info.mac[1], info.mac[2],
                 info.mac[3], info.mac[4], info.mac[5], (unsigned)info.channel);
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 7, base + 74);
        g->print(buf);
    }

    // Signal bars, right-aligned, greyed when the link is down.
    const int bars = up ? espnow_signal_bars() : 0;
    for (int i = 0; i < 4; i++) {
        const int bh = 6 + i * 5;
        const int bx = ox + 181 + i * 11;
        const int by = base + 58 - bh;
        g->fillRoundRect(bx, by, 8, bh, 1,
                         i < bars ? PROBE_C_GREEN : PROBE_BG_SCREEN);
    }
    if (up) {
        char rb[12];
        snprintf(rb, sizeof(rb), "%ddB", (int)espnow_rssi());
        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 183, base + 62);
        g->print(rb);
    }

    // Receive-path counters, in the space reclaimed by moving the action row
    // down.  They answer the question this screen exists for — "is the radio
    // actually delivering?" — and the two losses have different causes:
    // LOST is packets the radio or the RX queue never handed over, DROP is whole
    // messages abandoned in fragment reassembly.  DROP is invisible to LOST,
    // because those packets DID arrive.  Both read 0 on a healthy link.
    {
        const uint32_t lost = espnow_rx_dropped() + espnow_rx_pkt_dropped();
        const uint32_t drop = espnow_frag_aborted();
        char nb[16];

        g->drawFastHLine(ox + 7, base + 86, 206, PROBE_BG_SCREEN);

        g->setTextColor(COLOR_GRAY_TEXT);
        g->setCursor(ox + 7,   base + 92); g->print("RX BYTES");
        g->setCursor(ox + 105, base + 92); g->print("LOST");
        g->setCursor(ox + 163, base + 92); g->print("DROP");

        g->setTextColor(PROBE_C_BLUE);
        snprintf(nb, sizeof(nb), "%lu", (unsigned long)espnow_rx_bytes());
        g->setCursor(ox + 7, base + 102); g->print(nb);

        g->setTextColor(lost ? PROBE_C_RED : PROBE_C_GREEN);
        snprintf(nb, sizeof(nb), "%lu", (unsigned long)lost);
        g->setCursor(ox + 105, base + 102); g->print(nb);

        g->setTextColor(drop ? PROBE_C_RED : PROBE_C_GREEN);
        snprintf(nb, sizeof(nb), "%lu", (unsigned long)drop);
        g->setCursor(ox + 163, base + 102); g->print(nb);
    }
#endif

}


// The panel is 116 px tall — more than the shared scratch can be relied on to
// hold once the radio has taken its heap, and a panel that exceeds the scratch
// falls back to drawing straight to the display, which is what flickers.  So
// render it as two bands that each fit comfortably.  Each band is still pushed
// atomically, so there is no wipe-then-draw step visible in either.
static const int LK_BAND = 58;

static void drawLinkBand(int bandY, int bandH) {
    int ox, oy;
    LovyanGFX* g = beginPanelSprite(230, bandH, ox, oy, 5, LK_Y + bandY);
    drawLinkBody(g, ox, oy - bandY);
    endPanelSprite(230, bandH, 5, LK_Y + bandY);
}

static void drawLinkPanel() {
    drawLinkBand(0, LK_BAND);
    drawLinkBand(LK_BAND, LK_H - LK_BAND);
}

static void drawTransportPanel() {
    display.fillRoundRect(5, TP_Y, 230, TP_H, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, TP_Y + 3);
    display.print("TRANSPORT");

    // Reflects the STORED choice, not the running one — after a change the
    // selection updates immediately while the restart notice explains the gap.
    const TransportForce sel = get_transport_force();
    drawButton(rowBtnX(2, 0), TBTN_Y, rowBtnWAt(2, 0), TBTN_H, "UART",
               sel == TFORCE_UART ? COLOR_ORANGE : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(rowBtnX(2, 1), TBTN_Y, rowBtnWAt(2, 1), TBTN_H, "ESP-NOW",
               sel == TFORCE_ESPNOW ? COLOR_ORANGE : COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
}

// Pair / Machines are only meaningful on the wireless transport.
static void drawActionRow() {
    display.fillRect(5, ACT_Y, 230, ACT_H, COLOR_BACKGROUND);
    if (restartAtMs) {
        display.fillRoundRect(5, ACT_Y, 230, ACT_H, 4, PROBE_WARN_BG);
        display.drawRoundRect(5, ACT_Y, 230, ACT_H, 4, PROBE_WARN_BDR);
        display.setTextSize(1);
        display.setTextColor(COLOR_ORANGE);
        display.setCursor(14, ACT_Y + 8);
        display.print("Transport changed,");
        display.setTextSize(2);
        display.setCursor(14, ACT_Y + 21);
        display.printf("rebooting in %d", restartSecsLeft());
        return;
    }
    if (get_transport_force() != TFORCE_ESPNOW) return;   // UART: nothing to pair
    drawButton(rowBtnX(2, 0), ACT_Y, rowBtnWAt(2, 0), ACT_H, "Pair New",
               PROBE_BTN_TEAL, COLOR_WHITE, 2);
    drawButton(rowBtnX(2, 1), ACT_Y, rowBtnWAt(2, 1), ACT_H, "Machines",
               COLOR_INDIGO, COLOR_WHITE, 2);
}

void drawConnectionScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("CONNECTION");
    drawTransportPanel();
    drawLinkPanel();
    drawActionRow();
    drawButton(5, NAV_Y, 230, NAV_H, "Main Menu", COLOR_BLUE, COLOR_WHITE, 2);
}

void updateConnectionDisplay() {
    if (currentPendantScreen != PSCREEN_CONNECTION) return;

    if (restartAtMs) {
        // Countdown owns the screen: reboot when it expires, otherwise repaint
        // the banner only when the displayed second actually changes (the tick
        // runs at 100 ms — redrawing every tick would flicker the text).
        if ((long)(millis() - restartAtMs) >= 0) {
            dbg_println("Transport changed — restarting");
            delay_ms(50);          // let the last frame land before we go
            ESP.restart();         // never returns
        }
        static int lastShown = -1;
        const int  secs = restartSecsLeft();
        if (secs != lastShown) {
            lastShown = secs;
            drawActionRow();
        }
        return;                    // link panel is irrelevant while rebooting
    }

    drawLinkPanel();          // fields only — no fillScreen on a periodic tick
}

void handleConnectionTouch(int x, int y) {
    // Transport selection — writes NVS, takes effect on restart.
    for (int i = 0; i < 2; i++) {
        if (isTouchInBounds(x, y, rowBtnX(2, i), TBTN_Y, rowBtnWAt(2, i), TBTN_H)) {
            const TransportForce want = (i == 0) ? TFORCE_UART : TFORCE_ESPNOW;
            if (want == get_transport_force()) return;      // already selected
            set_transport_force(want);
            // Re-selecting during the countdown is the escape hatch for a
            // mis-tap: it stores the other transport and restarts the timer,
            // so there is no way to be stuck rebooting into the wrong one.
            restartAtMs = millis() + RESTART_DELAY_MS;
            drawTransportPanel();
            drawActionRow();
            return;
        }
    }

#ifdef USE_ESPNOW
    if (!restartAtMs && get_transport_force() == TFORCE_ESPNOW) {
        if (isTouchInBounds(x, y, rowBtnX(2, 0), ACT_Y, rowBtnWAt(2, 0), ACT_H)) {
            currentPendantScreen = PSCREEN_ESPNOW_PAIR;
            return;
        }
        if (isTouchInBounds(x, y, rowBtnX(2, 1), ACT_Y, rowBtnWAt(2, 1), ACT_H)) {
            currentPendantScreen = PSCREEN_ESPNOW_MACHINES;
            return;
        }
    }
#endif

    if (isTouchInBounds(x, y, 5, NAV_Y, 230, NAV_H)) {
        currentPendantScreen = PSCREEN_MAIN_MENU;
    }
}
