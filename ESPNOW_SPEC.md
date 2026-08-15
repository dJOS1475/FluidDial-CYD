# Spec: ESP-NOW transport for FluidDial-CYD (v2.2.0)

Status: **spec — no code written.**

## 1. Goal

Add ESP-NOW as a wireless transport with **full UART feature parity**. UART is the
reliability benchmark; every capability that works over UART must work identically
over ESP-NOW. WiFi is not the reference — it has been unreliable in practice (small
CYD antenna, WebSocket/port-80 contention), and several of its behaviours are
workarounds for its own problems rather than features to reproduce.

**The WiFi backend is removed in this release.** It has been unreliable in practice
and is not worth carrying as a third code path. Users who want it stay on **v2.1.x**,
which remains available and installable — a pinned release is a cleaner fallback than
a runtime toggle nobody should be choosing. UART remains the wired default and the
reliability reference throughout.

## 2. Why this is tractable

Three things make this much smaller than it looks.

**The controller side is already done and released.** FluidNC **v4.0.4** (2026-08-05)
ships ESP-NOW in the stock release — `FluidNC/esp32/ESPNow/` is on `bdring/FluidNC`
`main` (landed 2026-07-25), and the `$espnow/pair` command exists there. No FluidNC
fork is needed. *(Upstream FluidDial's `docs/ESP-NOW.md` still tells you to flash
`figamore/FluidNC feature/esp-now`; that doc predates the merge — the fork's last
commit is 2026-06-25. Ignore it.)*

**Upstream's transport layer maps 1:1 onto our Comms facade.**

| upstream `PeerLink.h` | our `Comms.h` |
|---|---|
| `espnow_init()` | backend init called from `comms_init()` |
| `espnow_poll()` | `comms_poll()` |
| `espnow_putchar(c)` | `comms_putchar(c)` |
| `espnow_getchar()` | `comms_getchar()` |

We already have `TransportForce` + NVS persistence + a function-pointer dispatch
built for exactly this. Adding a third backend is the case the facade was designed
for.

**`PeerLink.cpp` is self-contained.** 1878 lines whose only coupling to upstream's
architecture is a single `current_scene->reDisplay()` call. It uses `dbg_printf` /
`dbg_println` (we have both) and includes `FluidNCModel.h`, `Scene.h`, `System.h` —
all of which this fork still carries.

**ESP-NOW is a byte-stream transport, like UART.** That is the key insight for
parity: features that work over UART work over ESP-NOW for the same reason, because
both are "bytes in, bytes out" through `fnc_getchar()` / `fnc_putchar()`. Anything
that works on UART today needs no transport-specific work.

## 3. What we take from upstream

Copied close to verbatim:

| File | Lines | Notes |
|---|---|---|
| `PeerLink.cpp` / `.h` | ~1920 | transport, pairing, fragmentation, profiles |
| `ESPNowCrypto.cpp` / `.h` | ~320 | P-256 ECDH key exchange, per-peer keys |

Adaptations needed:
- Replace the one `current_scene->reDisplay()` with our screen's redraw call.
- Confirm `#ifdef USE_WIFI` / `#ifdef USE_ESPNOW` guards match our build flags.
- Route its `dbg_*` output through ours (already compatible — never raw `printf`,
  which on a wired build would inject into the FluidNC control stream).

**Not** copied: `ESPNowPairingScene`, `ESPNowMachineScene`, `TransportScene`. These
are written against the old `Scene` system that this fork replaced with
`PSCREEN_*` / `screen_*.cpp`. They must be rewritten in our idiom, not ported.

Useful capabilities the transport gives us for free: up to **5 stored machine
profiles** (select / forget), `espnow_rssi()`, `espnow_signal_bars()`,
`espnow_is_connected()`, `espnow_is_reconnecting()`, `espnow_status_str()`.

## 4. Transport wiring

1. `Comms.h` — add `COMMS_MODE_ESPNOW = 2` and `TFORCE_ESPNOW = 2`.
2. `Comms.cpp` — third branch in `comms_init()`; point the dispatchers at
   `espnow_putchar` / `espnow_getchar` / `espnow_poll`.
3. `transport_force_label()` — add `"ESP-NOW"`.
4. NVS key `tport_force` already stores an int; value 2 is a clean extension. A
   pendant with no stored value still defaults to UART, so existing units are
   unaffected.
5. `platformio.ini` — `-DUSE_ESPNOW`. Note `PeerLink.cpp` is also guarded by
   `USE_WIFI` because ESP-NOW needs the WiFi radio initialised (not an AP/STA
   connection — just the PHY), so `USE_WIFI` must remain defined even in builds
   that never associate with an access point.

## 5. UART parity checklist

This is the core of the work. Every place the code currently branches on transport,
with the required ESP-NOW behaviour. **Default rule: ESP-NOW takes the UART path
unless listed below.** With WiFi deleted (§8 step 1) each of these becomes a simple
UART-or-ESP-NOW decision rather than a three-way branch.

| # | Site | Today | ESP-NOW should |
|---|---|---|---|
| 1 | `CNC_Pendant_UI.cpp:1032` — macros | WiFi → HTTP `wifi_http_get`; UART → `$File/SendJSON` | **take the UART path.** The HTTP fetch exists only because the WebSocket truncated the large `preferences.json` on port 80 — a WiFi-specific defect. The HTTP path is deleted with the WiFi backend, so this collapses to a single code path. |
| 2 | `:327` — battery icon | shown only in WiFi mode (mobile pendant) | **show it.** ESP-NOW is a battery/mobile mode too. Condition becomes "not UART". |
| 3 | `:370`, `:1614` — WiFi signal icon | WiFi RSSI bars, sampled every 500 ms | **draw an ESP-NOW link icon** from `espnow_signal_bars()` / `espnow_rssi()`, same cadence. Distinct glyph so the mode is obvious at a glance. |
| 4 | `:1881` — sleep eligibility | idle-sleep only in WiFi mode | **same as WiFi** — battery powered, so it should sleep when idle. |
| 5 | `:1558` — graceful disconnect before sleep | `wifi_graceful_disconnect()` | needs an ESP-NOW equivalent (stop the link cleanly; do **not** unpair). |
| 6 | `:1594` — 250 ms status poll | WiFi only: the WebSocket ignores `$Report/Interval`, so we poll | **verify, don't assume.** If FluidNC pushes status over ESP-NOW honouring `$Report/Interval`, this poll must NOT be enabled — it would add avoidable radio traffic. Test before deciding. |
| 7 | `SystemArduino.cpp:116` — `resetFlowControl()` | sends 0x11, plus `uart_backend_reset_flow_control()` for UART | decide whether PeerLink needs an equivalent ring/queue reset. Likely yes on reconnect. |
| 8 | `screen_wifi_setup.cpp:358` — transport toggle | 2-way UART ↔ WiFi | **UART ↔ ESP-NOW**, screen replaced by CONNECTION (§6a). Same 2-way shape, so no toggle rework — just different labels and targets. |
| 9 | `screen_fluidnc.cpp:126` — connection info | WiFi-specific fields | add an ESP-NOW section (peer MAC, channel, RSSI, profile name). |

Verified as needing **no** work — these already run over the byte stream and are
transport-agnostic:
- SD card file listing and job streaming
- Probing (all routines), jogging, homing, overrides, spindle control
- Status reports / DRO / alarm handling
- `$File/SendJSON` macro chain (the UART path, item 1)

### Jog flow control — the one thing to watch

`pending_nowait_sends >= 6` gates jog sends when FluidNC's planner is backed up.
It is transport-agnostic, but the WiFi backend resets it on (re)connect
(`WiFiConnection.cpp:300,315`). PeerLink must do the same on link
establishment/reconnect, or a stale count will silently throttle jogging after a
dropout.

ESP-NOW has per-packet ACKs and 238-byte fragmentation, so its latency profile
differs from UART. The dial-stop watchdog (adaptive 60–150 ms, v2.1.11) and this
gate were both tuned on UART — **re-validate continuous jogging on ESP-NOW
specifically**, since handwheel feel is the most latency-sensitive thing on the
pendant and the primary reason for wanting a reliable link.

## 6. Screens and pairing process

Three screens, all in this fork's idiom (shared `rowBtnX()`/`rowBtnWAt()` grid,
established colour language: orange = selected, green = go, blue = nav, teal =
config, indigo = secondary, yellow border = dial/active field).

### 6a. CONNECTION — replaces the WiFi Setup screen

With WiFi gone the transport choice is binary, so it becomes two large buttons
rather than a cycling toggle.

```
  CONNECTION
  ┌ TRANSPORT ─────────────────────┐
  │  [   UART   ] [  ESP-NOW  ]    │   orange = active
  ├ LINK ──────────────────────────┤
  │  Connected                 ▁▃▅ │   green / yellow / red
  │  Machine                  -58dB│
  │  Shapeoko                      │
  │  A4:CF:12:9B:00:1E  ch6        │
  └────────────────────────────────┘
  [  Pair New  ] [  Machines  ]
  [          Main Menu           ]
```

- Switching transport writes `tport_force` and prompts for restart (as today).
- LINK panel is live: state, machine name, peer MAC + channel, signal bars + RSSI.
- In UART mode the LINK panel shows baud/port instead and Pair/Machines are hidden.

### 6b. ESP-NOW PAIRING (`PSCREEN_ESPNOW_PAIR`)

The pairing action happens **on the controller**, which is unusual and non-obvious,
so the screen leads with the instructions rather than burying them.

```
  ESP-NOW PAIRING
  ┌ ON THE CONTROLLER ─────────────┐
  │  (1) Open FluidNC console      │   step dots turn green
  │  (2) Run:  $espnow/pair        │   as each completes
  │  (3) Keep pendant nearby       │
  │      or WebUI > Settings >     │
  │         ESP-NOW                │
  ├ STATUS ────────────────────────┤
  │  Waiting...                    │   → Pairing → Paired
  │  Listening for controller      │
  └────────────────────────────────┘
  [           Cancel             ]
  [            Back              ]
```

**States**

| State | Headline | Colour | Sub-line | Buttons |
|---|---|---|---|---|
| advertising | `Waiting...` | yellow | Listening for controller | Cancel · Back |
| handshaking | `Pairing` | cyan | Exchanging keys (P-256) | Cancel · Back |
| success | `Paired` | green | machine name + MAC/ch | **Use This Machine** · Machines |
| timeout | `Timed out` | red | No controller responded / Check FluidNC is v4.0.4+ | Retry · Cancel · Back |

The timeout sub-line naming the required FluidNC version matters — an older
controller is the most likely cause of failure and is otherwise invisible.

### 6c. MACHINES (`PSCREEN_ESPNOW_MACHINES`)

Backed by PeerLink's 5 profile slots.

```
  MACHINES
  ┌────────────────────────────────┐
  │ Shapeoko                   ▁▃▅ │  yellow border = active
  │ A4:CF:12:9B:00:1E           ok │
  │ ch6   ACTIVE                   │
  ├────────────────────────────────┤
  │ MPCNC                      ▁▃  │
  │ 3C:61:05:44:2A:B7           ok │
  │ ch1                            │
  ├────────────────────────────────┤
  │ Lathe                          │  greyed bars = not reachable
  │ 7C:9E:BD:03:11:C4          n/a │
  │ ch11                           │
  └────────────────────────────────┘
   Tap a machine to use it - 3/5 paired
  [  Pair New  ] [   Forget   ]
  [            Back              ]
```

- Tap a row → `espnow_select_profile()`, becomes ACTIVE, link re-establishes.
- **Forget** acts on the selected row and must confirm (reuse the probe screens'
  confirm overlay — and note the v2.1.10 lesson: the dial must be inert while that
  overlay is up).
- Rows show live reachability so a machine that is powered off is obvious before
  you switch to it.
- At 5/5 profiles, **Pair New** greys out with "Forget one first".

### 6d. First-run flow

```
  fresh flash ──► UART (safe default, no radio)
                    │
       user picks ESP-NOW on CONNECTION
                    │
                 restart
                    │
        ┌───── has stored pairing? ─────┐
       no                               yes
        │                                │
   PAIRING screen                 auto-connect active profile
   (auto-opened)                         │
        │                          ┌── reachable? ──┐
   $espnow/pair                   yes              no
        │                          │                │
     Paired ──► "Use This      running        MACHINES screen
                 Machine"                   (pick another / re-pair)
```

Key decisions:
- **UART stays the default on a fresh flash.** No radio starts unprompted, and a
  pendant with no pairing can never boot into an unusable state.
- The pairing screen **auto-opens** when ESP-NOW is selected but no profile is
  stored — otherwise the user faces a dead link with no obvious next step.
- If the active profile is unreachable at boot, go to MACHINES rather than sitting
  on a silent retry.

## 7. Removing WiFi — what it actually frees

Measured from the linked ELF of the current v2.1.11 build (1,336,480 B, 51.0% of the
2.5 MB app partition), not from source line counts:

| Subsystem | Bytes | Fate |
|---|---:|---|
| TLS handshake / record / RSA / AES | 49,700 | **removed** |
| Arduino WiFi (Client/Server/AP/STA, DNSServer, WebServer) | 28,679 | **removed** |
| WebSocket client | 14,537 | **removed** |
| x509 + cert bundle | 8,962 | **removed** |
| **Removable subtotal** | **101,878** (~99 KB) | |
| `esp_wifi` radio driver | 74,973 | **stays** — ESP-NOW needs the PHY |
| mbedtls ECP / ECDH / SHA-256 / HMAC | 16,241 | **stays** — ESPNowCrypto uses P-256 |

Plus `WiFiConnection.cpp` (1502 lines) and the 4.6 KB of captive-portal HTML
literals, minus whatever PeerLink + ESPNowCrypto add back (~2200 lines of new
source).

**Net saving: roughly 60–90 KB**, or 2–4% of the app partition. Note that this does
not free "all of WiFi" — the radio driver and the elliptic-curve crypto are
prerequisites for ESP-NOW itself and stay regardless.

The stronger argument for removal is not flash but **simplification**: every
transport branch in §5 is currently WiFi-vs-UART. Deleting WiFi turns each of them
into a single ESP-NOW-or-UART decision instead of a three-way one, and collapses the
macro fetch to one code path. That is why removal comes first in the phasing (§8)
rather than last.

### What removal deletes

- `src/WiFiConnection.cpp` / `.h`
- `src/screens/screen_wifi_setup.cpp` → replaced by the Connection screen (§6a)
- The `links2004/WebSockets` dependency in `platformio.ini`
- `wifi_http_get()` and the HTTP macro path in `FileParser.cpp` (~80 lines) — the
  `$File/SendJSON` UART path becomes the only path
- The `USE_WIFI` conditional in `requestMacros()` collapses to one code path
- Captive portal, credentials storage, AP mode, `wifi_*` status plumbing

### Migration risk

Users on WiFi today lose that transport on upgrade; **v2.1.11 remains the supported
build for them** and should be called out in the release notes and README.

The one thing that must not be left to chance: `tport_force` = 1 (WiFi) becomes a
stale value in NVS. **On boot, any unknown transport value must fall back to UART**,
not fail — a pendant that boots into a dead transport has no UI left to change it
and needs a reflash to recover. This is a three-line guard in `comms_init()`; add it
in the same commit that deletes the backend, not afterwards.

## 8. Phasing

WiFi removal comes **first**, not last. Doing it up front means every transport
branch in §5 gets rewritten once (UART vs ESP-NOW) instead of twice (UART vs WiFi vs
ESP-NOW), and the pendant stays fully working on UART throughout — UART is the
reliability reference anyway, so there is no window where the device is unusable.

1. **Remove WiFi.** Delete `WiFiConnection.*`, `screen_wifi_setup.cpp`, the
   `WebSockets` dependency, `wifi_http_get()` and the HTTP macro path. Collapse
   `requestMacros()` to the `$File/SendJSON` chain. Add the stale-`tport_force` → UART
   guard (§7). **Ship-able state: a UART-only pendant with every feature intact.**
   This alone is a valid checkpoint and should build, run and pass a jog/probe pass.
2. **Transport bring-up.** PeerLink + ESPNowCrypto in, `TFORCE_ESPNOW` wired,
   throwaway pairing screen. Prove `$espnow/pair` against stock FluidNC v4.0.4 and
   confirm bytes flow both ways. Validates the whole premise cheaply.
3. **Parity pass.** Work §5 top to bottom — icons, sleep, graceful disconnect,
   status polling, flow-control reset. Smaller now that WiFi is gone.
4. **Screens.** CONNECTION, PAIRING, MACHINES per §6, plus the first-run flow (§6d).
5. **Jog validation.** Continuous jogging, dial-stop latency, soft-limit clamp under
   ESP-NOW; re-tune the v2.1.11 watchdog constants if the latency profile differs.
   This is the acceptance gate — if handwheel feel is worse than UART, stop and fix
   it before shipping.
6. **Field test.** Macros (`macrocfg.json` over `$File/SendJSON`; the WebUI v3
   `preferences.json` format is not supported), SD
   job streaming, probing, and range/dropout behaviour measured against UART.

Steps 1 and 2 are independent enough to land as separate releases if wanted —
v2.2.0 could be "UART-only, WiFi removed" and v2.3.0 "ESP-NOW added". That splits
the risk and gets the simplification benefit immediately.

## 9. Open questions

1. Does FluidNC honour `$Report/Interval` over ESP-NOW, or is polling needed? (§5
   item 6 — affects radio traffic and battery life.)
2. Channel behaviour: ESP-NOW rides FluidNC's operational WiFi channel. What happens
   when the controller's WiFi is **off**, or changes channel? PeerLink stores a
   channel per profile, so re-pairing may be needed after a controller WiFi change.
3. Range and dropout behaviour with the CYD's small antenna — ESP-NOW should beat
   WiFi (no association, no DHCP, no TCP retransmit stalls), but this needs
   measuring, since antenna quality was the original complaint.
4. Does ESP-NOW coexist with FluidNC serving its WebUI simultaneously?
5. Power draw vs UART — matters for the battery/sleep behaviour in §5 items 4–5.

## 10. Effort summary

- **Ship-able on its own:** step 1 (WiFi removal) — deletion plus one NVS guard,
  leaves a fully working UART pendant.
- **Small / low risk:** transport wiring (§4); macro parity falls out for free once
  the HTTP path is gone; most of §5.
- **Medium:** the three screens (§6) — routine work in an established pattern.
- **Highest risk:** jog latency and handwheel feel under ESP-NOW (§5, phase 5) and
  the open questions in §9. All are answerable in phase 2 against a real controller,
  before the UI work is invested.

**Acceptance bar:** ESP-NOW must feel no worse than UART for jogging and probing.
UART is the reference, and if ESP-NOW cannot meet it the honest outcome is a
UART-only v2.2.0 (which step 1 already delivers) rather than shipping a wireless
link with the same reputation WiFi earned.
