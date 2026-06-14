// 2026 - Figamore (original WebSocket scaffolding)
// 2026 - Paul Mokbel (Telnet transport, since replaced)
// 2026 - CYD dual-core adaptation + WebSocket migration (Derek Osborn)
// Use of this source code is governed by a GPLv3 license.
//
// WiFi transport layer for FluidDial-CYD.
//
// Provides fnc_putchar() / fnc_getchar() over a WebSocket connection to
// FluidNC's WebUI socket on port 81 (path "/").  This is the same channel
// FluidNC's browser interface uses, so it's exercised constantly by every
// FluidNC install and is well understood on the controller side.
//
// History:
//   • The very first cut of this file connected via raw TCP / Telnet on
//     port 23.  That worked for streaming but ran into byte-level pain:
//     partial-send recovery, manual flow-control ACKs (0xB2), kernel
//     SO_SNDTIMEO tuning, and a fragile RX ring with multi-core races.
//   • Switching to WebSocket eliminates all of that.  Message boundaries
//     are preserved by the framing; the library handles flow control and
//     reconnect; we just sendTXT() a full line or sendBIN() a single
//     realtime byte and receive whole frames via the event callback.
//
// CYD adaptation notes (differences from upstream bdring/FluidDial):
//   • No Scene.h / request_redisplay() — the 100 ms sprite-refresh loop
//     on Core 1 picks up state changes automatically.
//   • Default transport is UART (manual choice via WiFi Setup screen)
//     so upgrading existing firmware never silently breaks wired pendants.
//   • HARDCODE_TEST_WIFI=1 bypasses NVS so end-to-end WebSocket connectivity
//     can be validated at compile time.
//   • wifi_init() auto-starts the captive-portal AP on first boot when
//     auto_ap=true (no separate "transport select" screen needed for setup).
//   • wifi_poll() and fnc_getchar() both run on Core 0 (pendant_comms_task)
//     so the RX ring buffer is never touched from two cores simultaneously.

#ifdef ARDUINO

#include "WiFiConnection.h"
#include "FluidNCModel.h"
#include "System.h"

// Boot-stage tracker (RTC memory) — defined in ardmain.cpp.  Updated at key
// milestones so a post-crash boot can report where we got to last time.
extern uint32_t rtcLastBootStage;

#include <Esp.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

#include <WebSocketsClient.h>
#include <HTTPClient.h>   // file fetch (macros) over plain HTTP, like FluidNC's WebUI
#include <functional>
#include <mdns.h>   // mdns_query_a() — ESP-IDF multicast DNS

// ─── Configuration ────────────────────────────────────────────────────────────

#define WIFI_AP_SSID            "FluidDial"
#define WIFI_AP_PASS            ""       // Open AP — no password needed
#define PREF_NAMESPACE          "fluidwifi"
// FluidNC mounts AsyncWebSocket("/") on the same AsyncWebServer that
// serves the WebUI HTML — so the WebSocket endpoint lives at the HTTP
// port (default 80), path "/", NOT on a separate port 81 like the
// older links2004-based WebSocketsServer setup.  Confirmed by reading
// FluidNC's WebUIServer.cpp:
//
//     _webserver    = new AsyncWebServer(_port);         // _port = 80
//     _socket_server = new AsyncWebSocket("/");
//     _webserver->addHandler(_socket_server);
#define FLUIDNC_WS_PORT         80
#define FLUIDNC_WS_PATH         "/"
// 8 KB so a single FluidNC send burst (its WS/TCP send window, ~5.7 KB) fits
// without overflowing before Core 0 drains it.  A large reply like the macros
// preferences.json is dumped without ack-gating, so the ring must absorb a
// whole window-worth at once; 4 KB overflowed and corrupted the JSON, which
// was why macros never loaded while the smaller SD listing did.
#define RX_BUF_SIZE             8192
#define TX_BUF_SIZE             512
#define WIFI_RETRY_DELAY_MS     15000    // Retry WiFi.begin() after a failure
#define DNS_RETRY_DELAY_MS      5000     // Retry hostname resolution after a failure
#define WS_RECONNECT_MS         2000     // Library auto-reconnect interval
#define WS_PING_INTERVAL_MS     10000    // Library-level WebSocket PING
#define WS_PONG_TIMEOUT_MS      3000     // Wait this long for PONG
#define WS_PONG_MISSES          2        // Disconnect after this many missed pongs
//
// Connection-health detection is OWNED BY THE LIBRARY.  The WebSocket
// PING/PONG heartbeat above catches a wedged FluidNC in roughly
// WS_PING_INTERVAL_MS + WS_PONG_MISSES * WS_PONG_TIMEOUT_MS = ~16 s,
// and triggers an automatic reconnect on the WS_RECONNECT_MS interval.
// We deliberately do NOT layer our own RX-silence watchdog on top —
// duplicate disconnect logic was tearing down healthy connections
// during normal homing (when commands legitimately pile up while the
// machine is moving).  The UART backend trusts the wire; the WebSocket
// backend trusts the library.  Same contract.
//
// Status polling is also pushed up to the application layer
// (fnc_is_connected on Core 0, plus FluidNC's setReportInterval(200)
// auto-broadcast).  This file no longer injects its own '?' polls.

// ── Hard-coded test credentials ───────────────────────────────────────────────
// Set HARDCODE_TEST_WIFI to 1 at compile time to bypass NVS credential storage
// and force WiFi mode.  Fill in the three values below, build, and test end-to-
// end WebSocket connectivity.  Reset to 0 for production firmware.
#define HARDCODE_TEST_WIFI 0
#define TEST_WIFI_SSID     "YourNetworkSSID"
#define TEST_WIFI_PASS     "YourNetworkPassword"
#define TEST_FLUIDNC_IP    "192.168.1.100"

// ─── Globals ──────────────────────────────────────────────────────────────────

static WebSocketsClient _wsClient;
static bool             _ws_client_inited = false;   // onEvent registered, etc.
static bool             _ws_begin_called  = false;   // begin() called → loop() active
static bool             _shutting_down    = false;   // power-off: stop servicing WS
static volatile bool    _ws_suspend_req   = false;   // request: close WS, stop servicing
static volatile bool    _ws_suspended     = false;   // ack: WS is closed (set by Core 0)
static char             _fluidnc_remote_ip[40] = {};

static WebServer        httpServer(80);
static DNSServer        dnsServer;

static bool _ap_mode            = false;
static bool _ws_connected       = false;   // FluidNC WebSocket up
static bool _wifi_was_connected = false;
static bool _wifi_stack_started = false;
static WiFiConfig _active_cfg   = {};
static wl_status_t      _last_wifi_status       = WL_IDLE_STATUS;
static const char*      _wifi_error_msg          = nullptr;
static uint32_t         _wifi_retry_at           = 0;
static volatile uint8_t _wifi_disconnect_reason  = 0;
static bool             _wifi_ever_connected     = false;
static uint32_t         _wifi_connect_start_ms   = 0;
static uint8_t          _handshake_timeout_count = 0;

// RX ring buffer — filled by the WebSocket event callback (TEXT/BIN frames),
// drained by ws_getchar() ← fnc_getchar().  Both run on Core 0.
static uint8_t _rx_buf[RX_BUF_SIZE];
static int     _rx_head = 0;
static int     _rx_tail = 0;
// Spinlock guarding _rx_head / _rx_tail.
//
// Historically this guarded a race where Core 1's fnc_send_line() spin could
// reach into rx_pop() while Core 0's drain was also there — both threads
// reading the same _rx_tail and double-dispensing the byte.  That dual-
// consumer hazard was removed by gating fnc_getchar() to Core 0 only
// (SystemArduino.cpp).  We keep the spinlock anyway: cheap, and it guards
// against any future code path that might be tempted to push or pop from
// a different task (eg. a WebSocket library version that dispatches the
// onEvent callback off-thread).
static portMUX_TYPE _rx_mux = portMUX_INITIALIZER_UNLOCKED;

// TX line-assembly buffer — used ONLY on Core 0 inside tx_drain() to
// accumulate a g-code / $ command line up to its terminating '\n' before
// handing it to the WebSocket library.  Never touched off Core 0.
static uint8_t _tx_buf[TX_BUF_SIZE];
static int     _tx_len = 0;

// ── TX ring buffer (CRITICAL for correctness) ────────────────────────────────
//
// The arduinoWebSockets client is NOT thread-safe.  _wsClient.loop() runs on
// Core 0 (pendant_comms_task), but command bytes are produced on BOTH cores:
//
//   • Core 0: the '?' status poll from fnc_is_connected().
//   • Core 1: jog ($J=...), homing ($H), feed-hold, the red-button $X,
//     overrides, etc. — all routed through fnc_putchar() from
//     pendant_hw_task / the Arduino loop task.
//
// Calling _wsClient.sendBIN()/sendTXT() from Core 1 while loop() runs on
// Core 0 corrupts the client's internal TCP framing state — the symptom
// being "status updates stop and jog does nothing the moment you touch the
// wheel."  The old Telnet/UART backends never hit this because lwIP sockets
// and the ESP-IDF UART driver both serialise concurrent writers internally;
// arduinoWebSockets does not.
//
// Fix: ws_putchar() (any core) only ENQUEUES raw bytes into this ring under a
// spinlock.  Core 0's wifi_poll() drains the ring through the framing logic
// and is the ONLY place that ever calls _wsClient.send*().  This mirrors the
// RX-ring design and makes the WebSocket client strictly single-threaded —
// exactly the contract the library requires.
#define TX_RING_SIZE            1024
static uint8_t _tx_ring[TX_RING_SIZE];
static int     _tx_ring_head = 0;
static int     _tx_ring_tail = 0;
static portMUX_TYPE _tx_mux = portMUX_INITIALIZER_UNLOCKED;

// Timestamp of the last frame received over the WebSocket.  Kept for
// diagnostics / future debug display only; the library's PING/PONG
// heartbeat owns "is the link alive?" detection.
static uint32_t _last_rx_byte_ms = 0;

// ─── Async hostname resolution ────────────────────────────────────────────────

static volatile bool _dns_resolving   = false;
static volatile bool _dns_done        = false;
static volatile bool _dns_ok          = false;
static char          _dns_result_str[40] = {};
static uint32_t      _dns_retry_at    = 0;

static bool is_dotted_decimal(const char* s) {
    for (const char* p = s; *p; p++) {
        if (!((*p >= '0' && *p <= '9') || *p == '.')) return false;
    }
    return true;
}

// Resolve a hostname to IPv4.  Uses mdns_query_a() for *.local names and
// unicast DNS (WiFi.hostByName) for everything else.
//
// The mDNS stack is initialised lazily on first use.  We deliberately do NOT
// call MDNS.begin() (which advertises the pendant as "fluiddial.local" and
// spawns a responder task that was implicated in post-connect crashes).  A
// bare mdns_init() is enough to let mdns_query_a() work safely.
static bool resolve_host_strict(const char* host, IPAddress& out) {
    size_t len    = strlen(host);
    const char* dot_local = ".local";
    size_t dl_len = strlen(dot_local);
    if (len > dl_len && strcasecmp(host + len - dl_len, dot_local) == 0) {
        // One-shot mDNS init.  esp_err_t==ESP_ERR_INVALID_STATE means it was
        // already initialised, which is fine.
        static bool _mdns_inited = false;
        if (!_mdns_inited) {
            esp_err_t e = mdns_init();
            _mdns_inited = (e == ESP_OK || e == ESP_ERR_INVALID_STATE);
            dbg_printf("mdns_init: err=%d (%s)\n", (int)e, esp_err_to_name(e));
            if (!_mdns_inited) return false;
        }

        char base[64];
        size_t base_len = len - dl_len;
        if (base_len >= sizeof(base)) return false;
        memcpy(base, host, base_len);
        base[base_len] = '\0';
        esp_ip4_addr_t addr = {};
        esp_err_t      err  = mdns_query_a(base, 2000, &addr);
        if (err != ESP_OK) {
            dbg_printf("mDNS: %s.local err=%d\n", base, (int)err);
            return false;
        }
        out = IPAddress(addr.addr);
        return true;
    }
    return WiFi.hostByName(host, out) == 1;
}

static void dnsResolveTask(void* /*param*/) {
    IPAddress ip;
    bool ok = resolve_host_strict(_active_cfg.fluidnc_ip, ip);
    if (ok) ip.toString().toCharArray(_dns_result_str, sizeof(_dns_result_str));
    _dns_ok        = ok;
    _dns_done      = true;   // written last — signals main task
    _dns_resolving = false;
    vTaskDelete(nullptr);
}

static void start_dns_resolve() {
    _dns_done      = false;
    _dns_ok        = false;
    _dns_resolving = true;
    dbg_printf("Resolving hostname (async): %s\n", _active_cfg.fluidnc_ip);
    BaseType_t ok = xTaskCreate(dnsResolveTask, "dns_resolve", 4096, nullptr, 1, nullptr);
    if (ok != pdPASS) {
        _dns_resolving  = false;
        _wifi_error_msg = "DNS resolve task start failed";
    }
}

extern volatile int pending_nowait_sends;  // FluidNCModel.cpp

// Forward decls — onWsEvent pushes RX bytes via rx_push and enqueues the
// connect-time '?' via tx_ring_push; ws_getchar pumps the transport via
// tx_drain (all defined further down).
static inline void rx_push(uint8_t c);
static inline void tx_ring_push(uint8_t c);
static void        tx_drain();

// ─── WebSocket event callback ─────────────────────────────────────────────────
//
// Runs on the same task that calls _wsClient.loop() — that's Core 0's
// pendant_comms_task.  Single-producer for the RX ring, which is the
// invariant the rest of the system depends on.
//
// FluidNC sends serial output (status reports, "ok", JSON, etc.) as either
// TEXT or BIN frames depending on whether the bytes are 7-bit ASCII or not.
// We treat both identically: every byte goes into the RX ring, the GRBL
// parser handles framing on '\n'.
//
// The library also surfaces PING/PONG/ERROR/DISCONNECTED events but it
// handles ping/pong itself; we just track connection state and reset
// counters when the socket flips.
static void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            if (_ws_connected) {
                dbg_println("WS: disconnected");
            }
            _ws_connected = false;
            // No acks will come for any commands still in flight.  Reset
            // the pending counter so the jog throttle re-opens cleanly
            // on the next connect.
            pending_nowait_sends = 0;
            set_disconnected_state();
            break;

        case WStype_CONNECTED:
            dbg_printf("WS: connected to %s\n", payload ? (const char*)payload : "(null)");
            _ws_connected    = true;
            _last_rx_byte_ms = millis();
            rtcLastBootStage = 9;       // stage 9: WebSocket handshake complete
            // Belt-and-braces: also clear pending_nowait_sends on the connect
            // edge.  WStype_DISCONNECTED already does this, but if the
            // library reconnected silently (eg. PING/PONG miss → automatic
            // reopen without firing DISCONNECTED first on some library
            // builds) we'd inherit a stuck counter and immediately throttle
            // jog.  Safer to reset on every fresh handshake.
            pending_nowait_sends = 0;
            // Also flush the TX line buffer.  If we were mid-line when the
            // previous connection dropped, leftover bytes would be prepended
            // to the next real command — turning eg. "$H\n" into
            // "$J=G91X1F1000$H\n" and corrupting both.
            _tx_len = 0;
            // Kick FluidNC with '?' so the first status report lands quickly.
            // Enqueue it on the TX ring rather than calling _wsClient.sendBIN()
            // directly here: this handler runs INSIDE _wsClient.loop(), and
            // sending from within the library's own event dispatch is fragile
            // (re-entrant use of the client's send buffer).  tx_drain() will
            // ship it on the very next line, immediately after loop() returns.
            tx_ring_push('?');
            break;

        case WStype_TEXT:
            // TEXT frames from FluidNC carry WebUI control messages ONLY —
            // "currentID:N", "CURRENT_ID:N", "activeID:N", "ACTIVE_ID:N",
            // "PING:60000:60000".  None of these are GRBL output, and most
            // arrive WITHOUT a trailing newline (see WSChannels::handleEvent
            // in FluidNC's WSChannel.cpp).  Pushing them into the parser's
            // line buffer corrupts the first status report — the leading
            // "currentID:5" stays buffered until the next '\n' arrives,
            // then "currentID:5<Idle|MPos:...>" gets handed to parse_report
            // which silently discards it as malformed.  Symptom: pendant
            // status sits on N/C forever even though FluidNC is auto-
            // reporting every 200 ms.
            //
            // FluidNC sends ALL actual GRBL output (status, "ok", [MSG:...],
            // [JSON:...]) via WSChannel::write() which goes out as BIN
            // frames, so dropping TEXT payloads is lossless for us.  Just
            // note the timestamp so the staleness watchdog stays happy.
            _last_rx_byte_ms = millis();
            update_rx_time();
            break;

        // GRBL data.  WStype_BIN is a complete binary message; the three
        // FRAGMENT cases are the pieces of a binary message that FluidNC's
        // AsyncWebSocket split across multiple WebSocket frames (it does this
        // for large payloads — e.g. a big $Files/ListGCode or preferences.json
        // reply).  The links2004 library does NOT reassemble fragments for us;
        // it hands them over piece by piece as:
        //     FRAGMENT_BIN_START (fin=0)  →  FRAGMENT … FRAGMENT  →  FRAGMENT_FIN
        // If we only handled WStype_BIN, any fragmented reply would be silently
        // dropped in its entirety — a cause of SD-card / macros "Loading…".
        // We treat all four identically: PUSH the bytes into the RX ring.
        //
        // We do NOT call collect() here.  collect() must run OUTSIDE
        // _wsClient.loop() (it's called from the comms-loop drain and from
        // ws_getchar's pump) so that any ack-waiting send_line() the parser
        // triggers — e.g. show_state()'s $G/$I/$A burst on the first status
        // report — can self-service instead of deadlocking inside loop().
        // The ring decouples receive (here, inside loop) from parse (there,
        // outside loop), exactly like the UART driver's RX buffer.
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
        case WStype_BIN:
            _last_rx_byte_ms = millis();
            for (size_t i = 0; i < length; i++) {
                uint8_t c = payload[i];
                if (c == '\r') continue;  // strip CR
                rx_push(c);
            }
            update_rx_time();
            break;

        case WStype_ERROR:
            dbg_printf("WS: error len=%u\n", (unsigned)length);
            break;

        case WStype_PING:
        case WStype_PONG:
        case WStype_FRAGMENT_TEXT_START:  // a fragmented TEXT control msg —
                                          // never happens in practice (control
                                          // strings are tiny); drop like TEXT
        default:
            break;
    }
}

static void ws_client_init_once() {
    if (_ws_client_inited) return;
    _wsClient.onEvent(onWsEvent);
    // Library will auto-retry the connect every WS_RECONNECT_MS while
    // disconnected — replaces the manual _tcp_next_try_ms loop from the
    // old Telnet code.
    _wsClient.setReconnectInterval(WS_RECONNECT_MS);
    // Heartbeat: WebSocket-level PING/PONG.  If FluidNC's TCP stack wedges
    // (peer crashed without FIN, planner jammed, etc.) the library drops
    // the connection in roughly WS_PING_INTERVAL_MS + WS_PONG_MISSES *
    // WS_PONG_TIMEOUT_MS = ~16 s, which is much faster than the kernel
    // KEEPALIVE timeout we used to wait for.
    _wsClient.enableHeartbeat(WS_PING_INTERVAL_MS, WS_PONG_TIMEOUT_MS, WS_PONG_MISSES);
    _ws_client_inited = true;
}

static void ws_disconnect_socket() {
    if (_ws_begin_called) {
        _wsClient.disconnect();
        _ws_begin_called = false;
    }
    _ws_connected        = false;
    pending_nowait_sends = 0;
}

// Start (or restart) the WebSocket client targeting FluidNC at `host:81/`.
// Returns true on "begin scheduled" — actual connection completes
// asynchronously and surfaces via the WStype_CONNECTED event.
static bool ws_socket_begin(const char* host) {
    ws_client_init_once();
    IPAddress ip;
    if (!ip.fromString(host)) {
        dbg_printf("WS: bad IP literal: %s\n", host);
        return false;
    }
    dbg_printf("WS: connecting to ws://%s:%d%s\n", host, FLUIDNC_WS_PORT, FLUIDNC_WS_PATH);

    // begin() schedules the connect.  The library's loop() drives the
    // socket forward; we'll get WStype_CONNECTED when the handshake
    // completes or it'll keep retrying on reconnect interval.
    _wsClient.begin(host, FLUIDNC_WS_PORT, FLUIDNC_WS_PATH);
    _ws_begin_called = true;
    _last_rx_byte_ms = millis();
    return true;
}

static void ws_socket_target(const char* host) {
    strncpy(_fluidnc_remote_ip, host, sizeof(_fluidnc_remote_ip) - 1);
    _fluidnc_remote_ip[sizeof(_fluidnc_remote_ip) - 1] = '\0';
    ws_socket_begin(host);
}

static const char* wifi_status_name(wl_status_t status) {
    switch (status) {
        case WL_NO_SHIELD:       return "WL_NO_SHIELD";
        case WL_IDLE_STATUS:     return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL:   return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED:  return "WL_SCAN_COMPLETED";
        case WL_CONNECTED:       return "WL_CONNECTED";
        case WL_CONNECT_FAILED:  return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED:    return "WL_DISCONNECTED";
        default:                 return "WL_UNKNOWN";
    }
}

// ─── Ring-buffer helpers ──────────────────────────────────────────────────────
//
// Incoming WebSocket bytes are pushed here by onWsEvent and pulled out by
// ws_getchar() ← fnc_getchar() on the Core 0 comms task / ack-wait spins.
// CRITICAL: collect() is NOT called from inside onWsEvent / _wsClient.loop().
// If it were, any ack-waiting send_line() reached from the parser (eg. the
// $G/$I/$A burst show_state() fires on the first status report) would spin
// inside loop() — unable to send the command (tx_drain runs after loop()
// returns) or receive its "ok" (needs the next loop()) — and deadlock for the
// full ack timeout.  By buffering into the ring and letting the comms-loop
// drain (and ws_getchar's pump, below) run collect() OUTSIDE loop(), those
// spins can self-service exactly like the UART driver does.

static inline void rx_push(uint8_t c) {
    portENTER_CRITICAL(&_rx_mux);
    int next = (_rx_head + 1) % RX_BUF_SIZE;
    if (next != _rx_tail) {
        _rx_buf[_rx_head] = c;
        _rx_head          = next;
    }
    portEXIT_CRITICAL(&_rx_mux);
    if (rtcLastBootStage < 11) rtcLastBootStage = 11;  // first byte from FluidNC
}

static inline int rx_pop() {
    int result = -1;
    portENTER_CRITICAL(&_rx_mux);
    if (_rx_head != _rx_tail) {
        result   = (unsigned char)_rx_buf[_rx_tail];
        _rx_tail = (_rx_tail + 1) % RX_BUF_SIZE;
    }
    portEXIT_CRITICAL(&_rx_mux);
    return result;
}

// ─── TX ring-buffer helpers ────────────────────────────────────────────────────
// tx_ring_push() is multi-producer (called from ws_putchar on any core);
// tx_ring_pop() is single-consumer (Core 0 tx_drain only).  The spinlock
// makes the multi-producer case safe.

static inline void tx_ring_push(uint8_t c) {
    portENTER_CRITICAL(&_tx_mux);
    int next = (_tx_ring_head + 1) % TX_RING_SIZE;
    if (next != _tx_ring_tail) {
        _tx_ring[_tx_ring_head] = c;
        _tx_ring_head           = next;
    }
    // If the ring is full we drop the byte.  At 1 KB this only happens if
    // Core 0 has been blocked from draining for a long time (eg. mid-
    // reconnect), in which case dropping is the right call — those bytes
    // would be stale commands by the time the link comes back anyway.
    portEXIT_CRITICAL(&_tx_mux);
}

static inline int tx_ring_pop() {
    int result = -1;
    portENTER_CRITICAL(&_tx_mux);
    if (_tx_ring_head != _tx_ring_tail) {
        result        = (unsigned char)_tx_ring[_tx_ring_tail];
        _tx_ring_tail = (_tx_ring_tail + 1) % TX_RING_SIZE;
    }
    portEXIT_CRITICAL(&_tx_mux);
    return result;
}

// ─── WebSocket transport primitives ───────────────────────────────────────────
// ws_putchar / ws_getchar are called by fnc_putchar / fnc_getchar in
// SystemArduino.cpp when in WiFi mode, FROM EITHER CORE.

// ws_putchar (ANY CORE): the ONLY thing it does is enqueue the byte into the
// TX ring.  It must NOT touch _wsClient — that object is owned exclusively by
// Core 0 (see the big comment on the TX ring declaration).  This is the
// pivotal change that fixed "status stops / jog dead the moment you turn the
// wheel": jog commands originate on Core 1, and calling _wsClient.sendBIN()
// there while Core 0 ran _wsClient.loop() was corrupting the TCP frame stream.
void ws_putchar(uint8_t c) {
    // Drop UART XON/XOFF flow-control bytes (irrelevant over WebSocket — the
    // library and TCP layer handle backpressure for us).
    if (c == 0x11 || c == 0x13) return;
    tx_ring_push(c);
}

// ws_getchar (Core 0): pop one byte from the RX ring.  If the ring is empty,
// PUMP the transport once — flush queued TX (tx_drain) and pull any pending
// WebSocket frames (_wsClient.loop()) — then retry the pop.
//
// This is what lets a synchronous ack-wait survive over WebSocket.  When
// fnc_send_line() spins on _ackwait it calls fnc_poll() → fnc_getchar() →
// ws_getchar() in a tight loop; pumping here means each spin iteration both
// SENDS the pending command and RECEIVES its "ok", so _ackwait clears in a
// round-trip or two instead of timing out.  This mirrors how the UART driver
// services fnc_getchar()/fnc_putchar() directly, independent of the comms
// loop — and it's the mechanism that unblocks show_state()'s $G/$I/$A burst.
//
// Reentrancy: collect() never runs inside _wsClient.loop() (onWsEvent only
// fills the ring), so ws_getchar() is never reached from within loop().  The
// _pumping guard is belt-and-suspenders against any future nesting.
int ws_getchar() {
    int c = rx_pop();
    if (c >= 0) return c;

    static bool _pumping = false;
    if (xPortGetCoreID() == 0 && _ws_connected && !_pumping) {
        _pumping = true;
        tx_drain();          // ship any queued command bytes
        _wsClient.loop();    // pull any pending frames into the ring (onWsEvent)
        _pumping = false;
        c = rx_pop();
    }
    return c;
}

// tx_drain (CORE 0 ONLY): pull every queued byte out of the TX ring and feed
// it through the same framing logic the old ws_putchar used — realtime bytes
// go out as their own 1-byte BIN frame immediately; everything else is
// accumulated into _tx_buf until '\n' (or buffer-full) and sent as one BIN
// frame.  Called from wifi_poll() right after _wsClient.loop(), so all
// _wsClient.send*() calls happen on the same task as loop().
//
// Why BIN for line commands too: FluidNC's WSChannels::handleEvent routes
// TEXT and BIN identically (both call wsChannel->push(data, len)), and BIN
// sidesteps AsyncWebSocket's strict UTF-8 validation of TEXT frames — a
// single invalid high byte in a TEXT payload would make FluidNC silently
// close the channel.  The trailing '\n' is preserved; FluidNC's pollLine()
// needs it to terminate the command.
static void tx_drain() {
    if (!_ws_connected) {
        // Not connected — discard anything queued so a backlog of stale
        // commands can't flush out the instant we reconnect.  _tx_len is
        // reset on the connect edge in onWsEvent.
        portENTER_CRITICAL(&_tx_mux);
        _tx_ring_tail = _tx_ring_head;
        portEXIT_CRITICAL(&_tx_mux);
        return;
    }

    int c;
    int budget = TX_RING_SIZE;   // bound the work per poll cycle
    while (budget-- > 0 && (c = tx_ring_pop()) >= 0) {
        uint8_t b = (uint8_t)c;

        // Single-byte realtime commands: Ctrl-X, 0x80–0x9F, 0xB0–0xB3.
        // 0xB2 is the JSON-channel ACK GrblParserC emits after each parsed
        // JSON line — FluidNC pauses JSON output until it arrives, so it
        // must never be dropped or delayed behind a partial line.
        if (b == 0x18 || (b >= 0x80 && b <= 0x9F) || (b >= 0xB0 && b <= 0xB3)) {
            _wsClient.sendBIN(&b, 1);
            continue;
        }
        // ASCII realtime commands ('?', '!', '~').  These MUST be extracted even
        // mid-line: fnc_realtime() pushes them to the ring WITHOUT txLineLock (so
        // the 200 ms '?' status poll can land between the bytes of a line command
        // being pushed from the other core).  The old `_tx_len == 0` guard let a
        // mid-line '?' get buffered INTO the command — "$H" + "?" → "$H?\n" — a
        // garbled line FluidNC rejects or, worse, reads as motion and ALARMS.
        // GRBL/FluidNC treat ?,!,~ as realtime regardless of stream position, so
        // pulling them out here (leaving the buffered line intact) is correct and
        // is what lets the line reassemble cleanly.
        if (b == '?' || b == '!' || b == '~') {
            _wsClient.sendBIN(&b, 1);
            continue;
        }
        // All other bytes: buffer until '\n', then send as one BIN frame.
        _tx_buf[_tx_len++] = b;
        if (b == '\n' || _tx_len >= TX_BUF_SIZE - 1) {
            _wsClient.sendBIN(_tx_buf, _tx_len);
            _tx_len = 0;
        }
    }
}

// ─── Captive portal HTML ──────────────────────────────────────────────────────

static const char SETUP_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FluidDial WiFi Setup</title>
<style>
  *{box-sizing:border-box}
  body{font-family:sans-serif;max-width:420px;margin:32px auto;padding:16px;
       background:#1a1a1a;color:#eee}
  h2{color:#4CAF50;margin-bottom:4px}
  p.sub{color:#aaa;font-size:14px;margin-bottom:20px}
  label{display:block;margin:14px 0 4px;color:#ccc;font-size:14px}
  input,select{width:100%;padding:10px;border-radius:6px;border:1px solid #555;
        background:#2a2a2a;color:#eee;font-size:16px}
  input:focus,select:focus{outline:none;border-color:#4CAF50}
  .row{display:flex;gap:8px}
  .row input{flex:1}
  .scan-btn{padding:10px 14px;background:#2a2a2a;color:#4CAF50;border:1px solid #4CAF50;
            border-radius:6px;font-size:14px;cursor:pointer;white-space:nowrap;font-weight:bold}
  .scan-btn:hover{background:#1e3d1e}
  .scan-btn:disabled{color:#555;border-color:#555;cursor:default}
  #netList{display:none;margin-top:6px}
  button[type=submit]{margin-top:24px;width:100%;padding:14px;background:#4CAF50;color:#fff;
         border:none;border-radius:6px;font-size:18px;cursor:pointer;font-weight:bold}
  button[type=submit]:hover{background:#45a049}
  .note{margin-top:16px;font-size:13px;color:#888;text-align:center}
  .scan-status{font-size:13px;color:#aaa;margin-top:4px;min-height:18px}
  .eye-btn{padding:10px 12px;background:#2a2a2a;color:#aaa;border:1px solid #555;
           border-radius:6px;font-size:18px;cursor:pointer;line-height:1}
  .eye-btn:hover{color:#4CAF50;border-color:#4CAF50}
</style>
</head>
<body>
<h2>FluidDial WiFi Setup</h2>
<p class="sub">Connect the FluidDial pendant to your WiFi network and FluidNC machine.</p>
<form method="POST" action="/save">
  <label>WiFi Network Name (SSID)</label>
  <div class="row">
    <input type="text" name="ssid" id="ssid" placeholder="YourNetworkName" autocomplete="off" required value="%SSID_VAL%">
    <button type="button" class="scan-btn" id="scanBtn" onclick="doScan()">Scan</button>
  </div>
  <select id="netList" onchange="pickNet(this)">
    <option value="">-- select a network --</option>
  </select>
  <div id="scanStatus" class="scan-status"></div>
  <label>WiFi Password</label>
  <div class="row">
    <input type="text" name="pass" id="pass" placeholder="Leave blank for open networks">
    <button type="button" class="eye-btn" id="eyeBtn" onclick="togglePass()">&#x1F441;</button>
  </div>
  <label>FluidNC Address (IP or hostname)</label>
  <input type="text" name="ip" placeholder="192.168.1.100 or fluidnc.local"
         autocomplete="off" required value="%IP_VAL%">
  <button type="submit">Save &amp; Connect</button>
</form>
<p class="note">The pendant will restart and connect automatically.</p>
<script>
function doScan(){
  var btn=document.getElementById('scanBtn');
  var lst=document.getElementById('netList');
  var st=document.getElementById('scanStatus');
  btn.disabled=true; btn.textContent='Scanning...';
  st.textContent='Scanning for networks, please wait...';
  lst.style.display='none';
  fetch('/scan').then(function(r){return r.json();}).then(function(nets){
    lst.innerHTML='<option value="">-- select a network --</option>';
    nets.sort(function(a,b){return b.rssi-a.rssi;});
    nets.forEach(function(n){
      var o=document.createElement('option');
      o.value=n.ssid;
      o.textContent=n.ssid+(n.secure?' [secured]':'')+'  ('+n.rssi+' dBm)';
      lst.appendChild(o);
    });
    lst.style.display='block';
    st.textContent=nets.length+' network'+(nets.length!==1?'s':'')+' found.';
    btn.disabled=false; btn.textContent='Scan';
  }).catch(function(){
    st.textContent='Scan failed. Try again.';
    btn.disabled=false; btn.textContent='Scan';
  });
}
function pickNet(sel){
  if(sel.value) document.getElementById('ssid').value=sel.value;
}
function togglePass(){
  var p=document.getElementById('pass');
  var b=document.getElementById('eyeBtn');
  if(p.type==='text'){p.type='password';b.style.color='#555';}
  else{p.type='text';b.style.color='#aaa';}
}
</script>
</body>
</html>
)HTML";

static const char SAVED_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FluidDial – Saved</title>
<style>
  body{font-family:sans-serif;max-width:420px;margin:80px auto;padding:16px;
       background:#1a1a1a;color:#eee;text-align:center}
  h2{color:#4CAF50}p{color:#aaa}
</style></head>
<body>
<h2>Settings Saved!</h2>
<p>FluidDial is restarting and will connect to your network shortly.</p>
</body>
</html>
)HTML";

// ─── HTTP handlers (captive portal) ──────────────────────────────────────────

static String htmlEscape(const String& input) {
    String escaped;
    escaped.reserve(input.length());
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        switch (c) {
            case '&':  escaped += F("&amp;");  break;
            case '<':  escaped += F("&lt;");   break;
            case '>':  escaped += F("&gt;");   break;
            case '"':  escaped += F("&quot;"); break;
            case '\'': escaped += F("&#39;");  break;
            default:   escaped += c;           break;
        }
    }
    return escaped;
}

static void handleRoot() {
    WiFiConfig cfg = wifi_load_config();
    String page = SETUP_HTML;
    page.replace("%SSID_VAL%", cfg.valid ? htmlEscape(String(cfg.ssid)) : "");
    page.replace("%IP_VAL%",   cfg.valid ? htmlEscape(String(cfg.fluidnc_ip)) : "");
    httpServer.send(200, "text/html", page);
}

static void handleSave() {
    String ssid = httpServer.arg("ssid");
    String pass = httpServer.arg("pass");
    String ip   = httpServer.arg("ip");

    // Trim SSID and IP — leading/trailing whitespace there is never meaningful
    // and is almost always an accidental copy-paste artefact.
    //
    // DO NOT trim the password.  WPA passphrases are arbitrary byte sequences
    // and may legitimately start or end with spaces.  More importantly, when
    // users copy-paste passwords from password managers a trailing space is
    // extremely common — silently stripping it produces "Check password"
    // errors with no clue as to why the saved value differs from what the
    // user thinks they typed.
    ssid.trim();
    ip.trim();

    // Sanitise IP / hostname.
    String cleanIp;
    for (int i = 0; i < (int)ip.length(); i++) {
        char c = ip[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || c == '.' || c == '-') {
            cleanIp += c;
        }
    }
    ip = cleanIp;

    if (ssid.length() == 0 || ip.length() == 0) {
        httpServer.send(400, "text/plain", "SSID and IP are required");
        return;
    }

    // Log lengths only (never the password itself).  Mismatched lengths
    // between what the user typed and what got saved are diagnosable from
    // these without leaking secrets.
    dbg_printf("WiFi setup: ssid_len=%d pass_len=%d ip=%s\n",
               ssid.length(), pass.length(), ip.c_str());

    wifi_save_config(ssid.c_str(), pass.c_str(), ip.c_str());
    httpServer.send(200, "text/html", SAVED_HTML);
    delay(2000);
    ESP.restart();
}

static void handleScan() {
    int n = WiFi.scanNetworks(false, false);
    String json = "[";
    for (int i = 0; i < n && i < 32; i++) {
        if (i > 0) json += ",";
        String ssid = WiFi.SSID(i);
        String safe;
        safe.reserve(ssid.length());
        for (int j = 0; j < (int)ssid.length(); j++) {
            char c = ssid[j];
            if (c == '\\' || c == '"') safe += '\\';
            if (c >= 0x20) safe += c;
        }
        json += "{\"ssid\":\""; json += safe;
        json += "\",\"rssi\":"; json += String(WiFi.RSSI(i));
        json += ",\"secure\":";
        json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false";
        json += "}";
    }
    json += "]";
    WiFi.scanDelete();
    httpServer.sendHeader("Cache-Control", "no-cache");
    httpServer.send(200, "application/json", json);
}

static void handleNotFound() {
    httpServer.sendHeader("Location", "http://192.168.4.1/", true);
    httpServer.send(302, "text/plain", "");
}

// ─── Public API ───────────────────────────────────────────────────────────────

void wifi_save_config(const char* ssid, const char* password, const char* ip) {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.putString("ip",   ip);
    prefs.end();
    dbg_printf("WiFi config saved: ssid=%s ip=%s\n", ssid, ip);
}

WiFiConfig wifi_load_config() {
    WiFiConfig cfg = {};

#if HARDCODE_TEST_WIFI
    strncpy(cfg.ssid,       TEST_WIFI_SSID,     sizeof(cfg.ssid)       - 1);
    strncpy(cfg.password,   TEST_WIFI_PASS,     sizeof(cfg.password)   - 1);
    strncpy(cfg.fluidnc_ip, TEST_FLUIDNC_IP,    sizeof(cfg.fluidnc_ip) - 1);
    cfg.valid = true;
    return cfg;
#endif

    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    String ssid = prefs.isKey("ssid") ? prefs.getString("ssid", "") : "";
    String pass = prefs.isKey("pass") ? prefs.getString("pass", "") : "";
    String ip   = prefs.isKey("ip")   ? prefs.getString("ip",   "") : "";
    prefs.end();

    if (ssid.length() > 0 && ip.length() > 0) {
        dbg_printf("NVS: ssid='%s' ip='%s'\n", ssid.c_str(), ip.c_str());
        strncpy(cfg.ssid,       ssid.c_str(), sizeof(cfg.ssid)       - 1);
        strncpy(cfg.password,   pass.c_str(), sizeof(cfg.password)   - 1);
        strncpy(cfg.fluidnc_ip, ip.c_str(),   sizeof(cfg.fluidnc_ip) - 1);
        cfg.valid = true;
    }
    return cfg;
}

void wifi_start_ap_setup() {
    _ap_mode            = true;
    ws_disconnect_socket();
    _wifi_stack_started = true;

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP_STA);  // STA kept active so the portal's "Scan" button works

    // Configure the soft-AP IP/subnet BEFORE softAP() brings the AP up.  On the
    // ESP32 Arduino core, calling softAPConfig() AFTER softAP() tears down and
    // restarts the on-AP DHCP server and can leave it not serving leases — the
    // client then self-assigns a 169.254.x.x (APIPA) address and can never reach
    // 192.168.4.1, so the captive portal appears dead.  Configuring first lets
    // softAP() start the DHCP server correctly on the chosen subnet.
    IPAddress apIP(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    if (!WiFi.softAPConfig(apIP, apIP, subnet)) {
        dbg_println("WiFi: softAPConfig() failed");
    }
    WiFi.softAP(WIFI_AP_SSID, (strlen(WIFI_AP_PASS) ? WIFI_AP_PASS : nullptr));
    delay(100);   // let the AP interface + DHCP server settle before clients join

    dnsServer.start(53, "*", apIP);

    httpServer.on("/",                         HTTP_GET,  handleRoot);
    httpServer.on("/generate_204",             HTTP_GET,  handleRoot);
    httpServer.on("/hotspot-detect.html",      HTTP_GET,  handleRoot);
    httpServer.on("/ncsi.txt",                 HTTP_GET,  handleRoot);
    httpServer.on("/fwlink",                   HTTP_GET,  handleRoot);
    httpServer.on("/scan",                     HTTP_GET,  handleScan);
    httpServer.on("/save",                     HTTP_POST, handleSave);
    httpServer.onNotFound(handleNotFound);
    httpServer.begin();

    dbg_printf("AP started — SSID: %s  IP: %s\n",
               WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

void wifi_stop_ap_and_restart() {
    httpServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    _ap_mode = false;
    delay(300);
    ESP.restart();
}

void wifi_stop_ap() {
    httpServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    _ap_mode            = false;
    _wifi_stack_started = false;
    wifi_init(false);  // re-enter STA mode without triggering AP again
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}
bool websocket_is_connected() {
    return _ws_connected;
}
// Suspend WebSocket servicing so a plain-HTTP fetch can have FluidNC's port 80
// to itself.  Sets a request flag and blocks (bounded) until Core 0's wifi_poll()
// has actually closed the socket, so the caller knows the port is free before it
// dials.  Safe to call from any task: only wifi_poll() (Core 0) ever touches the
// socket.  ALWAYS pair with wifi_ws_resume().
void wifi_ws_suspend() {
    if (!_ws_begin_called && !_ws_suspended) { _ws_suspended = false; }
    _ws_suspend_req = true;
    uint32_t t0 = millis();
    while (!_ws_suspended && (millis() - t0) < 1500) delay(5);
}

void wifi_ws_resume() {
    _ws_suspend_req = false;   // wifi_poll() reopens the socket on its next pass
}

// Plain HTTP GET of a file from FluidNC's filesystem, streamed chunk-by-chunk
// to `on_chunk`.  Used to fetch the macros file (preferences.json / macrocfg.json)
// the SAME way FluidNC's own WebUI does — over a SEPARATE TCP connection on
// port 80, NOT the WebSocket.  This avoids the WebSocket flow-control / heartbeat
// instability that was truncating the large $File/SendJSON reply mid-stream.
//
// Runs on the CALLER's task (we call it from a short-lived macros-fetch task,
// not the comms or UI loop), so the blocking read doesn't stall anything.
// Returns the HTTP status code (200 = OK) or a negative value on setup failure.
// Raw-socket implementation (NOT Arduino's HTTPClient).  HTTPClient::GET() was
// ignoring the connect timeout and hanging indefinitely against FluidNC —
// surfacing as "HTTP 0 / 0 bytes" (the call never returned, so no status code
// was ever recorded).  A hand-rolled GET gives a HARD-bounded connect and
// explicit, distinguishable error codes:
//   -1  WiFi not connected
//   -2  remote IP not a usable dotted-decimal address
//   -20 TCP connect failed/timed out (FluidNC refused the connection)
//   -21 timed out waiting for the status line
//   -22 timed out reading headers
//   >0  the real HTTP status code (200, 404, …)
// "Connection: close" makes FluidNC end the body cleanly so the read loop
// terminates on EOF instead of stalling on a kept-alive socket.
int wifi_http_get(const char* path,
                  std::function<void(const uint8_t*, size_t)> on_chunk,
                  int timeout_ms) {
    if (WiFi.status() != WL_CONNECTED) return -1;

    const char* host = _fluidnc_remote_ip[0] ? _fluidnc_remote_ip
                                             : _active_cfg.fluidnc_ip;
    IPAddress ip;
    if (!ip.fromString(host)) return -2;   // need a resolved dotted IP

    WiFiClient client;
    if (!client.connect(ip, FLUIDNC_WS_PORT, timeout_ms)) {
        client.stop();
        return -20;                        // hard-bounded: never hangs here
    }

    // Send a minimal HTTP/1.1 request.
    client.print(String("GET ") + path + " HTTP/1.1\r\n"
                 "Host: " + host + "\r\n"
                 "Connection: close\r\n"
                 "\r\n");

    uint32_t last = millis();
    auto read_line = [&](String& out) -> bool {   // true = got a full line
        out = "";
        for (;;) {
            int a = client.available();
            if (a > 0) {
                char c = (char)client.read();
                last = millis();
                if (c == '\n') return true;
                if (c != '\r' && out.length() < 256) out += c;
            } else if (!client.connected() && client.available() == 0) {
                return out.length() > 0;        // EOF
            } else if ((millis() - last) > (uint32_t)timeout_ms) {
                return false;                    // stalled
            } else {
                delay(2);
            }
        }
    };

    // Status line: "HTTP/1.1 200 OK"
    String line;
    if (!read_line(line)) { client.stop(); return -21; }
    int statusCode = 0;
    {
        int sp = line.indexOf(' ');
        if (sp > 0) statusCode = line.substring(sp + 1, sp + 4).toInt();
    }

    // Skip headers until the blank separator line, noting Content-Length so we
    // can stop reading the body exactly when it's complete (rather than waiting
    // on EOF/stall — robust even if the server ignores "Connection: close").
    long contentLength = -1;   // -1 = unknown
    bool headersDone = false;
    while (!headersDone) {
        if (!read_line(line)) { client.stop(); return -22; }
        if (line.length() == 0) { headersDone = true; break; }
        String lower = line; lower.toLowerCase();
        if (lower.startsWith("content-length:")) {
            contentLength = line.substring(line.indexOf(':') + 1).toInt();
        }
    }

    // Body: read until Content-Length satisfied, EOF (server closes), or a stall.
    if (statusCode == 200) {
        uint8_t buf[256];
        long received = 0;
        for (;;) {
            if (contentLength >= 0 && received >= contentLength) break;  // complete
            int a = client.available();
            if (a > 0) {
                int n = client.read(buf, a > (int)sizeof(buf) ? sizeof(buf) : a);
                if (n > 0) { on_chunk(buf, (size_t)n); received += n; last = millis(); }
            } else if (!client.connected()) {
                break;                           // clean EOF (Connection: close)
            } else if ((millis() - last) > (uint32_t)timeout_ms) {
                break;                           // stalled — take what we got
            } else {
                delay(2);
            }
        }
    }

    client.stop();
    return statusCode;
}

void wifi_graceful_disconnect() {
    // Called on Core 0 from the long-press handler before power-off, so it's
    // safe to touch _wsClient here (we're not inside _wsClient.loop()).
    _shutting_down = true;            // wifi_poll() will now skip all WS service
    if (_ws_begin_called) {
        _wsClient.disconnect();       // emits a WebSocket CLOSE frame to FluidNC
        _ws_begin_called = false;
    }
    _ws_connected = false;
    delay(50);                        // let the CLOSE frame flush before sleep
    dbg_println("WS: graceful disconnect (power-off)");
}
bool wifi_in_ap_mode() { return _ap_mode; }
const char* wifi_ap_ssid() { return WIFI_AP_SSID; }

const char* wifi_status_str() {
    if (_ap_mode)              return "AP Setup Mode";
    if (!wifi_is_connected())  return "Connecting to WiFi";
    if (!_ws_connected)        return "Connecting to FluidNC";
    return "Connected";
}
const bool wifi_not_ready() {
    return (!wifi_is_connected() || !_ws_connected);
}
const char* wifi_last_error() { return _wifi_error_msg; }

WiFiConfig wifi_active_config() { return _active_cfg; }

int wifi_signal_bars() {
    if (!wifi_is_connected()) return 0;
    int rssi = WiFi.RSSI();
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

// ─── WiFi event handler ───────────────────────────────────────────────────────

static void onWiFiDisconnect(WiFiEvent_t event, WiFiEventInfo_t info) {
    _wifi_disconnect_reason = info.wifi_sta_disconnected.reason;
}

void wifi_init(bool auto_ap) {
    // Called by comms_init() only when the user selected WiFi as the
    // transport.  No NVS-based first-boot guard: a battery pendant with no
    // saved credentials simply starts the AP captive portal so the user can
    // configure WiFi from a phone browser (192.168.4.1).
    WiFiConfig cfg = wifi_load_config();
    if (!cfg.valid) {
        if (auto_ap) {
            dbg_println("No WiFi credentials — starting AP setup portal");
            wifi_start_ap_setup();
        }
        return;
    }

    _wifi_stack_started      = true;
    _active_cfg              = cfg;
    ws_disconnect_socket();
    _fluidnc_remote_ip[0]    = '\0';
    _wifi_was_connected      = false;
    _last_wifi_status        = WL_IDLE_STATUS;
    _wifi_error_msg          = nullptr;
    _wifi_retry_at           = 0;
    _wifi_disconnect_reason  = 0;
    _wifi_ever_connected     = false;
    _wifi_connect_start_ms   = 0;
    _handshake_timeout_count = 0;
    _dns_retry_at            = 0;

    static bool _event_registered = false;
    if (!_event_registered) {
        WiFi.onEvent(onWiFiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        _event_registered = true;
    }

    // ── WiFi stack bring-up ─────────────────────────────────────────────────
    // Sequence and timings tuned for the ESP32 (Arduino-ESP32 3.x) on the
    // CYD's 2.4 GHz radio.  Notes on each step:
    //
    //   persistent(false):
    //     We manage credentials ourselves in NVS — don't let Arduino write a
    //     parallel copy that might disagree after a captive-portal reconfigure.
    //
    //   disconnect(true) + 500 ms settle:
    //     Tear down any prior STA association (matters on AP→STA transitions
    //     after captive-portal save).  500 ms is conservative — the previous
    //     100 ms wasn't always enough; we saw intermittent auth failures
    //     because the next .begin() was racing the disconnect cleanup.
    //
    //   mode(WIFI_STA):
    //     Station mode only.  Explicit so the radio doesn't linger in AP_STA
    //     after a portal-save restart.
    //
    //   setSleep(false):
    //     Keep the radio active.  Power saving introduces multi-hundred-ms
    //     latency spikes that show up as jog/probe stutter and WebSocket
    //     PING timeouts.
    //
    //   No setTxPower():
    //     Previously we forced WIFI_POWER_19_5dBm (max).  On the CYD's
    //     USB-powered rail this could brown out the 3V3 regulator the
    //     moment WiFi associates and the radio kicks up — the chip would
    //     reset cleanly (no panic, no backtrace) a few seconds after the
    //     signal-strength icon appeared.  Default TX power is roughly
    //     17 dBm which is plenty for indoor use and stays inside the
    //     CYD's power budget.
    //
    //   setAutoReconnect(false):
    //     wifi_poll() drives reconnection explicitly with its own backoff.
    //     Arduino's auto-reconnect fights our state machine.
    //
    // Explicit OFF → STA transition.  Some routers MAC-rate-limit after a
    // recent failed authentication; a clean WiFi-OFF then back ON forces a
    // fresh radio init so the next .begin() starts from a known state.
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_OFF);
    delay(500);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);
    WiFi.begin(cfg.ssid, cfg.password[0] ? cfg.password : nullptr);
    _wifi_connect_start_ms = millis();
    rtcLastBootStage = 6;     // stage 6: WiFi.begin returned (async connect in progress)

    dbg_printf("WiFi: connecting to %s (pass_len=%d)  FluidNC: %s\n",
               cfg.ssid, (int)strlen(cfg.password), cfg.fluidnc_ip);
}

void wifi_poll() {
    if (!_wifi_stack_started) return;
    if (_shutting_down) return;   // power-off in progress: don't reopen the WS

    // Resumable WS suspend — used while the macros HTTP fetch runs.  FluidNC
    // serves HTTP and the WebSocket on the SAME port 80, and won't reliably
    // complete a second connection while the pendant's WS is streaming status
    // (the GET hangs → "HTTP 0 / 0 bytes").  Closing the WS for the duration of
    // the fetch frees the port; we reopen it the moment the request clears.
    if (_ws_suspend_req) {
        if (!_ws_suspended) {
            if (_ws_begin_called) {
                _wsClient.disconnect();
                _ws_begin_called = false;
            }
            _ws_connected = false;
            _ws_suspended = true;
        }
        return;   // don't service or reconnect the WS while suspended
    }
    if (_ws_suspended) {
        // Request cleared → reopen the socket and fall through to normal service.
        _ws_suspended = false;
        if (_fluidnc_remote_ip[0]) {
            ws_socket_begin(_fluidnc_remote_ip);
            _last_rx_byte_ms = millis();
        }
    }

    if (_ap_mode) {
        dnsServer.processNextRequest();
        httpServer.handleClient();
        return;
    }

    wl_status_t wifi_status = WiFi.status();
    if (wifi_status != _last_wifi_status) {
        dbg_printf("WiFi status: %s (%d)\n", wifi_status_name(wifi_status), wifi_status);
        _last_wifi_status = wifi_status;
        if (wifi_status == WL_CONNECTED) {
            _wifi_ever_connected     = true;
            _wifi_error_msg          = nullptr;
            _wifi_retry_at           = 0;
            _wifi_connect_start_ms   = 0;
            _handshake_timeout_count = 0;
            _wifi_disconnect_reason  = 0;
            // NOTE: do NOT enable Arduino's WiFi auto-reconnect here.
            // wifi_init() explicitly disabled it because our own state
            // machine in wifi_poll() drives reconnects with proper backoff;
            // having both fighting causes priority-inversion bursts on
            // Core 0 that have been seen to starve the loop-task watchdog.
        }
    }

    // Generic connection timeout — also a retry trigger.
    //
    // WiFi.begin() can occasionally sit in WL_IDLE_STATUS for an extended
    // period without ever raising a disconnect event (eg. when the AP
    // hears the probe request but the auth/assoc handshake never starts
    // because the AP is itself busy or rate-limiting silently).  In that
    // case our disconnect-reason handler above never fires, so we'd be
    // stuck "connecting…" forever.  Treat the 8 s timeout as a soft
    // disconnect and let the retry path do a full radio reset.
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;
    if (!_wifi_ever_connected && !_wifi_error_msg && _wifi_connect_start_ms &&
        (millis() - _wifi_connect_start_ms) > WIFI_CONNECT_TIMEOUT_MS) {
        _wifi_error_msg = "Cannot connect";
        _handshake_timeout_count++;
        if (_handshake_timeout_count < 10) {
            uint32_t backoff = 2000UL;
            for (uint8_t i = 1; i < _handshake_timeout_count && backoff < 30000UL; ++i) {
                backoff <<= 1;
            }
            if (backoff > 30000UL) backoff = 30000UL;
            if (!_wifi_retry_at) _wifi_retry_at = millis() + backoff;
        }
        // Stop the connect-start clock so we don't re-fire this every tick.
        _wifi_connect_start_ms = 0;
    }

    // Decode disconnect reason set by WiFi event.
    static const char* const MSG_CHECK_PASS = "Check password";
    static const char* const MSG_NOT_FOUND  = "Network not found";

    if (_wifi_disconnect_reason) {
        uint8_t reason          = _wifi_disconnect_reason;
        _wifi_disconnect_reason = 0;
        dbg_printf("WiFi disconnect reason: %d\n", reason);

        if (!_wifi_ever_connected) {
            const char* new_msg    = nullptr;
            bool        stop_driver = false;
            bool        allow_retry = false;

            if (reason == WIFI_REASON_AUTH_FAIL   ||
                reason == WIFI_REASON_AUTH_EXPIRE  ||
                reason == WIFI_REASON_MIC_FAILURE) {
                // AUTH_FAIL is NOT always wrong-password.  Routers commonly
                // return this code when they're MAC-rate-limiting after a
                // recent failed/abrupt-disconnect attempt — and that
                // rate-limit can take 30–60 s to clear.  Treat exactly like
                // 4WAY_HANDSHAKE_TIMEOUT: retry up to 10 times with
                // exponential backoff before declaring the password bad.
                _handshake_timeout_count++;
                if (_handshake_timeout_count >= 10) {
                    new_msg     = MSG_CHECK_PASS;
                    stop_driver = true;
                } else {
                    allow_retry = true;
                    uint32_t backoff = 2000UL;
                    for (uint8_t i = 1; i < _handshake_timeout_count && backoff < 30000UL; ++i) {
                        backoff <<= 1;
                    }
                    if (backoff > 30000UL) backoff = 30000UL;
                    if (!_wifi_retry_at) _wifi_retry_at = millis() + backoff;
                }
            } else if (reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
                // 4-way handshake timeout often means the router is
                // MAC-rate-limiting us after a recent failed attempt
                // (common after a captive-portal save → reboot → connect
                // cycle, or after a previous crash that disconnected
                // abruptly).  The router can need 30–60 s to clear that
                // state.  We retry up to 10 times with exponential backoff
                // so the user doesn't see a misleading "Check password"
                // message when the password is actually fine.
                _handshake_timeout_count++;
                if (_handshake_timeout_count >= 10) {
                    new_msg     = MSG_CHECK_PASS;
                    stop_driver = true;
                } else {
                    allow_retry = true;
                    // 2 s, 4 s, 8 s, 16 s, then 30 s cap.  Total time to
                    // give up: 2+4+8+16+30+30+30+30+30 ≈ 3 minutes.
                    uint32_t backoff = 2000UL;
                    for (uint8_t i = 1; i < _handshake_timeout_count && backoff < 30000UL; ++i) {
                        backoff <<= 1;
                    }
                    if (backoff > 30000UL) backoff = 30000UL;
                    if (!_wifi_retry_at) _wifi_retry_at = millis() + backoff;
                }
            } else if (reason == WIFI_REASON_NO_AP_FOUND) {
                if (_wifi_error_msg != MSG_CHECK_PASS) {
                    new_msg     = MSG_NOT_FOUND;
                    stop_driver = true;
                    allow_retry = true;
                }
            }

            if (new_msg) {
                if (stop_driver) {
                    WiFi.setAutoReconnect(false);
                    WiFi.disconnect(false);
                }
                _wifi_error_msg = new_msg;
                if (allow_retry && !_wifi_retry_at) {
                    _wifi_retry_at = millis() + WIFI_RETRY_DELAY_MS;
                }
            }
        }
    }

    // Retry WiFi.begin() after any failure that allow_retry=true above.
    //
    // Critical: each retry does the full OFF→STA radio reset, not just
    // disconnect+begin.  Without the radio-off cycle, the ESP32 WiFi
    // driver can carry stuck state from the failed attempt into the next
    // one — leading to repeated AUTH_FAIL / 4WAY_HANDSHAKE_TIMEOUT even
    // when the router has already cleared its rate-limit window.
    if (_wifi_retry_at && millis() >= _wifi_retry_at) {
        _wifi_retry_at         = 0;
        _wifi_error_msg        = nullptr;
        _last_wifi_status      = WL_IDLE_STATUS;
        _wifi_connect_start_ms = millis();
        dbg_printf("WiFi: retry %u connecting to %s\n",
                   (unsigned)_handshake_timeout_count, _active_cfg.ssid);
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(true);
        delay(200);
        WiFi.mode(WIFI_OFF);
        delay(500);
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(_active_cfg.ssid, _active_cfg.password[0] ? _active_cfg.password : nullptr);
    }

    bool now_connected = (wifi_status == WL_CONNECTED);

    // WiFi just came up — open the WebSocket (or kick off async DNS first).
    //
    // We deliberately do NOT call MDNS.begin() to advertise the pendant as
    // "fluiddial.local".  That advertiser kept a background task alive and
    // showed up as a crash source a few seconds after WiFi associated.
    // Hostname *lookups* for FluidNC's "*.local" address still work — they
    // go through mdns_query_a() in dnsResolveTask, which is independent of
    // the local mDNS responder.  The pendant simply isn't discoverable by
    // name from other devices, which it never needed to be.
    if (now_connected && !_wifi_was_connected) {
        rtcLastBootStage = 7;     // stage 7: WL_CONNECTED detected
        dbg_printf("WiFi connected — IP: %s  free_heap=%u\n",
                   WiFi.localIP().toString().c_str(),
                   (unsigned)ESP.getFreeHeap());
        if (is_dotted_decimal(_active_cfg.fluidnc_ip)) {
            rtcLastBootStage = 8;     // stage 8: ws_socket_target about to be called
            ws_socket_target(_active_cfg.fluidnc_ip);
            // stage 9 is now set inside onWsEvent on WStype_CONNECTED.
        } else if (!_dns_resolving) {
            _dns_retry_at = 0;
            start_dns_resolve();
        }
    }
    // WiFi just dropped — tear down WebSocket.
    if (!now_connected && _wifi_was_connected) {
        ws_disconnect_socket();
        _dns_done = false;
        set_disconnected_state();
        dbg_println("WiFi lost");
    }
    _wifi_was_connected = now_connected;

    // Async DNS completion.
    if (_dns_done && !_ws_begin_called && now_connected) {
        bool ok = _dns_ok;
        _dns_done = false;
        if (ok) {
            dbg_printf("Hostname resolved: %s -> %s\n",
                       _active_cfg.fluidnc_ip, _dns_result_str);
            ws_socket_target(_dns_result_str);
        } else {
            dbg_printf("Hostname resolution failed: %s — retry in %d ms\n",
                       _active_cfg.fluidnc_ip, DNS_RETRY_DELAY_MS);
            _wifi_error_msg = "Host not found";
            _dns_retry_at   = millis() + DNS_RETRY_DELAY_MS;
        }
    }

    // DNS retry.
    if (_dns_retry_at && !_dns_resolving && !_ws_begin_called && now_connected
        && millis() >= _dns_retry_at) {
        _dns_retry_at   = 0;
        _wifi_error_msg = nullptr;
        dbg_printf("DNS retry: resolving %s\n", _active_cfg.fluidnc_ip);
        start_dns_resolve();
    }

    // Service the WebSocket.  Once the handshake is up the byte stream
    // "behaves just like serial" (per FluidNC's own Web API docs), so we
    // deliberately keep this loop minimal — the same way CommsUart.cpp
    // simply trusts the UART driver to ferry bytes.  The library's
    // loop() drives connect / reconnect / PING-PONG heartbeat / frame
    // parsing; frames land in onWsEvent which fills the RX ring.
    //
    // What we used to have here and why it's gone:
    //
    //   • A 15-second RX-silence staleness watchdog that ran in parallel
    //     with the library's own 10 s PING / 3 s PONG / 2-miss heartbeat
    //     (≈16 s detection).  Two layers of disconnect logic racing each
    //     other meant a marginal link could be torn down twice in quick
    //     succession, doubling the user-visible outage window.  The
    //     library's heartbeat is enough; if FluidNC ever wedges the
    //     PING/PONG mechanism catches it.
    //
    //   • An aggressive 3-second "≥2 pending sends + no RX = reconnect"
    //     watchdog.  This was supposed to catch "controller wedged"
    //     scenarios, but during normal homing it's the legitimate steady
    //     state — many jog/probe commands queue up while FluidNC is
    //     busy moving the machine.  The watchdog would tear down a
    //     perfectly healthy connection mid-home; on reconnect,
    //     pending_nowait_sends got reset to 0 but GrblParserC still
    //     thought the in-flight commands needed acks.  Result: jog
    //     throttle stays engaged, jog stops responding, user reboots.
    //
    //   • A redundant 500 ms '?' poll.  FluidNC's WSChannel constructor
    //     calls setReportInterval(200) which auto-broadcasts status to
    //     the channel; fnc_is_connected() on Core 0 also pings every
    //     200 ms when idle.  Adding our own 500 ms poll on top was just
    //     extra round-trips for no benefit.
    if (_ws_begin_called && now_connected) {
        _wsClient.loop();
        // Drain any command bytes queued by ws_putchar() (from either core)
        // and send them HERE, on Core 0, alongside loop().  This is the only
        // place _wsClient.send*() is ever called from the steady state, which
        // keeps the (non-thread-safe) WebSocket client strictly single-
        // threaded.  Must run after loop() so a freshly-completed handshake
        // is reflected in _ws_connected before we try to send.
        tx_drain();
    }
}

#endif  // ARDUINO
