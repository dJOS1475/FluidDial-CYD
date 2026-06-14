#pragma once

// WiFi / WebSocket transport layer for FluidDial-CYD.
//
// One of two transport backends (the other is CommsUart).  The Comms facade
// in Comms.cpp picks exactly one at boot based on hardware autodetection —
// battery-equipped pendants (IP5306 PMIC present) use WiFi; wired pendants
// use UART.  As a result, wifi_init() / wifi_poll() and ws_putchar() /
// ws_getchar() are only ever invoked on battery hardware.
//
// Dual-core architecture:
//   Core 0 (pendant_hw_task): calls wifi_poll(), ws_putchar(), ws_getchar()
//   Core 1 (Arduino loop):    display, touch — no WiFi state access

#ifdef USE_WIFI

#include <stdint.h>
#include <functional>

struct WiFiConfig {
    char ssid[64];
    char password[64];
    char fluidnc_ip[40];
    bool valid;
};

// ── Lifecycle ──────────────────────────────────────────────────────────────────
// Initialise WiFi.  Must be called once before the main loop begins.
// If no credentials are saved and auto_ap is true, starts the AP captive
// portal at 192.168.4.1 so the user can configure SSID / IP.
void wifi_init(bool auto_ap = true);

// Drive the WiFi state machine.  Must be called from the same task
// (Core 0 pendant_hw_task) that calls fnc_putchar() / fnc_getchar(),
// so the TCP RX ring buffer is never accessed from two cores simultaneously.
void wifi_poll();

// ── Status queries ─────────────────────────────────────────────────────────────
bool wifi_is_connected();         // ESP32 STA joined the network
bool websocket_is_connected();    // WebSocket connection to FluidNC is up
bool wifi_in_ap_mode();           // Running as setup access point
const char* wifi_ap_ssid();       // AP network name ("FluidDial")
const char* wifi_status_str();    // Human-readable status for UI display
const bool  wifi_not_ready();     // true while WiFi or TCP not yet established
const char* wifi_last_error();    // Last human-readable error ("Check password" etc.)
int         wifi_signal_bars();   // 0–4 signal-strength bars (0 = not connected)

// ── AP setup portal ────────────────────────────────────────────────────────────
void wifi_start_ap_setup();       // Start captive-portal AP "FluidDial"
void wifi_stop_ap_and_restart();  // Save done — stop AP and reboot
void wifi_stop_ap();              // Stop AP without restarting (go back to settings)

// Plain HTTP GET of a FluidNC filesystem file (e.g. "/preferences.json"),
// streamed to on_chunk.  Used to fetch macros over HTTP instead of the
// WebSocket (which truncates large $File/SendJSON replies).  Call from a
// dedicated task — it blocks until the transfer completes.  Returns the HTTP
// status (200 = OK) or a negative setup error.
int wifi_http_get(const char* path,
                  std::function<void(const uint8_t*, size_t)> on_chunk,
                  int timeout_ms = 5000);

// Temporarily close the WebSocket so an HTTP fetch can use FluidNC's shared
// port 80 without contention (the GET otherwise hangs).  suspend() blocks until
// Core 0 has closed the socket; resume() lets Core 0 reopen it.  Always pair.
void wifi_ws_suspend();
void wifi_ws_resume();

// Graceful shutdown for power-off / sleep.  Sends a WebSocket CLOSE frame so
// FluidNC frees the channel slot immediately (instead of waiting for its own
// ghost-connection timeout), then latches a flag so wifi_poll() stops
// servicing / reconnecting the socket.  MUST be called from Core 0 (the task
// that owns _wsClient) — the pendant calls it from the Core-0 long-press
// handler, before either core deep-sleeps.  No-op in UART mode.
void wifi_graceful_disconnect();

// ── Credential storage ──────────────────────────────────────────────────────────
// Stored in NVS namespace "fluidwifi".  Only ssid / pass / ip are persisted —
// there is no transport-mode key (transport is decided by hardware autodetect).
void       wifi_save_config(const char* ssid, const char* password, const char* ip);
WiFiConfig wifi_load_config();
WiFiConfig wifi_active_config();   // Config loaded at wifi_init() time (no NVS read)

// ── Transport primitives used by Comms.cpp's WiFi dispatcher ───────────────────
void ws_putchar(uint8_t c);  // Send one byte to FluidNC via TCP
int  ws_getchar();           // Pop one byte from TCP RX ring buffer (-1 if empty)

#endif  // USE_WIFI
