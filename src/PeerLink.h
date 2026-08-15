// 2026 - Figamore

#pragma once
#ifdef USE_ESPNOW

#include <stdint.h>
#include <stddef.h>

static constexpr size_t ESPNOW_PROFILE_HOSTNAME_SIZE = 32;

struct ESPNowProfileInfo {
    uint8_t mac[6];
    uint8_t channel;
    bool    active;
    char    hostname[ESPNOW_PROFILE_HOSTNAME_SIZE];
};

void espnow_init();
void espnow_poll();


void espnow_putchar(uint8_t c);
int  espnow_getchar();  // returns -1 if no data
bool espnow_rx_available();  // true if a received byte is buffered


bool espnow_is_paired();
bool espnow_is_connected();
const char* espnow_status_str();
void espnow_start_pairing();
void espnow_cancel_pairing();

// Close the link cleanly before deep sleep.  Keeps the stored pairing so the
// pendant reconnects on wake — see the sleep path in CNC_Pendant_UI.cpp.
void espnow_graceful_disconnect();
bool espnow_pairing_complete();
void espnow_clear_pairing();
bool espnow_has_saved_pairing();
bool espnow_is_reconnecting();
int8_t espnow_rssi();

// Bytes lost to RX ring overflow since boot.  Non-zero means a transfer was
// corrupted (JSON truncation → macros / SD listing failures).
uint32_t espnow_rx_dropped();

// Packets dropped because the receive queue was full.  Non-zero means a
// fragmented message (and therefore a file/macro transfer) was abandoned.
uint32_t espnow_rx_pkt_dropped();

// Total bytes delivered to the application since boot (diagnostic).
uint32_t espnow_rx_bytes();

// Messages discarded by fragment reassembly (partial, evicted, or skipped to
// unwedge the delivery cursor).  Non-zero here means data was lost AFTER the
// radio delivered it, which packet-loss counters cannot show.
uint32_t espnow_frag_aborted();

// False when espnow_init() bailed out (queue alloc, esp_now_init, PMK or
// callback registration failed) — usually heap exhaustion.  Without this the
// UI cannot tell "radio never started" from "peer is out of range", and both
// look like an endless "Reconnecting".
bool espnow_radio_ready();
int espnow_signal_bars();
size_t espnow_profile_count();
int espnow_active_profile_index();
bool espnow_get_profile(size_t index, ESPNowProfileInfo& out);
bool espnow_select_profile(size_t index);
bool espnow_remove_profile(size_t index);

#endif
