// 2026 - Figamore

#ifdef ARDUINO

#include "PeerLink.h"
#include <string.h>

#ifdef USE_ESPNOW

#include "ESPNowCrypto.h"
#include "FluidNCModel.h"
#include "System.h"
#include "screens/pendant_shared.h"   // hwEventQueue / HwEvent — cross-core redraw

// FluidDial-CYD: upstream called current_scene->reDisplay() directly from the
// ESP-NOW task.  This fork draws on Core 1 only, so a link-state change posts a
// STATE_UPDATE to the hardware event queue instead and lets the UI core redraw.
// Non-blocking (timeout 0): dropping a redraw under queue pressure is harmless,
// the next state change or the periodic refresh will catch up.
static void peerlink_request_redraw() {
    if (!hwEventQueue) return;
    HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
    xQueueSend(hwEventQueue, &ev, 0);
}

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <stdio.h>
#include <stddef.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define PREF_NAMESPACE             "fluidespnow"
#define ESPNOW_PAIR_CHANNEL        1
#define BEACON_INTERVAL_MS         250
#define PAIR_RESULT_TIMEOUT_MS     5000
#define PAIR_CONFIRM_RETRY_MS      300
#define PAIR_COMPLETE_RETRY_MS     100
#define PAIR_COMPLETE_ATTEMPTS     3
#define CONNECT_TIMEOUT_MS         5000
#define KEEPALIVE_INTERVAL_MS      2000
#define RECONNECT_PROBE_MS         1000
#define SYNCHRONIZE_RETRY_MS       300
#define SYNCHRONIZE_TIMEOUT_MS     3000
// One comms_poll() drains up to MAX_RX_PACKETS_PER_POLL packets (16) of up to
// FRAG_PAYLOAD_MAX (238) bytes each = ~3.8 KB pushed into this ring BEFORE the
// consumer runs again.  At 2048 the ring overflowed mid-poll on any large reply
// and rx_push() silently dropped the excess, truncating the JSON — which is why
// Macros and the SD listing failed on ESP-NOW while small status reports were
// fine.  8 KB matches the drain budget in pendant_comms_task and leaves room
// for back-to-back polls.
#define RX_BUF_SIZE                8192
#define TX_BUF_SIZE                512
#define MAX_FRAGS                  8
#define MAX_MACHINE_PROFILES       5
#define FRAG_HEADER_SIZE           12  // type + nonce(4) + counter(4) + seq + idx + total
#define FRAG_PAYLOAD_MAX           (250 - FRAG_HEADER_SIZE)  // 238 bytes
#define ART_TAG_SIZE               8   // anti-replay tag: nonce(4) + counter(4)
#define AUTH_KEEPALIVE_SIZE        (1 + 4 + ART_TAG_SIZE + 1)
#define KEEPALIVE_SESSION_CONFIRMED 0x01
#define PAIR_TAG_SIZE              16  // HMAC-SHA256 truncated tag for pairing packets
#define FRAG_REASSEMBLY_TIMEOUT_MS 3000   // discard stale partial reassembly
// Depth of the ISR->poll packet queue.  This was 16, and overflowing it is
// FATAL to a large transfer rather than merely lossy: reassembly is
// all-or-nothing with no retransmit, so ONE discarded packet abandons its
// entire ~1 KB fragmented message and punches a hole in the reply.  FluidNC
// flushes a file in 1024-byte chunks (5 packets each) back-to-back, so several
// chunks in flight between our 2 ms polls easily exceeded 16 — which is why
// small, self-repeating status reports were flawless while Macros and the SD
// listing always failed.  48 absorbs ~9 chunks of burst; the cost is ~12 KB.
// 48 x sizeof(RxPacket) (~260 B) = ~12.5 KB.  Briefly cut to 24 to free heap for
// the radio; restored because RX loss was then observed on the boot burst at
// close range with full signal — i.e. genuine queue overflow, not a radio
// problem.  The heap for it comes from the panel scratch, which no longer has
// to be sized for the tallest panel (see PANEL_SCRATCH_H).
#define RX_PACKET_QUEUE_DEPTH      48
#define PAIRING_QUEUE_DEPTH        4
#define PAIR_RESULT_QUEUE_DEPTH    1
// Drain the whole queue in one poll — leaving packets queued only makes the
// next burst more likely to overflow it.
#define MAX_RX_PACKETS_PER_POLL    RX_PACKET_QUEUE_DEPTH
#define PAIRING_PACKET_INTERVAL_MS 100

#define PKT_DISCOVERY  0x01
#define PKT_PAIR_CHALLENGE 0x02
#define PKT_DATA       0x03
#define PKT_PAIR_CONFIRM 0x04
#define PKT_REALTIME   0x05
#define PKT_KEEPALIVE  0x06
#define PKT_PAIR_RESULT 0x07
#define PKT_PAIR_COMPLETE 0x08

static const uint8_t PROBE_ORDER[13] = {6, 11, 1, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
static constexpr uint8_t PAIRING_PROTO_V4 = 4;
static constexpr uint8_t PAIRING_MODE_PAIR = 1;
static constexpr size_t  ESPNOW_HOSTNAME_SIZE = ESPNOW_PROFILE_HOSTNAME_SIZE;
static constexpr size_t  PAIRING_SESSION_ID_SIZE = 16;

struct __attribute__((packed)) DiscoveryV4Pkt {
    uint8_t type;       // PKT_DISCOVERY
    uint8_t version;    // PAIRING_PROTO_V4
    uint8_t mode;       // PAIRING_MODE_PAIR
    uint8_t mac[6];     // FluidDial MAC
    uint8_t channel;    // channel FluidDial is currently using
    uint8_t session_id[PAIRING_SESSION_ID_SIZE];
    uint8_t pubkey[ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE];
};

struct __attribute__((packed)) PairChallengeV4Pkt {
    uint8_t type;       // PKT_PAIR_CHALLENGE
    uint8_t version;    // PAIRING_PROTO_V4
    uint8_t mode;       // PAIRING_MODE_PAIR
    uint8_t mac[6];     // FluidNC MAC
    uint8_t channel;    // FluidNC's operational WiFi channel
    uint8_t dial_channel; // channel carried by the accepted discovery
    uint8_t session_id[PAIRING_SESSION_ID_SIZE];
    uint8_t pubkey[ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE];
    char hostname[ESPNOW_HOSTNAME_SIZE];
};

struct __attribute__((packed)) PairConfirmV4Pkt {
    uint8_t type;       // PKT_PAIR_CONFIRM
    uint8_t version;    // PAIRING_PROTO_V4
    uint8_t mode;       // PAIRING_MODE_PAIR
    uint8_t mac[6];     // FluidDial MAC
    uint8_t session_id[PAIRING_SESSION_ID_SIZE];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};

struct __attribute__((packed)) PairResultV4Pkt {
    uint8_t type;       // PKT_PAIR_RESULT
    uint8_t version;    // PAIRING_PROTO_V4
    uint8_t mode;       // PAIRING_MODE_PAIR
    uint8_t mac[6];     // FluidNC MAC
    uint8_t channel;    // FluidNC's operational WiFi channel
    uint8_t session_id[PAIRING_SESSION_ID_SIZE];
    char hostname[ESPNOW_HOSTNAME_SIZE];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};

struct __attribute__((packed)) PairCompleteV4Pkt {
    uint8_t type;       // PKT_PAIR_COMPLETE
    uint8_t version;    // PAIRING_PROTO_V4
    uint8_t mode;       // PAIRING_MODE_PAIR
    uint8_t mac[6];     // FluidDial MAC
    uint8_t session_id[PAIRING_SESSION_ID_SIZE];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};

static_assert(sizeof(DiscoveryV4Pkt) == 91, "ESP-NOW v4 discovery layout changed");
static_assert(sizeof(PairChallengeV4Pkt) == 124, "ESP-NOW v4 challenge layout changed");
static_assert(sizeof(PairConfirmV4Pkt) == 41, "ESP-NOW v4 confirmation layout changed");
static_assert(sizeof(PairResultV4Pkt) == 74, "ESP-NOW v4 result layout changed");
static_assert(sizeof(PairCompleteV4Pkt) == 41, "ESP-NOW v4 completion layout changed");

struct __attribute__((packed)) FragHeader {
    uint8_t type;         // PKT_DATA
    uint8_t nonce[4];     // receiver's current challenge (anti-replay)
    uint8_t counter[4];   // sender's monotonic counter, little-endian (anti-replay)
    uint8_t seq;
    uint8_t frag_idx;
    uint8_t total_frags;
};

struct __attribute__((packed)) StoredMachineProfile {
    uint8_t version;
    uint8_t mac[6];
    uint8_t lmk[16];
    uint8_t channel;
    char hostname[ESPNOW_HOSTNAME_SIZE];
};

struct __attribute__((packed)) StoredMachineProfileList {
    uint8_t version;
    uint8_t active;
    uint8_t count;
    uint8_t reserved;
    StoredMachineProfile profiles[MAX_MACHINE_PROFILES];
};

static constexpr uint8_t PROFILE_STORE_VERSION = 2;

struct RxPacket {
    uint8_t src[6];
    uint16_t len;
    uint8_t channel;
    int8_t rssi;
    uint8_t data[250];
};

#if ESP_IDF_VERSION_MAJOR < 5
struct RssiSample {
    uint8_t mac[6];
    int8_t rssi;
};
#endif

static uint8_t          _peer_mac[6]  = {};
static uint8_t          _lmk[16]      = {};
static uint8_t          _op_channel   = ESPNOW_PAIR_CHANNEL;
static uint8_t          _preferred_channel = ESPNOW_PAIR_CHANNEL;
static uint32_t _last_rx_ms        = 0;
static uint32_t _keepalive_last_ms = 0;
static int8_t   _peer_rssi         = -100;  // no measurement yet

// EMA: a = 1/5
static void update_rssi(int8_t raw) {
    int8_t cur = _peer_rssi;
    _peer_rssi = (cur == -100) ? raw
                               : (int8_t)(((int)cur * 4 + (int)raw) / 5);
}

enum class LinkState : uint8_t {
    Unpaired,
    Discovering,
    Confirming,
    Synchronizing,
    Connected,
    Reconnecting,
};

enum class KeepaliveResult : uint8_t {
    Rejected,
    NewPlainSession,
    Authenticated,
    AuthenticatedConfirmed,
    DuplicatePlain,
};

static LinkState        _link_state       = LinkState::Unpaired;
static uint32_t         _link_state_since_ms = 0;
static bool             _pairing_complete = false;
static char             _peer_hostname[ESPNOW_HOSTNAME_SIZE] = {};
static uint8_t          _pairing_lmk[16] = {};
static uint8_t          _pairing_window_lmk[16] = {};
static uint8_t          _pairing_private_key[ESPNowCrypto::ECDH_PRIVATE_KEY_SIZE] = {};
static uint8_t          _pairing_public_key[ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE] = {};
static bool             _pairing_keypair_valid = false;
static uint8_t          _pairing_session_id[PAIRING_SESSION_ID_SIZE] = {};
static uint8_t          _pairing_peer_mac[6] = {};
static uint8_t          _pairing_peer_channel = 0;
static PairConfirmV4Pkt _pairing_confirm = {};
static PairCompleteV4Pkt _pairing_complete_packet = {};
static PairResultV4Pkt  _pairing_result = {};
static bool             _pairing_completion_pending = false;
static uint8_t          _pairing_completion_attempts = 0;
static uint32_t         _pairing_last_completion_ms = 0;
static uint32_t         _pairing_last_confirm_ms = 0;
static uint32_t         _pairing_await_result_ms = 0;
static uint32_t         _beacon_last_ms   = 0;
static uint8_t          _probe_idx        = 0;

static uint8_t  _reconnect_probe_idx = 0;
static uint32_t _reconnect_beacon_ms = 0;
static uint8_t  _reconnect_saved_ch  = ESPNOW_PAIR_CHANNEL;
static uint32_t _synchronize_last_tx_ms = 0;
static bool     _synchronize_authenticated_tx = false;
static std::atomic<uint8_t> _rx_channel_hint {0};
static QueueHandle_t _rx_packet_queue = nullptr;
static QueueHandle_t _pairing_queue = nullptr;
static QueueHandle_t _pair_result_queue = nullptr;
static std::atomic<uint32_t> _pair_result_rx_count {0};
static std::atomic<uint32_t> _pair_result_queue_failures {0};
#if ESP_IDF_VERSION_MAJOR < 5
static QueueHandle_t _rssi_queue = nullptr;
#endif
static uint32_t _last_pairing_packet_ms = 0;
static bool _espnow_ready = false;

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool mac_eq(const uint8_t* a, const uint8_t* b) {
    return a && b && memcmp(a, b, 6) == 0;
}

static bool has_selected_profile() {
    bool mac_nonzero = false;
    bool lmk_nonzero = false;
    for (uint8_t byte : _peer_mac) {
        mac_nonzero |= byte != 0;
    }
    for (uint8_t byte : _lmk) {
        lmk_nonzero |= byte != 0;
    }
    return mac_nonzero && (_peer_mac[0] & 0x01) == 0 && lmk_nonzero;
}

static bool valid_link_transition(LinkState from, LinkState to) {
    if (from == to || to == LinkState::Unpaired || to == LinkState::Discovering) {
        return true;
    }
    if (to == LinkState::Reconnecting) {
        return has_selected_profile();
    }
    if (to == LinkState::Confirming) {
        return from == LinkState::Discovering;
    }
    if (to == LinkState::Synchronizing) {
        return has_selected_profile() &&
               (from == LinkState::Confirming ||
                from == LinkState::Connected ||
                from == LinkState::Reconnecting);
    }
    if (to == LinkState::Connected) {
        return has_selected_profile() &&
               (from == LinkState::Synchronizing ||
                from == LinkState::Reconnecting);
    }
    return false;
}

// A '?' status poll that arrived before the link was up.  Dropping it outright
// deadlocks the startup handshake: FluidNC only enables its 200 ms auto-report
// once the peer authenticates, and that needs traffic FROM us — so a dropped
// poll means no reports, which means the pendant sits on "Syncing" until some
// other command happens to go through (the "reset or home fixes it" symptom).
// Only '?' is latched: it is idempotent and safe to deliver late, whereas a
// deferred feed-hold or cycle-start firing on reconnect would be dangerous.
static volatile bool _pending_status_poll = false;
static void send_realtime(uint8_t c);                        // defined below, with the TX path
static void send_fragments(const uint8_t* data, size_t len);  // ditto

// Send the '?' status poll as an ordinary byte in the DATA stream rather than as
// a PKT_REALTIME packet — i.e. exactly the way UART sends it.
//
// FluidNC's ESPNowChannel::pollLine() defers to Channel::pollLine(), which
// extracts realtime characters out of the incoming byte stream, so a '?' inside
// a data message is handled identically to a '?' arriving on the wire over UART.
// It is sent as its OWN message, never appended to _tx_buf, so it can still
// never be spliced into a partial command (the original "homing unsticks it"
// corruption).
//
// This matters more than it looks: FluidNC's Channel::autoReport() gates its
// periodic status report on motionState(), so an IDLE machine emits no reports
// at all no matter what the report interval is set to.  The '?' poll is
// therefore the only source of status while the machine sits idle — if it does
// not land, the pendant never syncs, which is exactly the observed fault.
static void send_status_poll() {
    static const uint8_t q = '?';
    send_fragments(&q, 1);
}

static bool set_link_state(LinkState state) {
    if (!valid_link_transition(_link_state, state)) {
        dbg_printf("ESP-NOW: rejected invalid state transition %u -> %u\n",
                   (unsigned)_link_state, (unsigned)state);
        return false;
    }
    if (_link_state == state) {
        return true;
    }

    // FluidDial-CYD: clear the jog flow-control counter whenever the link comes
    // up.  pending_nowait_sends throttles jog sends while FluidNC's planner is
    // backed up, and it is decremented by acks — so any sends that were in
    // flight when the link dropped would never be acked and would leave the
    // counter permanently high, silently throttling jogging after every
    // reconnect.  The WiFi backend did the same on its connect edge.
    if (state == LinkState::Connected) {
        pending_nowait_sends = 0;

        // Poll for status the moment the link is usable.  The pendant otherwise
        // polls only every 4 s (ping_interval_ms), and an idle machine sends
        // nothing unprompted (see send_status_poll), so without this the DRO
        // could sit stale for seconds after every reconnect.
        //
        // _link_state is assigned below, and send_fragments() requires
        // Connected, so set it first.
        _pending_status_poll = false;
        _link_state = state;
        send_status_poll();
    }

    _link_state = state;
    _link_state_since_ms = millis();
    return true;
}

static bool pairing_active() {
    return _link_state == LinkState::Discovering ||
           _link_state == LinkState::Confirming;
}

static bool accepts_selected_peer_traffic() {
    return has_selected_profile() &&
           (_link_state == LinkState::Synchronizing ||
            _link_state == LinkState::Connected ||
            _link_state == LinkState::Reconnecting);
}

static bool valid_profile(const StoredMachineProfile& profile) {
    bool mac_nonzero = false;
    bool lmk_nonzero = false;
    for (size_t i = 0; i < sizeof(profile.mac); ++i) {
        mac_nonzero |= profile.mac[i] != 0;
    }
    for (size_t i = 0; i < sizeof(profile.lmk); ++i) {
        lmk_nonzero |= profile.lmk[i] != 0;
    }

    return profile.version == PROFILE_STORE_VERSION &&
           mac_nonzero &&
           (profile.mac[0] & 0x01) == 0 &&
           lmk_nonzero &&
           profile.channel >= 1 &&
           profile.channel <= 14 &&
           memchr(profile.hostname, '\0', sizeof(profile.hostname)) != nullptr;
}

static bool valid_profile_list(const StoredMachineProfileList& list) {
    if (list.version != PROFILE_STORE_VERSION ||
        list.count > MAX_MACHINE_PROFILES ||
        (list.count != 0 && list.active >= list.count)) {
        return false;
    }
    for (uint8_t i = 0; i < list.count; ++i) {
        if (!valid_profile(list.profiles[i])) {
            return false;
        }
    }
    return true;
}

static bool load_profiles(Preferences& prefs, StoredMachineProfileList& list) {
    memset(&list, 0, sizeof(list));
    if (!prefs.isKey("profiles")) {
        return false;
    }
    if (prefs.getBytesLength("profiles") != sizeof(list)) {
        return false;
    }
    if (prefs.getBytes("profiles", &list, sizeof(list)) != sizeof(list)) {
        memset(&list, 0, sizeof(list));
        return false;
    }
    if (!valid_profile_list(list)) {
        memset(&list, 0, sizeof(list));
        return false;
    }
    return true;
}

static void init_profiles(StoredMachineProfileList& list) {
    memset(&list, 0, sizeof(list));
    list.version = PROFILE_STORE_VERSION;
}

static bool load_active_profile(Preferences& prefs) {
    StoredMachineProfileList list;
    if (!load_profiles(prefs, list) || list.count == 0) {
        return false;
    }

    const StoredMachineProfile& profile = list.profiles[list.active];
    memcpy(_peer_mac, profile.mac, sizeof(_peer_mac));
    memcpy(_lmk, profile.lmk, sizeof(_lmk));
    _preferred_channel = profile.channel ? profile.channel : ESPNOW_PAIR_CHANNEL;
    strlcpy(_peer_hostname, profile.hostname, sizeof(_peer_hostname));
    ESPNowCrypto::secureZero(&list, sizeof(list));
    return true;
}

static bool store_active_profile(Preferences& prefs,
                                 const uint8_t mac[6],
                                 const uint8_t lmk[16],
                                 uint8_t channel,
                                 const char hostname[ESPNOW_HOSTNAME_SIZE]) {
    if (!mac || !lmk || channel < 1 || channel > 14) {
        return false;
    }

    StoredMachineProfileList list;
    if (!load_profiles(prefs, list)) {
        init_profiles(list);
    }

    int slot = -1;
    for (uint8_t i = 0; i < list.count; ++i) {
        if (memcmp(list.profiles[i].mac, mac, 6) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (list.count < MAX_MACHINE_PROFILES) {
            slot = list.count++;
        } else {
            slot = list.active < MAX_MACHINE_PROFILES ? list.active : 0;
        }
    }

    StoredMachineProfile& profile = list.profiles[slot];
    memset(&profile, 0, sizeof(profile));
    profile.version = PROFILE_STORE_VERSION;
    memcpy(profile.mac, mac, 6);
    memcpy(profile.lmk, lmk, 16);
    profile.channel = channel;
    strlcpy(profile.hostname, hostname ? hostname : "", sizeof(profile.hostname));
    list.active = (uint8_t)slot;

    bool stored = prefs.putBytes("profiles", &list, sizeof(list)) == sizeof(list);
    ESPNowCrypto::secureZero(&list, sizeof(list));
    return stored;
}

static void profile_to_info(const StoredMachineProfileList& list, uint8_t index, ESPNowProfileInfo& out) {
    memset(&out, 0, sizeof(out));
    if (index >= list.count) {
        return;
    }
    const StoredMachineProfile& profile = list.profiles[index];
    memcpy(out.mac, profile.mac, sizeof(out.mac));
    out.channel = profile.channel;
    out.active = index == list.active;
    strlcpy(out.hostname, profile.hostname, sizeof(out.hostname));
}


static uint8_t             _rx_buf[RX_BUF_SIZE];
static std::atomic<int>    _rx_head {0};
static int                 _rx_tail = 0;

// Fragment reassembly, processed from espnow_poll().
//
// There used to be exactly ONE reassembly slot, which silently destroyed any
// multi-fragment message that a second message overlapped.  Each message gets
// its own seq (send_fragments: `seq = _tx_seq++`), so a multi-LINE reply — an
// $I burst, a macro list, an SD listing — is a burst of separate messages, and
// ESP-NOW unicast retries give no cross-message ordering.  With one slot,
// A-frag0, B, A-frag1 meant A was dropped on B's arrival and A-frag1 then
// opened a fresh pending that could never complete.  Short single-fragment
// replies always survived (they complete on arrival), which is why the version
// string and status reports worked flawlessly while Macros and SD never did.
//
// Messages are emitted as soon as they complete, NOT held back and released in
// seq order.  Ordering was tried and is wrong here: it assumes the sequence
// numbers we observe are contiguous, and a single gap stalls the cursor for its
// whole timeout while later messages fill the slots and get evicted — which
// loses everything after the first message of a multi-line reply.
#define FRAG_SLOTS 4

struct FragSlot {
    uint8_t  buf[MAX_FRAGS][FRAG_PAYLOAD_MAX];
    uint8_t  len[MAX_FRAGS];
    uint8_t  got;
    uint8_t  total;
    uint8_t  seq;
    bool     used;
    uint32_t start_ms;
};

static FragSlot _frag[FRAG_SLOTS];
static uint32_t _frag_aborted = 0;        // partial/evicted messages — diagnostic

// Sequence numbers of the last few messages emitted, so a unicast retry cannot
// re-open a slot for a message already delivered.
static uint8_t  _recent_seq[FRAG_SLOTS]       = { 0 };
static bool     _recent_seq_valid[FRAG_SLOTS] = { false };
static int      _recent_slot                  = 0;

static void frag_reset_all() {
    for (int i = 0; i < FRAG_SLOTS; i++) {
        _frag[i].used = false;
        _recent_seq_valid[i] = false;
    }
    _recent_slot = 0;
}

static uint8_t _tx_buf[TX_BUF_SIZE];
static int     _tx_len = 0;
static uint8_t _tx_seq = 0;

// ---- anti-replay ----------------------------------------------------------
// Each side issues a random 32-bit "challenge" nonce for the traffic it
// receives and advertises it in its keepalives. The peer stamps every
// DATA/REALTIME frame with challenge; (a monotonic 32 bit counter). The
// receiver accepts a frame only if the nonce matches its current challenge and
// the counter is ahead of a 64-entry sliding window, so a captured frame can't
// be replayed. A fresh challenge is issued on every reconnect.
static std::atomic<uint32_t> _rx_nonce      {0};  // challenge to the peer
static std::atomic<uint32_t> _tx_peer_nonce {0};  // peer's challenge, learned via keepalive
static std::atomic<bool>     _tx_peer_known {false};
static std::atomic<uint32_t> _tx_counter    {0};  // monotonic send counter

// Receiver sliding-window state is touched only from espnow_poll().
static ESPNowCrypto::ReplayState _rx_replay;

static void reset_link_session() {
    _tx_peer_known.store(false, std::memory_order_release);
    _tx_peer_nonce.store(0, std::memory_order_release);
    _tx_counter.store(0, std::memory_order_release);
    _tx_seq = 0;
    ESPNowCrypto::issueRxChallenge(_rx_nonce);
    _rx_replay = {};
    frag_reset_all();
}

static bool register_peer(const uint8_t mac[6], uint8_t channel, bool encrypt, const uint8_t lmk[16]) {
    if (!mac || channel < 1 || channel > 14 || (encrypt && !lmk)) {
        return false;
    }

    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = channel;
    p.encrypt = encrypt;
    if (encrypt) memcpy(p.lmk, lmk, 16);
    p.ifidx = WIFI_IF_STA;

    esp_err_t result;
    if (esp_now_is_peer_exist(mac)) {
        if (esp_now_get_peer(mac, &p) != ESP_OK) {
            return false;
        }
        p.channel = channel;
        p.encrypt = encrypt;
        if (encrypt) {
            memcpy(p.lmk, lmk, 16);
        } else {
            memset(p.lmk, 0, sizeof(p.lmk));
        }
        result = esp_now_mod_peer(&p);
        if (result != ESP_OK) {
            esp_now_del_peer(mac);
            result = esp_now_add_peer(&p);
        }
    } else {
        result = esp_now_add_peer(&p);
    }
    return result == ESP_OK;
}

// Dropped-byte counter.  A silent drop here corrupts whatever transfer is in
// flight (JSON truncates, macros/SD "fail to load") with no other symptom, so
// count it and say so once — an invisible failure mode cost a release to find.
static uint32_t _rx_dropped = 0;
// Packets discarded because the ISR->poll queue was full.  Non-zero means a
// fragmented message was abandoned — see RX_PACKET_QUEUE_DEPTH.
static volatile uint32_t _rx_pkt_dropped = 0;
// Total bytes handed to the application.  Distinguishes "nothing arrived" from
// "arrived but was not parsed".
static volatile uint32_t _rx_bytes = 0;

static inline void rx_push(uint8_t c) {
    int cur  = _rx_head.load(std::memory_order_relaxed);
    int next = (cur + 1) % RX_BUF_SIZE;
    if (next != _rx_tail) {
        _rx_buf[cur] = c;
        _rx_head.store(next, std::memory_order_release);
        ++_rx_bytes;
    } else {
        if (++_rx_dropped == 1) {
            dbg_println("ESP-NOW: RX ring FULL — dropping bytes (transfers will corrupt)");
        }
    }
}

uint32_t espnow_rx_dropped()     { return _rx_dropped; }
uint32_t espnow_rx_pkt_dropped() { return _rx_pkt_dropped; }
uint32_t espnow_rx_bytes()       { return _rx_bytes; }
uint32_t espnow_frag_aborted()   { return _frag_aborted; }
bool     espnow_radio_ready()    { return _espnow_ready; }

static inline int rx_pop() {
    int head = _rx_head.load(std::memory_order_acquire);
    if (head == _rx_tail) return -1;
    uint8_t c = _rx_buf[_rx_tail];
    _rx_tail  = (_rx_tail + 1) % RX_BUF_SIZE;
    return (unsigned char)c;
}

static void set_connected_now() {
    _last_rx_ms = millis();
    set_link_state(LinkState::Connected);
}

static uint8_t current_wifi_channel() {
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) != ESP_OK) {
        return 0;
    }
    return primary;
}

static void note_rx_channel(uint8_t channel) {
    if (channel >= 1 && channel <= 14) {
        _rx_channel_hint.store(channel, std::memory_order_release);
    }
}

static bool send_keepalive(const uint8_t mac[6]) {
    uint32_t n = _rx_nonce.load(std::memory_order_acquire);
    if (!_tx_peer_known.load(std::memory_order_acquire)) {
        uint8_t pkt[5];
        pkt[0] = PKT_KEEPALIVE;
        memcpy(pkt + 1, &n, 4);
        esp_now_send(mac, pkt, sizeof(pkt));
        return false;
    }

    uint8_t pkt[AUTH_KEEPALIVE_SIZE];
    pkt[0] = PKT_KEEPALIVE;
    memcpy(pkt + 1, &n, 4);   // auth'd advertisement of the challenge
    if (!ESPNowCrypto::stampAntiReplayTag(
            _tx_peer_known, _tx_peer_nonce, _tx_counter, pkt + 5)) {
        return false;
    }
    pkt[1 + 4 + ART_TAG_SIZE] =
        _link_state == LinkState::Connected ? KEEPALIVE_SESSION_CONFIRMED : 0;
    return esp_now_send(mac, pkt, sizeof(pkt)) == ESP_OK;
}

static void begin_reconnecting() {
    if (!has_selected_profile()) {
        set_link_state(LinkState::Unpaired);
        return;
    }

    _tx_len = 0;
    reset_link_session();
    _last_rx_ms = 0;
    _reconnect_probe_idx = 0;
    _reconnect_beacon_ms = 0;
    _reconnect_saved_ch = _preferred_channel;
    _synchronize_authenticated_tx = false;
    set_link_state(LinkState::Reconnecting);
    set_disconnected_state();
    dbg_printf("ESP-NOW: reconnect started — saved ch=%d\n", _preferred_channel);
}

static void begin_synchronizing(uint8_t channel) {
    if (!has_selected_profile() || channel < 1 || channel > 14) {
        begin_reconnecting();
        return;
    }

    _op_channel = channel;
    _synchronize_last_tx_ms = millis();
    set_link_state(LinkState::Synchronizing);
    _synchronize_authenticated_tx = send_keepalive(_peer_mac);
}

static KeepaliveResult accept_keepalive(const uint8_t* data, int len) {
    if (len == AUTH_KEEPALIVE_SIZE) {
        uint32_t advertised, nonce, counter;
        memcpy(&advertised, data + 1, 4);
        memcpy(&nonce,      data + 5, 4);
        memcpy(&counter,    data + 9, 4);
        if (!ESPNowCrypto::acceptReplay(
                _rx_nonce, _rx_replay, nonce, counter, millis()) ||
            advertised == 0) {
            return KeepaliveResult::Rejected;
        }
        _tx_peer_nonce.store(advertised, std::memory_order_release);
        _tx_peer_known.store(true, std::memory_order_release);
        return (data[1 + 4 + ART_TAG_SIZE] & KEEPALIVE_SESSION_CONFIRMED)
                   ? KeepaliveResult::AuthenticatedConfirmed
                   : KeepaliveResult::Authenticated;
    }

    if (len == 5) {
        uint32_t advertised;
        memcpy(&advertised, data + 1, 4);
        if (advertised == 0) return KeepaliveResult::Rejected;

        bool new_session =
            !_tx_peer_known.load(std::memory_order_acquire) ||
            _tx_peer_nonce.load(std::memory_order_acquire) != advertised;
        _tx_peer_nonce.store(advertised, std::memory_order_release);
        _tx_peer_known.store(true, std::memory_order_release);
        if (new_session) {
            _tx_counter.store(0, std::memory_order_release);
            ESPNowCrypto::issueRxChallenge(_rx_nonce);
            _rx_replay = {};
        }
        return new_session ? KeepaliveResult::NewPlainSession
                           : KeepaliveResult::DuplicatePlain;
    }

    return KeepaliveResult::Rejected;
}

static void new_pairing_session_id() {
    static const uint8_t zero_session[PAIRING_SESSION_ID_SIZE] = {};
    do {
        for (size_t offset = 0; offset < sizeof(_pairing_session_id); offset += sizeof(uint32_t)) {
            uint32_t value = ESPNowCrypto::randomNonce();
            memcpy(_pairing_session_id + offset, &value, sizeof(value));
        }
    } while (ESPNowCrypto::constantTimeEquals(
        _pairing_session_id, zero_session, sizeof(_pairing_session_id)));
}

static void build_discovery_v4(DiscoveryV4Pkt& pkt,
                               const uint8_t my_mac[6],
                               uint8_t channel) {
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = PKT_DISCOVERY;
    pkt.version = PAIRING_PROTO_V4;
    pkt.mode = PAIRING_MODE_PAIR;
    memcpy(pkt.mac, my_mac, 6);
    pkt.channel = channel;
    memcpy(pkt.session_id, _pairing_session_id, sizeof(pkt.session_id));
    memcpy(pkt.pubkey, _pairing_public_key, sizeof(pkt.pubkey));
}

static bool complete_pairing_from_result(const PairResultV4Pkt& result);

static void clear_pairing_secrets() {
    _pairing_keypair_valid = false;
    ESPNowCrypto::secureZero(_pairing_private_key, sizeof(_pairing_private_key));
    ESPNowCrypto::secureZero(_pairing_public_key, sizeof(_pairing_public_key));
    ESPNowCrypto::secureZero(_pairing_window_lmk, sizeof(_pairing_window_lmk));
    ESPNowCrypto::secureZero(_pairing_lmk, sizeof(_pairing_lmk));
    ESPNowCrypto::secureZero(&_pairing_confirm, sizeof(_pairing_confirm));
    ESPNowCrypto::secureZero(&_pairing_complete_packet, sizeof(_pairing_complete_packet));
    ESPNowCrypto::secureZero(&_pairing_result, sizeof(_pairing_result));
    _pairing_completion_pending = false;
    _pairing_completion_attempts = 0;
    _pairing_last_completion_ms = 0;
}

static void remove_broadcast_peer() {
    if (esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_del_peer(BROADCAST_MAC);
    }
}

static bool accept_pair_result(const uint8_t* src, const uint8_t* data, int len) {
    if (_link_state != LinkState::Confirming) {
        dbg_printf("ESP-NOW: ignored pairing result in state %u\n",
                   (unsigned)_link_state);
        return false;
    }
    if (!mac_eq(src, _pairing_peer_mac)) {
        dbg_println("ESP-NOW: rejected pairing result from unexpected peer");
        return false;
    }
    if (len != (int)sizeof(PairResultV4Pkt)) {
        dbg_printf("ESP-NOW: rejected pairing result length %d (expected %u)\n",
                   len, (unsigned)sizeof(PairResultV4Pkt));
        return false;
    }

    PairResultV4Pkt result;
    memcpy(&result, data, sizeof(result));
    if (result.type != PKT_PAIR_RESULT ||
        result.version != PAIRING_PROTO_V4 ||
        result.mode != PAIRING_MODE_PAIR ||
        !mac_eq(src, result.mac) ||
        !ESPNowCrypto::constantTimeEquals(
            result.session_id, _pairing_session_id, sizeof(result.session_id))) {
        dbg_println("ESP-NOW: rejected pairing result metadata");
        return false;
    }

    if (!ESPNowCrypto::verifyPairingAuthTag(_pairing_lmk,
                                            reinterpret_cast<const uint8_t*>(&result),
                                            offsetof(PairResultV4Pkt, auth_tag),
                                            result.auth_tag)) {
        dbg_println("ESP-NOW: rejected unauthenticated pairing result");
        return false;
    }

    uint8_t my_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, my_mac);
    memset(&_pairing_complete_packet, 0, sizeof(_pairing_complete_packet));
    _pairing_complete_packet.type = PKT_PAIR_COMPLETE;
    _pairing_complete_packet.version = PAIRING_PROTO_V4;
    _pairing_complete_packet.mode = PAIRING_MODE_PAIR;
    memcpy(_pairing_complete_packet.mac, my_mac, sizeof(_pairing_complete_packet.mac));
    memcpy(_pairing_complete_packet.session_id,
           result.session_id,
           sizeof(_pairing_complete_packet.session_id));
    ESPNowCrypto::pairingAuthTag(
        _pairing_lmk,
        reinterpret_cast<const uint8_t*>(&_pairing_complete_packet),
        offsetof(PairCompleteV4Pkt, auth_tag),
        _pairing_complete_packet.auth_tag);

    memcpy(&_pairing_result, &result, sizeof(_pairing_result));
    _pairing_completion_pending = true;
    _pairing_completion_attempts = 1;
    _pairing_last_completion_ms = millis();
    bool queued =
        esp_now_send(_pairing_peer_mac,
                     reinterpret_cast<const uint8_t*>(&_pairing_complete_packet),
                     sizeof(_pairing_complete_packet)) == ESP_OK;
    dbg_println("ESP-NOW: pairing result authenticated");
    ESPNowCrypto::secureZero(&result, sizeof(result));
    return queued;
}

static bool accept_pair_challenge(const uint8_t* src,
                                  const uint8_t* data,
                                  int len,
                                  uint8_t rx_channel) {
    if (_link_state != LinkState::Discovering ||
        !_pairing_keypair_valid ||
        len != (int)sizeof(PairChallengeV4Pkt)) {
        return false;
    }

    PairChallengeV4Pkt challenge;
    memcpy(&challenge, data, sizeof(challenge));
    if (challenge.type != PKT_PAIR_CHALLENGE ||
        challenge.version != PAIRING_PROTO_V4 ||
        challenge.mode != PAIRING_MODE_PAIR ||
        !mac_eq(src, challenge.mac) ||
        !ESPNowCrypto::constantTimeEquals(
            challenge.session_id, _pairing_session_id, sizeof(challenge.session_id)) ||
        challenge.channel < 1 ||
        challenge.channel > 14 ||
        challenge.dial_channel < 1 ||
        challenge.dial_channel > 14 ||
        challenge.pubkey[0] != 0x04) {
        return false;
    }

    uint8_t shared_secret[ESPNowCrypto::ECDH_SHARED_SECRET_SIZE] = {};
    if (!ESPNowCrypto::deriveEcdhSharedSecret(_pairing_private_key, challenge.pubkey, shared_secret)) {
        return false;
    }

    uint8_t my_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, my_mac);
    DiscoveryV4Pkt discovery;
    build_discovery_v4(discovery, my_mac, challenge.dial_channel);

    ESPNowCrypto::derivePairingSessionLmk(_pairing_window_lmk,
                                          shared_secret,
                                          reinterpret_cast<const uint8_t*>(&discovery),
                                          sizeof(discovery),
                                          reinterpret_cast<const uint8_t*>(&challenge),
                                          sizeof(challenge),
                                          _pairing_lmk);

    memset(&_pairing_confirm, 0, sizeof(_pairing_confirm));
    _pairing_confirm.type = PKT_PAIR_CONFIRM;
    _pairing_confirm.version = PAIRING_PROTO_V4;
    _pairing_confirm.mode = PAIRING_MODE_PAIR;
    memcpy(_pairing_confirm.mac, my_mac, sizeof(_pairing_confirm.mac));
    memcpy(_pairing_confirm.session_id, challenge.session_id, sizeof(_pairing_confirm.session_id));
    ESPNowCrypto::pairingAuthTag(_pairing_lmk,
                                 reinterpret_cast<const uint8_t*>(&_pairing_confirm),
                                 offsetof(PairConfirmV4Pkt, auth_tag),
                                 _pairing_confirm.auth_tag);

    uint8_t channel = (rx_channel >= 1 && rx_channel <= 14)
                      ? rx_channel : challenge.channel;
    bool peer_registered =
        channel >= 1 &&
        channel <= 14 &&
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK &&
        register_peer(challenge.mac, channel, false, nullptr);
    bool sent = peer_registered &&
                esp_now_send(challenge.mac,
                             reinterpret_cast<const uint8_t*>(&_pairing_confirm),
                             sizeof(_pairing_confirm)) == ESP_OK;
    if (sent) {
        memcpy(_pairing_peer_mac, challenge.mac, sizeof(_pairing_peer_mac));
        _pairing_peer_channel = channel;
        set_link_state(LinkState::Confirming);
        _pairing_last_confirm_ms = millis();
        _pairing_await_result_ms = _pairing_last_confirm_ms;
    } else if (peer_registered) {
        esp_now_del_peer(challenge.mac);
    }

    ESPNowCrypto::secureZero(shared_secret, sizeof(shared_secret));
    ESPNowCrypto::secureZero(&discovery, sizeof(discovery));
    ESPNowCrypto::secureZero(&challenge, sizeof(challenge));
    return sent;
}

static void send_realtime(uint8_t c) {
    uint8_t pkt[1 + ART_TAG_SIZE + 1];
    pkt[0] = PKT_REALTIME;
    if (!ESPNowCrypto::stampAntiReplayTag(_tx_peer_known, _tx_peer_nonce, _tx_counter, pkt + 1)) return;   // peer challenge not learned yet -> drop
    pkt[1 + ART_TAG_SIZE] = c;
    esp_now_send(_peer_mac, pkt, sizeof(pkt));
}

static void send_fragments(const uint8_t* data, size_t len) {
    if (_link_state != LinkState::Connected || len == 0) return;

    uint8_t total = (uint8_t)((len + FRAG_PAYLOAD_MAX - 1) / FRAG_PAYLOAD_MAX);
    if (total == 0) total = 1;
    if (total > MAX_FRAGS) {
        dbg_printf("ESP-NOW: TX message %u B exceeds max (%u B) — truncating\n",
                   (unsigned)len, (unsigned)(MAX_FRAGS * FRAG_PAYLOAD_MAX));
        total = MAX_FRAGS;
        len   = MAX_FRAGS * FRAG_PAYLOAD_MAX;
    }

    uint8_t seq    = _tx_seq++;
    uint8_t pkt[250];
    size_t  offset = 0;
    for (uint8_t i = 0; i < total; i++) {
        size_t chunk = len - offset;
        if (chunk > FRAG_PAYLOAD_MAX) chunk = FRAG_PAYLOAD_MAX;
        pkt[0] = PKT_DATA;
        if (!ESPNowCrypto::stampAntiReplayTag(_tx_peer_known, _tx_peer_nonce, _tx_counter, pkt + 1)) return;   // peer challenge not learned yet -> drop
        pkt[9]  = seq;                     // 1 + ART_TAG_SIZE
        pkt[10] = i;
        pkt[11] = total;
        memcpy(pkt + FRAG_HEADER_SIZE, data + offset, chunk);
        esp_now_send(_peer_mac, pkt, FRAG_HEADER_SIZE + chunk);
        offset += chunk;
    }
}

static bool complete_pairing_from_result(const PairResultV4Pkt& result) {
    uint8_t new_channel = (result.channel > 0 && result.channel <= 14)
                          ? result.channel : ESPNOW_PAIR_CHANNEL;
    if (esp_wifi_set_channel(new_channel, WIFI_SECOND_CHAN_NONE) != ESP_OK ||
        !register_peer(result.mac, new_channel, true, _pairing_lmk)) {
        dbg_println("ESP-NOW: failed to activate encrypted peer");
        if (has_selected_profile()) {
            esp_wifi_set_channel(_preferred_channel, WIFI_SECOND_CHAN_NONE);
            register_peer(_peer_mac, _preferred_channel, true, _lmk);
        }
        return false;
    }

    memcpy(_peer_mac, result.mac, 6);
    memcpy(_lmk, _pairing_lmk, 16);
    memcpy(_peer_hostname, result.hostname, sizeof(_peer_hostname));
    _peer_hostname[sizeof(_peer_hostname) - 1] = '\0';
    _preferred_channel = new_channel;
    _op_channel = new_channel;

    Preferences prefs;
    bool prefs_open = prefs.begin(PREF_NAMESPACE, false);
    bool stored = prefs_open &&
                  store_active_profile(prefs,
                                       _peer_mac,
                                       _lmk,
                                       _preferred_channel,
                                       _peer_hostname);
    if (prefs_open) {
        prefs.end();
    }
    if (!stored) {
        dbg_println("ESP-NOW: warning: failed to persist machine profile");
    }

    _pairing_complete = true;
    _pairing_peer_channel = 0;
    memset(_pairing_peer_mac, 0, sizeof(_pairing_peer_mac));
    clear_pairing_secrets();
    remove_broadcast_peer();

    // Start a fresh anti-replay session + re-learn the peer's challenge from
    // its first keepalive before sending any DATA/REALTIME frames.
    reset_link_session();
    _last_rx_ms = 0;
    begin_synchronizing(new_channel);

    dbg_printf("ESP-NOW: pairing complete — peer %02x:%02x:%02x:%02x:%02x:%02x host=%s ch=%d\n",
               _peer_mac[0], _peer_mac[1], _peer_mac[2],
               _peer_mac[3], _peer_mac[4], _peer_mac[5], _peer_hostname, _op_channel);
    return true;
}

static void process_rx_packet(const RxPacket& packet) {
    const uint8_t* src = packet.src;
    const uint8_t* data = packet.data;
    int len = packet.len;
    uint8_t pkt_type = data[0];

    if (has_selected_profile() && mac_eq(src, _peer_mac) && packet.rssi != -100) {
        update_rssi(packet.rssi);
    }

    if (pkt_type == PKT_PAIR_CHALLENGE && len == (int)sizeof(PairChallengeV4Pkt) &&
        accept_pair_challenge(src, data, len, packet.channel)) {
        return;
    }
    if (pkt_type == PKT_PAIR_RESULT) {
        if (accept_pair_result(src, data, len)) {
            peerlink_request_redraw();
        }
        return;
    }

    // Pairing owns the radio channel. Ignore traffic from the previously
    // selected profile until pairing completes or is cancelled.
    if (!accepts_selected_peer_traffic() || !mac_eq(src, _peer_mac)) {
        return;
    }

    if (pkt_type == PKT_KEEPALIVE) {
        KeepaliveResult keepalive = accept_keepalive(data, len);
        if (keepalive == KeepaliveResult::Authenticated ||
            keepalive == KeepaliveResult::AuthenticatedConfirmed) {
            note_rx_channel(packet.channel);
            if (_link_state == LinkState::Reconnecting) {
                begin_synchronizing(packet.channel);
            } else if (_link_state == LinkState::Synchronizing) {
                if (keepalive == KeepaliveResult::AuthenticatedConfirmed &&
                    _synchronize_authenticated_tx) {
                    set_connected_now();
                    update_rx_time();
                } else {
                    _synchronize_last_tx_ms = millis();
                    _synchronize_authenticated_tx = send_keepalive(_peer_mac);
                }
            } else if (_link_state == LinkState::Connected) {
                set_connected_now();
                update_rx_time();
            }
        } else if (keepalive == KeepaliveResult::NewPlainSession) {
            note_rx_channel(packet.channel);
            begin_synchronizing(packet.channel);
        } else if (keepalive == KeepaliveResult::DuplicatePlain) {
            note_rx_channel(packet.channel);
            if (_link_state == LinkState::Synchronizing ||
                _link_state == LinkState::Connected) {
                // A valid (replay-checked) duplicate keepalive is still proof the
                // peer is alive. Feed the RX watchdog (_last_rx_ms) so a steady
                // stream of these isn't mistaken for silence and doesn't trigger a
                // needless reconnect; also refresh the model-level link timer.
                _last_rx_ms = millis();
                update_rx_time();
                send_keepalive(_peer_mac);
            }
        }
        return;
    }

    if (pkt_type == PKT_REALTIME && len == 1 + ART_TAG_SIZE + 1) {
        uint32_t nonce, counter;
        memcpy(&nonce, data + 1, 4);
        memcpy(&counter, data + 5, 4);
        if (!ESPNowCrypto::acceptReplay(_rx_nonce, _rx_replay, nonce, counter, millis())) {
            return;
        }
        note_rx_channel(packet.channel);
        set_connected_now();
        rx_push(data[1 + ART_TAG_SIZE]);
        update_rx_time();
        return;
    }

    if (pkt_type != PKT_DATA || len < FRAG_HEADER_SIZE) {
        return;
    }

    const FragHeader* hdr = reinterpret_cast<const FragHeader*>(data);
    uint32_t nonce, counter;
    memcpy(&nonce, hdr->nonce, 4);
    memcpy(&counter, hdr->counter, 4);
    if (!ESPNowCrypto::acceptReplay(_rx_nonce, _rx_replay, nonce, counter, millis())) {
        return;
    }

    uint8_t idx = hdr->frag_idx;
    uint8_t total = hdr->total_frags;
    uint8_t seq = hdr->seq;
    int plen = len - FRAG_HEADER_SIZE;
    if (total == 0 || total > MAX_FRAGS || idx >= total ||
        plen < 0 || plen > FRAG_PAYLOAD_MAX) {
        return;
    }

    note_rx_channel(packet.channel);
    set_connected_now();

    const uint32_t now = millis();

    // Retire partials that will never finish, so their slot can be reused.
    for (int i = 0; i < FRAG_SLOTS; i++) {
        if (_frag[i].used &&
            (uint32_t)(now - _frag[i].start_ms) > FRAG_REASSEMBLY_TIMEOUT_MS) {
            _frag[i].used = false;
            ++_frag_aborted;
        }
    }

    // Ignore a message we have already emitted.  Unicast retries can redeliver
    // a fragment after the message completed; without this it would open a
    // fresh slot that can never fill and expire as a spurious abort.
    for (int i = 0; i < FRAG_SLOTS; i++) {
        if (_recent_seq_valid[i] && _recent_seq[i] == seq) return;
    }

    // Find this message's slot, or open one.
    FragSlot* s = nullptr;
    for (int i = 0; i < FRAG_SLOTS && !s; i++) {
        if (_frag[i].used && _frag[i].seq == seq) s = &_frag[i];
    }
    if (!s) {
        for (int i = 0; i < FRAG_SLOTS && !s; i++) {
            if (!_frag[i].used) s = &_frag[i];
        }
        if (!s) {
            // All slots busy: evict the oldest, which is the least likely to
            // still be completable.
            FragSlot* victim = &_frag[0];
            for (int i = 1; i < FRAG_SLOTS; i++) {
                if ((int32_t)(victim->start_ms - _frag[i].start_ms) > 0) victim = &_frag[i];
            }
            victim->used = false;
            ++_frag_aborted;
            s = victim;
        }
        s->used     = true;
        s->got      = 0;
        s->total    = total;
        s->seq      = seq;
        s->start_ms = now;
        memset(s->len, 0, sizeof(s->len));
    }

    if (s->total != total) {
        return;
    }

    memcpy(s->buf[idx], data + FRAG_HEADER_SIZE, (size_t)plen);
    s->len[idx] = (uint8_t)plen;
    s->got |= (uint8_t)(1u << idx);

    uint8_t full_mask = total >= 8 ? 0xFF : (uint8_t)((1u << total) - 1u);
    if ((s->got & full_mask) != full_mask) {
        return;
    }

    // Emit as soon as the message is complete.
    //
    // An earlier version held completed messages back and released them in
    // strict seq order, to stop a short message overtaking a fragmented one.
    // That assumes the sequence numbers we observe are contiguous, and they are
    // not — so a single gap stalled the cursor for its whole 3 s timeout while
    // later messages piled into the four slots and were evicted.  The visible
    // effect was that a multi-line reply delivered its FIRST message and lost
    // the rest: the controller's firmware version arrived from $I while the
    // IP/SSID line that follows it in the same reply never did.  Reordering is
    // the rarer risk of the two — FluidNC sends messages sequentially, so it
    // needs the radio to reorder within a few milliseconds — and losing most of
    // every multi-line reply is a certainty, so deliver on completion.
    for (int i = 0; i < total; ++i) {
        for (int j = 0; j < s->len[i]; ++j) {
            uint8_t c = s->buf[i][j];
            if (c != '\r') {
                rx_push(c);
            }
        }
    }
    s->used = false;
    _recent_seq[_recent_slot]       = seq;
    _recent_seq_valid[_recent_slot] = true;
    _recent_slot = (_recent_slot + 1) % FRAG_SLOTS;

    // NOTE: do NOT synthesise a trailing '\n' here.
    //
    // Upstream appended one whenever a reassembled message did not already end
    // with a newline, on the assumption that one ESP-NOW message == one line.
    // That assumption breaks on file transfers: FluidNC's ESPNowChannel::write()
    // flushes when the buffer ends in '\n' OR when it reaches TX_FLUSH_THRESHOLD
    // (1024 B) — and the second case fires MID-LINE.  Appending a newline there
    // splices a line break into the middle of the payload every 1024 bytes.  For
    // a ~10 KB macros/SD reply that is ~10 spurious breaks at arbitrary offsets;
    // any one landing inside a JSON string literal makes the document invalid
    // and the parse dies, which is exactly why Macros and the SD listing failed
    // while everything else was flawless (short replies always end in '\n', so
    // the injection never fired for them).
    //
    // This is a byte stream: line framing belongs to the newlines actually in
    // the data, so pass the bytes through untouched.
    update_rx_time();
}

#if ESP_IDF_VERSION_MAJOR >= 5
static void on_data_recv(const esp_now_recv_info_t* recv_info,
                         const uint8_t* data, int len) {
    const uint8_t* src = recv_info ? recv_info->src_addr : nullptr;
    uint8_t rx_channel = (recv_info && recv_info->rx_ctrl) ? recv_info->rx_ctrl->channel : 0;
    int8_t rssi = (recv_info && recv_info->rx_ctrl) ? (int8_t)recv_info->rx_ctrl->rssi : -100;
#else
static void on_data_recv(const uint8_t* src,
                         const uint8_t* data, int len) {
    uint8_t rx_channel = current_wifi_channel();
    int8_t rssi = -100;
#endif
    if (!src || !data || len < 1 || len > 250) return;
    uint8_t pkt_type = data[0];
    QueueHandle_t queue = nullptr;

    if (pkt_type == PKT_PAIR_RESULT) {
        queue = _pair_result_queue;
    } else if (pkt_type == PKT_PAIR_CHALLENGE && len == (int)sizeof(PairChallengeV4Pkt)) {
        queue = _pairing_queue;
    } else if ((pkt_type == PKT_KEEPALIVE &&
                (len == 5 || len == AUTH_KEEPALIVE_SIZE)) ||
               (pkt_type == PKT_REALTIME && len == 1 + ART_TAG_SIZE + 1) ||
               (pkt_type == PKT_DATA && len >= FRAG_HEADER_SIZE)) {
        queue = _rx_packet_queue;
    }

    if (!queue) return;

    RxPacket packet;
    memcpy(packet.src, src, sizeof(packet.src));
    packet.len = (uint16_t)len;
    packet.channel = rx_channel;
    packet.rssi = rssi;
    memcpy(packet.data, data, (size_t)len);
    if (queue == _pair_result_queue) {
        _pair_result_rx_count.fetch_add(1, std::memory_order_relaxed);
        if (xQueueOverwrite(queue, &packet) != pdTRUE) {
            _pair_result_queue_failures.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        // Timeout 0: never block the WiFi task.  A full queue means the packet
        // is discarded, which will abandon whatever fragmented message it
        // belonged to — count it so the failure is visible instead of silent.
        if (xQueueSend(queue, &packet, 0) != pdTRUE) {
            ++_rx_pkt_dropped;
        }
    }
}

#if ESP_IDF_VERSION_MAJOR < 5
static void promiscuous_rx_cb(void* buf, wifi_promiscuous_pkt_type_t /*type*/) {
    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    if (!_rssi_queue || pkt->rx_ctrl.sig_len < 16) return;
    RssiSample sample;
    memcpy(sample.mac, pkt->payload + 10, sizeof(sample.mac));
    sample.rssi = pkt->rx_ctrl.rssi;
    xQueueSend(_rssi_queue, &sample, 0);
}
#endif

static void on_data_sent(const uint8_t* /*mac*/, esp_now_send_status_t /*status*/) {}

static void reset_receive_queues() {
    if (_rx_packet_queue) xQueueReset(_rx_packet_queue);
    if (_pairing_queue) xQueueReset(_pairing_queue);
    if (_pair_result_queue) xQueueReset(_pair_result_queue);
#if ESP_IDF_VERSION_MAJOR < 5
    if (_rssi_queue) xQueueReset(_rssi_queue);
#endif
}

static void delete_receive_queues() {
    if (_rx_packet_queue) vQueueDelete(_rx_packet_queue);
    if (_pairing_queue) vQueueDelete(_pairing_queue);
    if (_pair_result_queue) vQueueDelete(_pair_result_queue);
    _rx_packet_queue = nullptr;
    _pairing_queue = nullptr;
    _pair_result_queue = nullptr;
#if ESP_IDF_VERSION_MAJOR < 5
    if (_rssi_queue) vQueueDelete(_rssi_queue);
    _rssi_queue = nullptr;
#endif
}

static void clear_temporary_pairing_peer() {
    if (_link_state == LinkState::Confirming) {
        esp_now_del_peer(_pairing_peer_mac);
    }
    _pairing_peer_channel = 0;
    _pairing_last_confirm_ms = 0;
    _pairing_await_result_ms = 0;
    memset(_pairing_peer_mac, 0, sizeof(_pairing_peer_mac));
}

void espnow_init() {
    if (_espnow_ready) return;

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    _rx_packet_queue = xQueueCreate(RX_PACKET_QUEUE_DEPTH, sizeof(RxPacket));
    _pairing_queue = xQueueCreate(PAIRING_QUEUE_DEPTH, sizeof(RxPacket));
    _pair_result_queue = xQueueCreate(PAIR_RESULT_QUEUE_DEPTH, sizeof(RxPacket));
#if ESP_IDF_VERSION_MAJOR < 5
    _rssi_queue = xQueueCreate(8, sizeof(RssiSample));
#endif
    if (!_rx_packet_queue || !_pairing_queue || !_pair_result_queue
#if ESP_IDF_VERSION_MAJOR < 5
        || !_rssi_queue
#endif
    ) {
        dbg_println("ESP-NOW: failed to create receive queues");
        delete_receive_queues();
        return;
    }

    if (esp_now_init() != ESP_OK) {
        dbg_println("ESP-NOW: init failed");
        delete_receive_queues();
        return;
    }

    {
        uint8_t pmk[16];
        ESPNowCrypto::derivePmk(pmk);
        esp_err_t pmk_result = esp_now_set_pmk(pmk);
        ESPNowCrypto::secureZero(pmk, sizeof(pmk));
        if (pmk_result != ESP_OK) {
            dbg_println("ESP-NOW: failed to configure PMK");
            esp_now_deinit();
            delete_receive_queues();
            return;
        }
    }

    if (esp_now_register_recv_cb(on_data_recv) != ESP_OK ||
        esp_now_register_send_cb(on_data_sent) != ESP_OK) {
        dbg_println("ESP-NOW: failed to register callbacks");
        esp_now_deinit();
        delete_receive_queues();
        return;
    }

    reset_link_session();

#if ESP_IDF_VERSION_MAJOR < 5
    {
        wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
        esp_wifi_set_promiscuous_filter(&filt);
        esp_wifi_set_promiscuous_rx_cb(promiscuous_rx_cb);
        esp_wifi_set_promiscuous(true);
    }
#endif

    Preferences prefs;
    bool prefs_open = prefs.begin(PREF_NAMESPACE, false);
    if (prefs_open && load_active_profile(prefs)) {
        _op_channel = _preferred_channel;
        prefs.end();

        if (esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE) == ESP_OK &&
            register_peer(_peer_mac, _op_channel, true, _lmk)) {
            dbg_printf("ESP-NOW: loaded pairing, ch=%d\n", _op_channel);
        } else {
            dbg_println("ESP-NOW: failed to restore saved pairing");
            memset(_peer_mac, 0, sizeof(_peer_mac));
            ESPNowCrypto::secureZero(_lmk, sizeof(_lmk));
        }
    } else {
        if (prefs_open) prefs.end();
        dbg_println("ESP-NOW: no saved pairing");
    }

    _espnow_ready = true;
    if (has_selected_profile()) {
        begin_reconnecting();
    } else {
        set_link_state(LinkState::Unpaired);
    }
}

void espnow_poll() {
    if (!_espnow_ready) return;
    uint32_t now = millis();

#if ESP_IDF_VERSION_MAJOR < 5
    RssiSample sample;
    while (_rssi_queue && xQueueReceive(_rssi_queue, &sample, 0) == pdTRUE) {
        if (has_selected_profile() && mac_eq(sample.mac, _peer_mac)) {
            update_rssi(sample.rssi);
        }
    }
#endif

    RxPacket packet;
    while (_pair_result_queue &&
           xQueueReceive(_pair_result_queue, &packet, 0) == pdTRUE) {
        process_rx_packet(packet);
    }

    size_t processed = 0;
    while (_rx_packet_queue &&
           processed < MAX_RX_PACKETS_PER_POLL &&
           xQueueReceive(_rx_packet_queue, &packet, 0) == pdTRUE) {
        process_rx_packet(packet);
        ++processed;
    }

    if (_pairing_queue &&
        (uint32_t)(now - _last_pairing_packet_ms) >= PAIRING_PACKET_INTERVAL_MS &&
        xQueueReceive(_pairing_queue, &packet, 0) == pdTRUE) {
        _last_pairing_packet_ms = now;
        process_rx_packet(packet);
    }

    uint8_t rx_channel = _rx_channel_hint.exchange(0, std::memory_order_acq_rel);
    if (!pairing_active() && rx_channel >= 1 && rx_channel <= 14 && rx_channel != _op_channel) {
        if (esp_wifi_set_channel(rx_channel, WIFI_SECOND_CHAN_NONE) == ESP_OK &&
            (!has_selected_profile() || register_peer(_peer_mac, rx_channel, true, _lmk))) {
            _op_channel = rx_channel;
        }
    }

    now = millis();

    if (_link_state == LinkState::Connected &&
        (uint32_t)(now - _last_rx_ms) > CONNECT_TIMEOUT_MS) {
        begin_reconnecting();
    }

    static LinkState processed_state = LinkState::Unpaired;
    if (_link_state != processed_state) {
        LinkState previous_state = processed_state;
        processed_state = _link_state;

        if (_link_state == LinkState::Connected &&
            previous_state != LinkState::Connected) {
            WiFi.setSleep(false);
            esp_wifi_set_ps(WIFI_PS_NONE);
            bool channel_changed = _preferred_channel != _op_channel;
            _preferred_channel = _op_channel;

            if (channel_changed) {
                Preferences prefs;
                if (prefs.begin(PREF_NAMESPACE, false)) {
                    if (!store_active_profile(prefs,
                                              _peer_mac,
                                              _lmk,
                                              _preferred_channel,
                                              _peer_hostname)) {
                        dbg_println("ESP-NOW: failed to update saved channel");
                    }
                    prefs.end();
                }
            }
            dbg_printf("ESP-NOW: connected on ch=%d (RSSI %d dBm)\n",
                       _op_channel, (int)_peer_rssi);
            fnc_realtime(StatusReport);
        }

        peerlink_request_redraw();
    }

    if (state == Disconnected) {
        static uint32_t _badge_tick_ms = 0;
        if ((now - _badge_tick_ms) >= 400) {
            _badge_tick_ms = now;
            peerlink_request_redraw();
        }
    }

    if (_link_state == LinkState::Reconnecting) {
        if ((now - _reconnect_beacon_ms) >= RECONNECT_PROBE_MS) {
            _reconnect_beacon_ms = now;

            uint8_t ch;
            if (_reconnect_probe_idx < 3 && _reconnect_saved_ch >= 1 && _reconnect_saved_ch <= 13) {
                ch = _reconnect_saved_ch;
            } else {
                uint8_t sweep_idx = (uint8_t)((_reconnect_probe_idx - 3) % 13);
                ch = PROBE_ORDER[sweep_idx];
            }
            _reconnect_probe_idx++;

            if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) == ESP_OK &&
                register_peer(_peer_mac, ch, true, _lmk)) {
                _op_channel = ch;
                send_keepalive(_peer_mac);
            }
        }
        return;
    }

    if (_link_state == LinkState::Synchronizing) {
        if ((uint32_t)(now - _link_state_since_ms) >= SYNCHRONIZE_TIMEOUT_MS) {
            begin_reconnecting();
            return;
        }
        if ((uint32_t)(now - _synchronize_last_tx_ms) >= SYNCHRONIZE_RETRY_MS) {
            _synchronize_last_tx_ms = now;
            _synchronize_authenticated_tx =
                send_keepalive(_peer_mac) || _synchronize_authenticated_tx;
        }
        return;
    }

    if (_link_state == LinkState::Connected &&
        (now - _keepalive_last_ms) >= KEEPALIVE_INTERVAL_MS) {
        _keepalive_last_ms = now;
        send_keepalive(_peer_mac);
        return;
    }

    if (!pairing_active()) {
        return;
    }

    if (_link_state == LinkState::Confirming) {
        static uint32_t last_result_wait_log_ms = 0;
        if (!_pairing_completion_pending &&
            (uint32_t)(now - last_result_wait_log_ms) >= 1000) {
            last_result_wait_log_ms = now;
            dbg_printf("ESP-NOW: awaiting pairing result (rx=%lu queue_fail=%lu)\n",
                       (unsigned long)_pair_result_rx_count.load(std::memory_order_relaxed),
                       (unsigned long)_pair_result_queue_failures.load(std::memory_order_relaxed));
        }

        if (_pairing_completion_pending) {
            if (_pairing_completion_attempts < PAIR_COMPLETE_ATTEMPTS &&
                (uint32_t)(now - _pairing_last_completion_ms) >= PAIR_COMPLETE_RETRY_MS) {
                ++_pairing_completion_attempts;
                _pairing_last_completion_ms = now;
                esp_now_send(
                    _pairing_peer_mac,
                    reinterpret_cast<const uint8_t*>(&_pairing_complete_packet),
                    sizeof(_pairing_complete_packet));
                return;
            }
            if (_pairing_completion_attempts >= PAIR_COMPLETE_ATTEMPTS &&
                (uint32_t)(now - _pairing_last_completion_ms) >= PAIR_COMPLETE_RETRY_MS) {
                PairResultV4Pkt result;
                memcpy(&result, &_pairing_result, sizeof(result));
                bool completed = complete_pairing_from_result(result);
                ESPNowCrypto::secureZero(&result, sizeof(result));
                if (!completed) {
                    _pairing_completion_pending = false;
                }
                return;
            }
            return;
        }

        if ((uint32_t)(now - _pairing_await_result_ms) >= PAIR_RESULT_TIMEOUT_MS) {
            clear_temporary_pairing_peer();
            ESPNowCrypto::secureZero(_pairing_lmk, sizeof(_pairing_lmk));
            set_link_state(LinkState::Discovering);
            _beacon_last_ms = 0;
            return;
        }

        if ((uint32_t)(now - _pairing_last_confirm_ms) >= PAIR_CONFIRM_RETRY_MS) {
            _pairing_last_confirm_ms = now;
            esp_wifi_set_channel(_pairing_peer_channel, WIFI_SECOND_CHAN_NONE);
            esp_now_send(_pairing_peer_mac,
                         reinterpret_cast<const uint8_t*>(&_pairing_confirm),
                         sizeof(_pairing_confirm));
        }
        return;
    }

    if (_link_state == LinkState::Discovering &&
        (now - _beacon_last_ms) >= BEACON_INTERVAL_MS) {
        _beacon_last_ms = now;
        bool broadcast_ready = true;

        _probe_idx  = (_probe_idx + 1) % 13;
        _op_channel = PROBE_ORDER[_probe_idx];
        broadcast_ready =
            esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE) == ESP_OK;

        remove_broadcast_peer();
        esp_now_peer_info_t bp = {};
        memcpy(bp.peer_addr, BROADCAST_MAC, 6);
        bp.channel = _op_channel;
        bp.encrypt = false;
        bp.ifidx   = WIFI_IF_STA;
        broadcast_ready = broadcast_ready && esp_now_add_peer(&bp) == ESP_OK;

        uint8_t my_mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, my_mac);

        if (!_pairing_keypair_valid) {
            _pairing_keypair_valid =
                ESPNowCrypto::generateEcdhKeypair(_pairing_private_key, _pairing_public_key);
        }
        if (_pairing_keypair_valid && broadcast_ready) {
            DiscoveryV4Pkt pkt;
            build_discovery_v4(pkt, my_mac, _op_channel);
            esp_now_send(BROADCAST_MAC, (const uint8_t*)&pkt, sizeof(pkt));
            ESPNowCrypto::secureZero(&pkt, sizeof(pkt));
        }
    }
}

void espnow_putchar(uint8_t c) {
    if (!_espnow_ready) return;
    if (c == 0x11 || c == 0x13) return;

    if (c == 0x18 || (c >= 0x80 && c <= 0x9F) || (c >= 0xB0 && c <= 0xB3)) {
        if (_link_state == LinkState::Connected) send_realtime(c);
        return;
    }
    // '?' / '!' / '~' are GRBL realtime commands: they are out-of-band by
    // definition and must NEVER be queued into the line buffer.
    //
    // This used to be gated on `_tx_len == 0`.  Core 0 emits the '?' status poll
    // while Core 1 pushes line commands, so whenever a partial line was buffered
    // the '?' fell through and was appended INTO that command — losing the status
    // request and corrupting the command.  With no status reports the pendant
    // stayed on "Syncing" indefinitely, and a subsequent newline-terminated
    // command (e.g. $H) flushed the buffer and made it briefly work again, which
    // is exactly the reported "homing unsticks it" behaviour.
    if (c == '?') {
        if (_link_state == LinkState::Connected) {
            send_status_poll();
        } else {
            _pending_status_poll = true;   // replayed on the Connected edge
        }
        return;
    }
    // '!' (feed hold) and '~' (cycle start) stay on the out-of-band realtime
    // packet, which FluidNC pushes to the FRONT of its receive queue so a stop
    // never queues behind jog traffic.  They are dropped rather than deferred
    // when the link is down: a feed hold arriving late, after a reconnect, is
    // worse than one that never arrives.
    if (c == '!' || c == '~') {
        if (_link_state == LinkState::Connected) send_realtime(c);
        return;
    }

    _tx_buf[_tx_len++] = c;

    if (c == '\n' || _tx_len >= TX_BUF_SIZE - 1) {
        size_t flush_len = (size_t)_tx_len;
        _tx_len = 0;

        send_fragments(_tx_buf, flush_len);
    }
}

int espnow_getchar() {
    return rx_pop();
}

bool espnow_rx_available() {
    return _rx_head.load(std::memory_order_acquire) != _rx_tail;
}

bool        espnow_is_paired()      { return has_selected_profile(); }
bool        espnow_is_connected()   { return _link_state == LinkState::Connected; }
bool        espnow_is_reconnecting(){
    return _link_state == LinkState::Synchronizing ||
           _link_state == LinkState::Reconnecting;
}
int8_t      espnow_rssi()           { return _peer_rssi; }

int espnow_signal_bars() {
    if (_link_state != LinkState::Connected) return 0;
    int8_t r = _peer_rssi;
    if (r >= -55) return 4;
    if (r >= -65) return 3;
    if (r >= -75) return 2;
    if (r >= -85) return 1;
    return 0;
}

const char* espnow_status_str() {
    switch (_link_state) {
        case LinkState::Unpaired:      return "Not Paired";
        case LinkState::Discovering:   return "Discovering...";
        case LinkState::Confirming:    return "Confirming...";
        case LinkState::Synchronizing: return "Synchronizing...";
        case LinkState::Connected:     return "Connected";
        case LinkState::Reconnecting:  return "Searching...";
    }
    return "Not Paired";
}

void espnow_start_pairing() {
    if (!_espnow_ready) {
        dbg_println("ESP-NOW: pairing unavailable");
        return;
    }
    reset_receive_queues();
    _pair_result_rx_count.store(0, std::memory_order_relaxed);
    _pair_result_queue_failures.store(0, std::memory_order_relaxed);
    clear_temporary_pairing_peer();
    clear_pairing_secrets();
    if (has_selected_profile() && esp_now_is_peer_exist(_peer_mac)) {
        esp_now_del_peer(_peer_mac);
    }

    reset_link_session();

    _probe_idx    = 12;
    _op_channel   = PROBE_ORDER[0];
    esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);

    remove_broadcast_peer();

    ESPNowCrypto::derivePairingWindowLmk(_pairing_window_lmk);
    _pairing_keypair_valid =
        ESPNowCrypto::generateEcdhKeypair(_pairing_private_key, _pairing_public_key);
    new_pairing_session_id();

    set_link_state(LinkState::Discovering);
    _pairing_complete = false;
    _beacon_last_ms   = 0;

    dbg_println("ESP-NOW: pairing window started");
}

// FluidDial-CYD addition: close the link cleanly before deep sleep so the
// controller sees a departure rather than waiting out a keepalive timeout.
// Deliberately does NOT clear the stored pairing — the profile must survive so
// the pendant reconnects on the next wake.
void espnow_graceful_disconnect() {
    if (!_espnow_ready) return;
    reset_receive_queues();
    // Reconnecting (not Unpaired) — the stored profile stays valid and the link
    // re-establishes on wake; Unpaired would imply the pairing itself was lost.
    set_link_state(LinkState::Reconnecting);
    dbg_println("ESP-NOW: link closed for sleep");
}

void espnow_cancel_pairing() {
    if (!_espnow_ready) {
        set_link_state(LinkState::Unpaired);
        _pairing_complete = false;
        clear_pairing_secrets();
        return;
    }
    reset_receive_queues();
    clear_temporary_pairing_peer();
    _pairing_complete = false;
    clear_pairing_secrets();
    remove_broadcast_peer();

    if (has_selected_profile()) {
        _op_channel = _preferred_channel;
        if (esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE) != ESP_OK ||
            !register_peer(_peer_mac, _op_channel, true, _lmk)) {
            dbg_println("ESP-NOW: failed to restore selected machine");
        }
        begin_reconnecting();
    } else {
        set_link_state(LinkState::Unpaired);
    }
    dbg_println("ESP-NOW: pairing cancelled");
}

bool        espnow_pairing_complete() { return _pairing_complete; }

bool espnow_has_saved_pairing() {
    Preferences prefs;
    if (!prefs.begin(PREF_NAMESPACE, false)) return false;
    StoredMachineProfileList list;
    bool has = load_profiles(prefs, list) && list.count > 0;
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();
    return has;
}

size_t espnow_profile_count() {
    Preferences prefs;
    if (!prefs.begin(PREF_NAMESPACE, false)) return 0;
    StoredMachineProfileList list;
    size_t count = load_profiles(prefs, list) ? list.count : 0;
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();
    return count;
}

int espnow_active_profile_index() {
    Preferences prefs;
    if (!prefs.begin(PREF_NAMESPACE, false)) return -1;
    StoredMachineProfileList list;
    int active = load_profiles(prefs, list) && list.count > 0 ? (int)list.active : -1;
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();
    return active;
}

bool espnow_get_profile(size_t index, ESPNowProfileInfo& out) {
    Preferences prefs;
    if (!prefs.begin(PREF_NAMESPACE, false)) {
        memset(&out, 0, sizeof(out));
        return false;
    }
    StoredMachineProfileList list;
    bool ok = load_profiles(prefs, list) && index < list.count;
    if (ok) {
        profile_to_info(list, (uint8_t)index, out);
    } else {
        memset(&out, 0, sizeof(out));
    }
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();
    return ok;
}

bool espnow_select_profile(size_t index) {
    // Same reasoning as espnow_remove_profile(): the stored selection is NVS
    // state and must stay editable even when the radio is down.
    Preferences prefs;
    if (!prefs.begin(PREF_NAMESPACE, false)) return false;
    StoredMachineProfileList list;
    if (!load_profiles(prefs, list) || index >= list.count) {
        ESPNowCrypto::secureZero(&list, sizeof(list));
        prefs.end();
        return false;
    }

    list.active = (uint8_t)index;

    if (!_espnow_ready) {
        // Radio down: persist the choice and stop.  The activation path below
        // touches esp_wifi/esp_now directly, which is not safe before
        // esp_now_init(), and it gates persistence on that activation — so
        // without this branch the selection would be silently discarded.
        const bool ok = prefs.putBytes("profiles", &list, sizeof(list)) == sizeof(list);
        ESPNowCrypto::secureZero(&list, sizeof(list));
        prefs.end();
        return ok;
    }

    const StoredMachineProfile& profile = list.profiles[list.active];
    uint8_t new_mac[6];
    uint8_t new_lmk[16];
    char new_hostname[ESPNOW_HOSTNAME_SIZE];
    memcpy(new_mac, profile.mac, sizeof(new_mac));
    memcpy(new_lmk, profile.lmk, sizeof(new_lmk));
    strlcpy(new_hostname, profile.hostname, sizeof(new_hostname));
    uint8_t new_channel = profile.channel;

    uint8_t old_mac[6];
    memcpy(old_mac, _peer_mac, sizeof(old_mac));
    uint8_t old_channel = _preferred_channel;
    bool had_old_profile = has_selected_profile();

    reset_receive_queues();
    clear_temporary_pairing_peer();
    clear_pairing_secrets();
    remove_broadcast_peer();

    bool activated = esp_wifi_set_channel(new_channel, WIFI_SECOND_CHAN_NONE) == ESP_OK &&
                     register_peer(new_mac, new_channel, true, new_lmk);
    bool persisted = activated &&
                     prefs.putBytes("profiles", &list, sizeof(list)) == sizeof(list);
    if (!persisted) {
        if (activated && !mac_eq(new_mac, old_mac)) {
            esp_now_del_peer(new_mac);
        }
        if (had_old_profile) {
            esp_wifi_set_channel(old_channel, WIFI_SECOND_CHAN_NONE);
            register_peer(_peer_mac, old_channel, true, _lmk);
            begin_reconnecting();
        } else {
            set_link_state(LinkState::Unpaired);
        }
        ESPNowCrypto::secureZero(new_lmk, sizeof(new_lmk));
        ESPNowCrypto::secureZero(&list, sizeof(list));
        prefs.end();
        return false;
    }

    if (had_old_profile && !mac_eq(old_mac, new_mac)) {
        esp_now_del_peer(old_mac);
    }

    memcpy(_peer_mac, new_mac, sizeof(_peer_mac));
    memcpy(_lmk, new_lmk, sizeof(_lmk));
    _preferred_channel = new_channel;
    _op_channel = _preferred_channel;
    strlcpy(_peer_hostname, new_hostname, sizeof(_peer_hostname));

    ESPNowCrypto::secureZero(new_lmk, sizeof(new_lmk));
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();

    _pairing_complete = false;
    _tx_len = 0;
    begin_reconnecting();

    dbg_printf("ESP-NOW: selected profile %u - peer %02x:%02x:%02x:%02x:%02x:%02x host=%s ch=%d\n",
               (unsigned)index + 1,
               _peer_mac[0], _peer_mac[1], _peer_mac[2],
               _peer_mac[3], _peer_mac[4], _peer_mac[5], _peer_hostname, _op_channel);
    return true;
}

bool espnow_remove_profile(size_t index) {
    // Deliberately NOT gated on _espnow_ready.  The profile list lives in NVS,
    // so editing it needs no radio — and gating it meant that when the radio
    // failed to come up, Forget silently did nothing and the machine stayed in
    // the list, which reads as a broken button rather than a broken radio.
    // The radio-side teardown below is gated instead.
    Preferences prefs;
    if (!prefs.begin(PREF_NAMESPACE, false)) return false;

    StoredMachineProfileList list;
    if (!load_profiles(prefs, list) || index >= list.count) {
        ESPNowCrypto::secureZero(&list, sizeof(list));
        prefs.end();
        return false;
    }

    const bool removed_active = index == list.active;
    uint8_t removed_mac[6];
    memcpy(removed_mac, list.profiles[index].mac, sizeof(removed_mac));

    for (size_t i = index; i + 1 < list.count; ++i) {
        list.profiles[i] = list.profiles[i + 1];
    }
    --list.count;
    memset(&list.profiles[list.count], 0, sizeof(list.profiles[list.count]));

    if (list.count == 0) {
        list.active = 0;
    } else if (removed_active) {
        list.active = (uint8_t)(index < list.count ? index : list.count - 1);
    } else if (index < list.active) {
        --list.active;
    }

    StoredMachineProfile next_profile = {};
    if (removed_active && list.count > 0) {
        memcpy(&next_profile, &list.profiles[list.active], sizeof(next_profile));
    }

    bool stored = prefs.putBytes("profiles", &list, sizeof(list)) == sizeof(list);
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();
    if (!stored) {
        ESPNowCrypto::secureZero(&next_profile, sizeof(next_profile));
        return false;
    }

    if (!_espnow_ready) {
        // NVS updated; there is no live radio state to unwind.
        ESPNowCrypto::secureZero(&next_profile, sizeof(next_profile));
        return true;
    }

    if (esp_now_is_peer_exist(removed_mac)) {
        esp_now_del_peer(removed_mac);
    }
    if (!removed_active) {
        ESPNowCrypto::secureZero(&next_profile, sizeof(next_profile));
        return true;
    }

    reset_receive_queues();
    clear_temporary_pairing_peer();
    clear_pairing_secrets();
    remove_broadcast_peer();
    reset_link_session();
    _pairing_complete = false;
    _peer_rssi = -100;
    _tx_len = 0;

    if (next_profile.version == PROFILE_STORE_VERSION) {
        memcpy(_peer_mac, next_profile.mac, sizeof(_peer_mac));
        memcpy(_lmk, next_profile.lmk, sizeof(_lmk));
        _preferred_channel = next_profile.channel;
        _op_channel = _preferred_channel;
        strlcpy(_peer_hostname, next_profile.hostname, sizeof(_peer_hostname));

        esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);
        register_peer(_peer_mac, _op_channel, true, _lmk);
        begin_reconnecting();
    } else {
        memset(_peer_mac, 0, sizeof(_peer_mac));
        ESPNowCrypto::secureZero(_lmk, sizeof(_lmk));
        memset(_peer_hostname, 0, sizeof(_peer_hostname));
        _preferred_channel = ESPNOW_PAIR_CHANNEL;
        _op_channel = ESPNOW_PAIR_CHANNEL;
        set_link_state(LinkState::Unpaired);
        set_disconnected_state();
    }

    ESPNowCrypto::secureZero(&next_profile, sizeof(next_profile));
    dbg_printf("ESP-NOW: removed machine profile %u\n", (unsigned)index + 1);
    return true;
}

void espnow_clear_pairing() {
    reset_receive_queues();
    if (_espnow_ready) {
        clear_temporary_pairing_peer();
        remove_broadcast_peer();
    }
    clear_pairing_secrets();
    if (_espnow_ready && has_selected_profile() && esp_now_is_peer_exist(_peer_mac)) {
        esp_now_del_peer(_peer_mac);
    }
    memset(_peer_mac, 0, 6);
    memset(_lmk, 0, 16);
    set_link_state(LinkState::Unpaired);
    _pairing_complete = false;
    _peer_rssi        = -100;

    _tx_len = 0;

    reset_link_session();

    Preferences prefs;
    if (prefs.begin(PREF_NAMESPACE, false)) {
        prefs.remove("profiles");
        prefs.end();
    }
    dbg_println("ESP-NOW: pairing cleared");
}

#else  // !USE_ESPNOW

// ESP-NOW is excluded from this build. Provide inert stubs so the rest of the
// firmware (transport dispatch, status UI, pairing scene) links and runs with
// ESP-NOW simply unavailable. wifi_get_transport() never reports ESPNOW in this
// configuration, so none of these are reached at runtime
void        espnow_init() {}
void        espnow_poll() {}
void        espnow_putchar(uint8_t) {}
int         espnow_getchar() { return -1; }
bool        espnow_rx_available() { return false; }
bool        espnow_is_paired() { return false; }
bool        espnow_is_connected() { return false; }
const char* espnow_status_str() { return ""; }
void        espnow_start_pairing() {}
void        espnow_cancel_pairing() {}
void        espnow_graceful_disconnect() {}
bool        espnow_pairing_complete() { return false; }
void        espnow_clear_pairing() {}
bool        espnow_has_saved_pairing() { return false; }
bool        espnow_is_reconnecting() { return false; }
int8_t      espnow_rssi() { return -100; }
uint32_t    espnow_rx_dropped() { return 0; }
uint32_t    espnow_rx_pkt_dropped() { return 0; }
uint32_t    espnow_rx_bytes()       { return 0; }
uint32_t    espnow_frag_aborted()   { return 0; }
bool        espnow_radio_ready()    { return false; }
int         espnow_signal_bars() { return 0; }
size_t      espnow_profile_count() { return 0; }
int         espnow_active_profile_index() { return -1; }
bool        espnow_get_profile(size_t, ESPNowProfileInfo& out) { memset(&out, 0, sizeof(out)); return false; }
bool        espnow_select_profile(size_t) { return false; }
bool        espnow_remove_profile(size_t) { return false; }

#endif  // USE_ESPNOW

#endif  // ARDUINO
