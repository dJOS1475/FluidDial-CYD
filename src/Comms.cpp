// Copyright (c) 2026 — FluidDial-CYD
// Use of this source code is governed by a GPLv3 license.

#include "Comms.h"
#include "CommsUart.h"
#include "System.h"           // dbg_print*

#include <Preferences.h>

#ifdef USE_WIFI
#include "WiFiConnection.h"
#endif

#define COMMS_PREF_NAMESPACE  "fluidwifi"
#define COMMS_PREF_FORCE_KEY  "tport_force"

// ── Active backend state ─────────────────────────────────────────────────────
// Function pointers default to the UART backend so any byte sent before
// comms_init() has finished (e.g. fnc_realtime(StatusReport) from setup())
// goes safely through UART.  comms_init() flips them to WiFi only when the
// hardware is a battery-equipped (mobile / wireless) pendant.

static CommsMode _mode = COMMS_MODE_UART;

static void (*_putchar_fn)(uint8_t) = uart_backend_putchar;
static int  (*_getchar_fn)()        = uart_backend_getchar;

static void _noop_poll() {}
static void (*_poll_fn)() = _noop_poll;

// ── Public API ───────────────────────────────────────────────────────────────
//
// Transport selection is HARDWARE-DRIVEN, not user-configurable:
//
//   • Battery-equipped pendant (IP5306 PMIC detected on I2C)
//       → mobile / wireless variant → WiFi backend
//   • No battery hardware
//       → wired pendant → UART backend
//
// Rationale: the only pendants that physically need WiFi are battery-powered
// (i.e. not tethered to FluidNC by a UART cable).  Tying the choice to a
// hardware capability that the firmware can detect at boot eliminates the
// "stuck in the wrong mode" failure class entirely — there is no NVS key
// to get out of sync with reality, and no toggle for a user to flip into
// a non-functional state.

void comms_init() {
    TransportForce force = get_transport_force();

#ifdef USE_WIFI
    bool want_wifi = (force == TFORCE_WIFI);

    dbg_printf("Comms: NVS transport = %s\n", transport_force_label());

    if (want_wifi) {
        _mode       = COMMS_MODE_WIFI;
        _putchar_fn = ws_putchar;
        _getchar_fn = ws_getchar;
        _poll_fn    = wifi_poll;
        wifi_init();              // start STA / AP captive portal
        return;
    }
#else
    (void)force;
#endif

    // UART path: the driver was already installed by the hardware init pass.
    // No further action required; function pointers already point at UART.
    _mode = COMMS_MODE_UART;
}

// ── Transport selection (NVS-backed) ─────────────────────────────────────────
// NVS key "tport_force" holds the integer value from the TransportForce enum.
// Default (key absent) is UART so a fresh pendant always boots safely.  The
// WiFi Setup screen lets the user toggle the value via the banner tap, then
// restarts the device to apply.  There is NO hardware autodetect — the user
// makes the choice once, and it sticks across reboots and firmware updates
// (NVS is preserved by the Update path of the web installer).

TransportForce get_transport_force() {
    Preferences prefs;
    prefs.begin(COMMS_PREF_NAMESPACE, true);   // read-only
    int v = prefs.getInt(COMMS_PREF_FORCE_KEY, TFORCE_UART);
    prefs.end();
    return (v == TFORCE_WIFI) ? TFORCE_WIFI : TFORCE_UART;
}

void set_transport_force(TransportForce f) {
    Preferences prefs;
    prefs.begin(COMMS_PREF_NAMESPACE, false);
    prefs.putInt(COMMS_PREF_FORCE_KEY, (int)f);
    prefs.end();
    dbg_printf("Comms: transport set to %s (restart required)\n",
               (f == TFORCE_WIFI) ? "WiFi" : "UART");
}

const char* transport_force_label() {
    return (get_transport_force() == TFORCE_WIFI) ? "WiFi" : "UART";
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
    return (_mode == COMMS_MODE_WIFI) ? "WiFi" : "UART";
}
