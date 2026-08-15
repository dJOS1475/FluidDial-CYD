// Copyright (c) 2026 — FluidDial-CYD
// Use of this source code is governed by a GPLv3 license.

#include "Comms.h"
#include "CommsUart.h"
#include "System.h"           // dbg_print*

#include <Preferences.h>

#ifdef USE_ESPNOW
#include "PeerLink.h"
#endif

#define COMMS_PREF_NAMESPACE  "fluidwifi"
#define COMMS_PREF_FORCE_KEY  "tport_force"

// ── Active backend state ─────────────────────────────────────────────────────
// Function pointers default to the UART backend so any byte sent before
// comms_init() has finished (e.g. fnc_realtime(StatusReport) from setup())
// goes safely through UART.  comms_init() only moves them once it has chosen.

static CommsMode _mode = COMMS_MODE_UART;

static void (*_putchar_fn)(uint8_t) = uart_backend_putchar;
static int  (*_getchar_fn)()        = uart_backend_getchar;

static void _noop_poll() {}
static void (*_poll_fn)() = _noop_poll;

// ── Public API ───────────────────────────────────────────────────────────────
//
// Transport is a stored user choice (NVS "tport_force"), defaulting to UART.
// UART is the default deliberately: a freshly-flashed pendant never starts a
// radio unprompted, and a pendant with no ESP-NOW pairing can never boot into a
// state with no working link and no UI to fix it.

void comms_init() {
    Preferences prefs;
    prefs.begin(COMMS_PREF_NAMESPACE, true);              // read-only
    int forced = prefs.getInt(COMMS_PREF_FORCE_KEY, TFORCE_UART);
    prefs.end();

    // Any value we don't recognise falls back to UART.  This matters on upgrade
    // from v2.1.x, where a WiFi pendant has TFORCE_WIFI(1) stored and the WiFi
    // backend no longer exists: without this guard the pendant would boot to a
    // dead transport with no UI left to change it, and would need a reflash to
    // recover.  Treat "unknown" as "use the safe wired path", always.
    if (forced != TFORCE_UART
#ifdef USE_ESPNOW
        && forced != TFORCE_ESPNOW
#endif
    ) {
        dbg_printf("Comms: stored transport %d not available in this build — using UART\n",
                   forced);
        forced = TFORCE_UART;
    }

#ifdef USE_ESPNOW
    if (forced == TFORCE_ESPNOW) {
        _mode       = COMMS_MODE_ESPNOW;
        _putchar_fn = espnow_putchar;
        _getchar_fn = espnow_getchar;
        _poll_fn    = espnow_poll;
        dbg_println("Comms: transport = ESP-NOW");
        espnow_init();
        return;
    }
#endif

    // UART path: the driver was already installed by the hardware init pass.
    // No further action required; function pointers already point at UART.
    dbg_println("Comms: transport = UART");
    _mode = COMMS_MODE_UART;
}

// ── Transport selection (NVS-backed) ─────────────────────────────────────────
// NVS key "tport_force" holds the integer value from the TransportForce enum.
// The value is written by the Connection screen and sticks across reboots and
// firmware updates (NVS is preserved by the web installer's Update path).

TransportForce get_transport_force() {
    Preferences prefs;
    prefs.begin(COMMS_PREF_NAMESPACE, true);   // read-only
    int v = prefs.getInt(COMMS_PREF_FORCE_KEY, TFORCE_UART);
    prefs.end();
#ifdef USE_ESPNOW
    if (v == TFORCE_ESPNOW) return TFORCE_ESPNOW;
#endif
    return TFORCE_UART;   // unknown / removed transports read back as UART
}

void set_transport_force(TransportForce f) {
    Preferences prefs;
    prefs.begin(COMMS_PREF_NAMESPACE, false);
    prefs.putInt(COMMS_PREF_FORCE_KEY, (int)f);
    prefs.end();
    dbg_printf("Comms: transport set to %s (restart required)\n",
               transport_force_label());
}

const char* transport_force_label() {
#ifdef USE_ESPNOW
    if (get_transport_force() == TFORCE_ESPNOW) return "ESP-NOW";
#endif
    return "UART";
}

void comms_putchar(uint8_t c) {
    _putchar_fn(c);
}

int comms_getchar() {
    return _getchar_fn();
}

void comms_poll() {
    _poll_fn();
}

CommsMode comms_active_mode() {
    return _mode;
}

const char* comms_mode_name() {
#ifdef USE_ESPNOW
    if (_mode == COMMS_MODE_ESPNOW) return "ESP-NOW";
#endif
    return "UART";
}
