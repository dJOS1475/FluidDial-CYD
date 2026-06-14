// Copyright (c) 2026 — FluidDial-CYD
// Use of this source code is governed by a GPLv3 license.

#pragma once
#include <stdint.h>

// ── Comms facade ─────────────────────────────────────────────────────────────
//
// Single entry point for all FluidNC byte-level I/O.  The application calls
// fnc_putchar() / fnc_getchar() (from SystemArduino.cpp) which forward into
// the four functions declared here.  The facade picks exactly ONE backend at
// boot — either UART (CommsUart) or WiFi/WebSocket (WiFiConnection) — and
// dispatches every byte through a fixed function pointer.  No mode check, no
// NVS lookup, and no cross-backend code runs in the hot path.
//
// Backends never see each other:
//   • CommsUart only knows about the ESP-IDF UART driver.
//   • WiFiConnection only knows about WiFi.h / TCP.
//   • Comms.cpp is the only file that knows both exist.
//
// In UART mode wifi_init() is never called — the WiFi radio and TCP stack
// stay cold.  In WiFi mode the UART driver is still installed by the
// hardware init (idempotent, ~4 KB RX buffer) but no bytes are routed
// through it.

enum CommsMode {
    COMMS_MODE_UART = 0,
    COMMS_MODE_WIFI = 1,
};

// Transport selection stored in NVS (namespace "fluidwifi", key "tport_force").
// User chooses UART or WiFi manually via the WiFi Setup screen — there is no
// hardware autodetect.  UART is the default so a freshly-flashed pendant
// always boots into the safe mode and won't try to start WiFi unprompted.
enum TransportForce {
    TFORCE_UART = 0,   // default — UART transport
    TFORCE_WIFI = 1,   // WiFi transport
};

// Read / write the selection.  set_transport_force() does NOT restart — the
// caller is expected to show a "restarting" splash and call ESP.restart()
// so the new selection takes effect via the next comms_init().
TransportForce get_transport_force();
void           set_transport_force(TransportForce f);
const char*    transport_force_label();   // "UART" or "WiFi"

// Pick the active backend based on the detected hardware:
//   • battery_hardware_present() == true  → WiFi backend (mobile pendant)
//   • battery_hardware_present() == false → UART backend (wired pendant)
// Calls the backend's own init() and wires up the dispatchers.  Must be
// called from the SAME task (Core 0 pendant_hw_task) that will later
// service comms_poll() / comms_getchar(), so the backend's ring buffers
// are only ever touched by one core.
void comms_init();

// Hot-path I/O — single indirect call after comms_init() has run.  Before
// comms_init() is called the pointers point at the UART backend, which is
// safe because the UART hardware is initialised by the hardware setup pass.
void comms_putchar(uint8_t c);
int  comms_getchar();      // returns -1 if no byte is available

// Periodic service hook.  No-op when the active backend is UART; in WiFi
// mode this drains the TCP socket into the RX ring buffer, runs the AP
// captive portal, and handles reconnects.
void comms_poll();

// Diagnostics / UI — used by the WiFi setup screen and the FluidNC info
// screen to show which transport is live.
CommsMode   comms_active_mode();
const char* comms_mode_name();   // "UART" or "WiFi"
