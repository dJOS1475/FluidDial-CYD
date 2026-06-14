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
#ifdef USE_WIFI
    // Transport selection:
    //   • An explicit NVS override (set once via the WiFi Setup screen) always
    //     wins and sticks across reboots / firmware updates.
    //   • With NO override stored yet (fresh flash / factory reset → empty NVS)
    //     we fall back to a HARDWARE autodetect: a battery pendant (IP5306 PMIC
    //     present) wants WiFi — and on first boot with no saved credentials
    //     wifi_init() raises the captive-portal AP so it can be configured — while
    //     a wired pendant stays on UART.
    //
    // This is why a clean build still brings up the captive portal: without the
    // autodetect default, an empty-NVS pendant would silently pick UART and never
    // start the portal.  battery_hardware_present() is reliable here because
    // init_hardware()/battery_init() (with its IP5306 retry probe) runs inside
    // init_system(), before setup_pendant() calls comms_init().
    Preferences prefs;
    prefs.begin(COMMS_PREF_NAMESPACE, true);          // read-only
    int forced = prefs.getInt(COMMS_PREF_FORCE_KEY, -1);  // -1 = no explicit choice
    prefs.end();

    bool want_wifi;
    if (forced == TFORCE_WIFI) {
        want_wifi = true;
        dbg_println("Comms: transport = WiFi (NVS override)");
    } else if (forced == TFORCE_UART) {
        want_wifi = false;
        dbg_println("Comms: transport = UART (NVS override)");
    } else {
        want_wifi = battery_hardware_present();
        dbg_printf("Comms: transport = %s (autodetect — no override stored)\n",
                   want_wifi ? "WiFi" : "UART");
    }

    if (want_wifi) {
        _mode       = COMMS_MODE_WIFI;
        _putchar_fn = ws_putchar;
        _getchar_fn = ws_getchar;
        _poll_fn    = wifi_poll;
        wifi_init();              // start STA / AP captive portal
        return;
    }
#endif

    // UART path: the driver was already installed by the hardware init pass.
    // No further action required; function pointers already point at UART.
    _mode = COMMS_MODE_UART;
}

// ── Transport selection (NVS-backed) ─────────────────────────────────────────
// NVS key "tport_force" holds the integer value from the TransportForce enum.
// When the key is ABSENT (fresh flash), comms_init() autodetects from hardware
// (battery pendant → WiFi, wired → UART); this read-only getter still reports
// UART as its nominal default for callers that just want the stored override.
// Once the user toggles transport on the WiFi Setup screen the explicit value
// is written here and sticks across reboots and firmware updates (NVS is
// preserved by the Update path of the web installer).

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
