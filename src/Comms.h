// Copyright (c) 2026 — FluidDial-CYD
// Use of this source code is governed by a GPLv3 license.

#pragma once
#include <stdint.h>

// ── Comms facade ─────────────────────────────────────────────────────────────
//
// Single entry point for all FluidNC byte-level I/O.  The application calls
// fnc_putchar() / fnc_getchar() (from SystemArduino.cpp) which forward into
// the four functions declared here.  The facade picks exactly ONE backend at
// boot — UART (CommsUart) or ESP-NOW (PeerLink) — and dispatches every byte
// through a fixed function pointer.  No mode check, no NVS lookup, and no
// cross-backend code runs in the hot path.
//
// Backends never see each other:
//   • CommsUart only knows about the ESP-IDF UART driver.
//   • PeerLink only knows about esp_now / esp_wifi.
//   • Comms.cpp is the only file that knows both exist.
//
// Both transports are byte streams, so everything above this layer — probing,
// jogging, SD streaming, the $File/SendJSON macro chain — is transport-agnostic
// and behaves identically either way.
//
// In UART mode espnow_init() is never called and the radio stays cold.  In
// ESP-NOW mode the UART driver is still installed by the hardware init
// (idempotent, ~4 KB RX buffer) but no bytes are routed through it.
//
// The WiFi/WebSocket backend was removed in v2.2.0 — see ESPNOW_SPEC.md.

enum CommsMode {
    COMMS_MODE_UART   = 0,
    COMMS_MODE_ESPNOW = 2,   // 1 was COMMS_MODE_WIFI (removed in v2.2.0)
};

// Transport selection stored in NVS (namespace "fluidwifi", key "tport_force").
// The user chooses on the Connection screen; there is no hardware autodetect.
// UART is the default so a freshly-flashed pendant always boots into the safe
// wired mode and never starts a radio unprompted.
//
// Value 1 was TFORCE_WIFI and is now retired.  comms_init() treats any value it
// does not recognise as UART, so a pendant upgrading from v2.1.x with WiFi
// stored comes up wired rather than on a dead transport it cannot escape.
enum TransportForce {
    TFORCE_UART   = 0,   // default — UART transport
    TFORCE_ESPNOW = 2,   // ESP-NOW transport (1 = removed WiFi backend)
};

// Read / write the selection.  set_transport_force() does NOT restart — the
// caller is expected to show a "restarting" splash and call ESP.restart()
// so the new selection takes effect via the next comms_init().
TransportForce get_transport_force();
void           set_transport_force(TransportForce f);
const char*    transport_force_label();   // "UART" or "WiFi"

// Pick the active backend from the stored NVS selection (defaulting to UART),
// call that backend's own init() and wire up the dispatchers.  Must be
// called from the SAME task (Core 0 pendant_hw_task) that will later
// service comms_poll() / comms_getchar(), so the backend's ring buffers
// are only ever touched by one core.
void comms_init();

// Hot-path I/O — single indirect call after comms_init() has run.  Before
// comms_init() is called the pointers point at the UART backend, which is
// safe because the UART hardware is initialised by the hardware setup pass.
void comms_putchar(uint8_t c);
int  comms_getchar();      // returns -1 if no byte is available

// Periodic service hook.  No-op when the active backend is UART; in ESP-NOW
// mode this drains received packets into the RX ring buffer and handles
// keepalive / reconnect.
void comms_poll();

// Diagnostics / UI — used by the Connection screen and the FluidNC info
// screen to show which transport is live.
CommsMode   comms_active_mode();
const char* comms_mode_name();   // "UART" or "ESP-NOW"
