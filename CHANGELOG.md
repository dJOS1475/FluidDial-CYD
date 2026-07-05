# FluidDial-CYD: 

**ChangeLog:**


**2026-07-06**

v2.1.7
* Change: **Boss shape is now toggled by tapping the diagram** (a single tap cycles Circular ↔ Rectangular), replacing the hidden triple-tap on the first settings field. The diagram is bordered like a button with a "Tap: shape" hint — since the diagram is itself the mode indicator, tapping it to change the shape reads naturally, and the first settings field goes back to plain single-tap-to-focus (no more fighting the focus toggle).

**2026-07-05**

v2.1.6
* Fix: **handwheel jog-stop now works at every increment.** In v2.1.5 the dial-stop JogCancel only fired for coarse (10 mm+) jogs; at **1 mm and finer** the axis kept coasting after the dial stopped. Cause: a fast fine-increment spin produces more jog sends than FluidNC's planner accepts, so many were dropped by the flow-control gate *before* the continuous-spin detector ran — making the spin look intermittent and never arming the stop. The dial cadence is now recorded for **every** detent (before the flow-control drop), so a continuous spin is recognised and stopped on dial-stop regardless of increment. Single deliberate detents still complete their full move.

**2026-07-05**

v2.1.5
* Safety: **jogging now stops when the dial stops (handwheel/MPG behaviour).** A single deliberate detent completes its full move as before, but a **continuous spin** is halted the instant you stop turning — the pendant sends a real-time **JogCancel** (0x85) after ~150 ms of dial silence, which flushes FluidNC's queued jog moves so the axis stops where you stopped, instead of coasting through the commanded distance. Tunable via `JOG_CONTINUOUS_MS` / `JOG_STOP_MS`.
* Fix: **spindle/feed override no longer floods the controller.** Reaching a target % previously reset to 100% and fired up to ~25–50 real-time bytes in a tight loop, which could overrun a Modbus VFD's queue (`VFD Queue Full`, wrong landing) or starve FluidNC into a task-watchdog reboot. It now **anchors off the current reported %** and walks to the target with **coarse (±10%) + fine (±1%)** steps, **one byte every 60 ms** — e.g. 100→75% is 7 gentle bytes instead of 26, with no jump to 100% first. Works during a running job (realtime bytes only). Applies to both spindle and feed, buttons and dial.
* Add: **3D-probe deflection calibration on the Probe Config screen.** A new **Deflection Cal** panel with a **Gauge width** field (default 50.8 mm = the 2″ face of a 1-2-3 block) and a **Calibrate** button (with a confirm prompt). It touches the gauge top for a safe depth, probes the ±X faces from outside (10 mm clear, 5 mm below top — never plunging onto the part), and computes the deflection, showing the result to Apply/Cancel. It **never changes your work zero**. Deflection is now **added** to the ball radius (may be negative) and can also be entered manually.
* Change: **the Jog & Homing and Status DROs now show MACHINE coordinates** (MPos), matching the Work Area screen — homing and travel reasoning live in machine space. The Status position panel is relabelled **MACHINE POSITION**.

**2026-07-04**

v2.1.4
* Add: **Rectangular / square boss probing.** The Boss screen now handles rectangular features as well as round ones. **Triple-tap the first settings field** to switch between **Circular** boss (one *Nominal dia.*) and **Rectangular** boss (independent **X size** / **Y size**); the field labels and the diagram change to confirm the mode (rectangular shows a top-down view of the boss with inward ±X/±Y arrows and a Z0 centre mark). Centre-finding is the same opposed-pair midpoint (exact for a rectangle too) with X re-centred before the Y pair; the only difference is each axis's seek/clear distances are sized from its own half-dimension. Sets **X0/Y0/Z0** as before.
* Change: the boss **Probe depth** field is relabelled **Probe depth Z** to distinguish it from the new horizontal size fields (it always meant the Z depth below the boss top).

**2026-07-04**

v2.1.3
* Safety: **jogging can no longer be driven past the machine's soft limits.** The old per-tick distance cap only bounded a single encoder detent, so cumulative `$J=G91` jogs could still walk an axis into a hard stop. The pendant now clamps every jog so the resulting **absolute machine position** stays inside the homed travel envelope. The per-axis envelope sign is read from the controller's **`$23`** homing-direction mask (home = MPos 0; homes + → `[-$13x, 0]`, homes − → `[0, +$13x]`), so mixed homing directions are handled correctly with nothing assumed. A 0.5 mm margin keeps the tip off the switch at both ends. The clamp works against a **predicted** position that leads the reported MPos by queued-but-unexecuted jogs (re-seeded from the real MPos when a jog burst ends and the machine idles), so a fast continuous spin can't over-commit while FluidNC's status lags. Engages only for X/Y/Z once travel + `$23` are known and the machine isn't in Alarm; otherwise it falls through to FluidNC's own soft limits unchanged. **Recommended companion setting: enable FluidNC soft limits (`$20=1`, with homing `$22=1`) as the authoritative backstop.**
* Improve: **Bore/Boss now probe 4 points at 90° (±X, ±Y)** instead of 3 at 120°. The centre is the midpoint of each opposed pair — division-free, and it cancels the tip radius per axis. The **X pair is probed first and the tool re-centres in X before the Y pair**, so the Y touches run through the true vertical diameter and contact each wall head-on even from a well off-centre start. Same probe count as before (one extra re-centre move).
* Improve: **3D-probe Deflection is now applied.** The Probe Config screen drops the unused *Stylus length* and *Pre-travel* fields; the remaining **Deflection** value (default 0) is subtracted from the ball radius in every 3D-probe offset (Z Surface, Corner edges, Boss top) as an optional accuracy tune. Centre-finding (Bore/Boss) is unaffected — deflection cancels radially.
* UI: **colour + affordance cleanup.** Red is now reserved for warnings/alarms — editable field values use one consistent palette (cyan editable / yellow focused). Every tappable field gets a visible border (borderless = read-only, yellow border = focused), so buttons like the Work-Area chip no longer look read-only. The XYZ Corner screen was rebalanced (taller, evenly spaced fields). The Bore screen's positioning warning now matches the Boss screen's amber (was red).

**2026-07-01**

v2.1.2
* Fix: **Bore/Boss centre-finding was broken** — the centre-average expression used curly braces `{ }`, which FluidNC's parser rejects (it only groups with square brackets), so the final centre move errored out. Corrected to valid nested `[ ]`.
* Fix: **Bore/Boss computed the centre in the wrong coordinate frame** — FluidNC returns G38 probe results in **machine** coordinates, but the routines were treating them as work coordinates, so the found centre was off by the active work offset. The maths and the final move are now done in machine coordinates (`G53`).
* Improve: **Bore/Boss now use radial 3-point probing** — three points 120° apart, each approached **perpendicular to the wall** (no tangential skid), fitted to a circle (circumcentre). More accurate than the old ±X/±Y sweep, and exact even when you don't start dead-centre.
* Fix: **automatic work-zeroing now takes effect** — every routine (Z Surface, XYZ Corner, Bore, Boss) now **activates** the selected work coordinate system (G54–G57) before it probes, so the zero it sets is live in the system shown on screen; no manual re-zero.

**2026-06-27**

v2.1.1
* Fix: the **XYZ touch-plate corner probe now compensates for the plate's wall thickness** — previously the X/Y zero landed on the plate's outer face instead of the true workpiece edge. The **XY offset X/Y** config fields are now applied (in the approach direction, the same way the 3D probe's ball radius is), so the corner zeroes on the workpiece edge. The 3D-probe corner is unchanged.
* Docs: added a **Calibration** section to the [Probing guide](Probing.md) — how to dial in the effective ball radius (3D probe) and the plate offsets (touch plate) against a known reference.

**2026-06-26**

v2.1.0
* Probing — **crash-safe two-pass probing across all routines** (Z Surface, XYZ Corner, Bore, Boss): every wall/surface is reached with a **fast seek + slow re-probe** `G38.2` move — never a blind rapid — so a wrong nominal size can no longer drive the tip into a wall, and the slow second pass improves repeatability. The default **seek rate is now 500 mm/min** (≈19.7 in/min).
* Probing — **Bore** reworked: probes the walls with no Z motion at all (place the tip inside at any depth), **re-centres on X before the Y pair** so Y runs through the true diameter, and sets **X0/Y0 only** — Z work-zero is done separately with the Z Surface routine. Also fixed a latent **Boss** crash where the tip could be carried back over the feature between sides (now lifts straight up). Boss still touches the flat top to set **Z0**.
* Probing — **XYZ Corner now always probes X/Y/Z** (the Axes selector was removed).
* UI — **all four probe routine screens share one consistent layout**: a combined Sequence/Settings panel, a **probe-sequence list** and an at-a-glance **probe diagram** on the left, and a **Work Area (WCS) selector** on each screen — tap to cycle **G54→G57** for the system the probe will zero. The Z Surface, Corner, Bore and Boss screens were all brought in line with this style.

**2026-06-25**

v2.0.6
* Feature: **screen sleep** — battery (WiFi) pendants now blank the display (backlight off) after **15 minutes of inactivity** while the CNC is **Idle**, or while the pendant has been stuck **"Connecting"** that long. A **touch anywhere wakes it** back to the exact screen you left, and the waking touch sends **no command** to the controller. It never sleeps mid-job: any active machine state (Run/Jog/Hold/Home/Alarm) keeps it awake and resets the timer, and it wakes automatically if a job starts or an alarm fires while blanked. The WiFi Setup screen is excluded. **Wired (UART) pendants never sleep** — they're powered by the controller and have no battery, so they switch off with it.
* Fix: the **charging indicator no longer false-triggers for minutes after boot** on a battery-only pendant — the boot load spike sags the cell, and the recovery as that load eased was being misread as a charging trend. A post-boot settling window now holds the trend detector off until the cell voltage stops climbing (or a 3-minute cap), so the lightning bolt only appears when actually on charge.

**2026-06-14**

v2.0.5
* Feature: the **A axis (4th axis)** is treated as rotary on the Jog & Homing screen — the DRO shows a **degree symbol** (°, drawn as a small ring) instead of "mm"/"in", and the **"JOG INCREMENT (mm)" label reads "JOG INCREMENT (deg)"** when A is selected. X/Y/Z still show mm/in, and the alarm state still takes precedence over the unit.

**2026-06-14**

v2.0.4
* Fix: **wired (UART) pendants no longer have their feed rate drift down mid-job** — `dbg_printf()` wrote debug output to UART0 via `vprintf`, which on a wired build is the same port the pendant uses to reach FluidNC, so debug text was injected into the control stream (and read by the controller as feed-override realtime commands). `dbg_printf()` now routes through the gated debug path — silent in the shipped firmware and never on the controller UART; two stray raw `printf` calls were cleaned up the same way.

**2026-06-14**

v2.0.3
* Feature: **charging detection + indicator reworked** — charging is now inferred from the battery-voltage trend (a fast/slow EMA crossover, tuned for Li-Ion cells) instead of the IP5306's status bits, which proved unreliable on these boards; it reacts within ~30 s of plugging in. The indicator is now a **full-height yellow lightning bolt** overlaid on the battery icon, replacing the previous red outline.
* Docs: README WiFi transport corrected to **WebSocket** (was described as Telnet/TCP); added a battery **"off"-mode drain** note — deep sleep still draws a few mA, so charge after use.

**2026-06-14**

v2.0.2
* Fix: the WiFi **captive portal again starts on a clean build** — `comms_init()` was defaulting to UART whenever no transport override was stored in NVS (i.e. every fresh flash / factory reset), so a battery pendant silently picked UART and never raised the setup AP. It now autodetects from hardware when no override is stored (battery/IP5306 pendant → WiFi + captive portal, wired → UART); an explicit UART/WiFi choice from the WiFi Setup screen still overrides and persists. The transport toggle now flips the *active* transport so it points the right way after an autodetected default.
* Fix: the captive-portal **soft-AP now hands out DHCP leases** — `softAPConfig()` was called *after* `softAP()`, which left the on-AP DHCP server not serving, so a connecting phone/PC got a `169.254.x.x` (APIPA) address and couldn't reach `192.168.4.1`. The AP IP/subnet is now configured before `softAP()` brings the AP up.

v2.0.1
* UX: the probe-type label on the Probe config screens ("3D Touch Probe" / "Z-Height Touch Plate" / "XYZ Touch Plate") is now green instead of dim grey, for better legibility
* Fix: the web installer's **Update** path no longer silently wipes saved WiFi credentials — the manifest's `new_install_prompt_erase` was `false`, which (for a non-Improv device) makes ESP Web Tools full-chip-erase by default; set to `true` so Update routes to the erase prompt with the box unchecked, preserving NVS. The installer page and README now note to leave "Erase device" unchecked on an update

v2.0.0
* Feature: **probing rebuilt around three probe types** — the Probe screen now opens on a hub with a segmented type selector: **Z-Height Touch Plate**, **XYZ Touch Plate**, and **3D Touch Probe**; the routines on offer are gated by the selected type — Z-Height Plate exposes Z Surface only, XYZ Plate adds XYZ Corner, and the 3D Probe adds Bore and Boss
* Feature: probe routines now **generate their G-code on the pendant** (G38.2 straight-probe moves, with named-parameter arithmetic for circle centre-finding) instead of running pre-written `.nc` macro files from the controller; the trigger offset is computed per type — touch-plate thickness for plates, ball radius for the 3D probe — so the same routine zeroes correctly regardless of probe hardware
* Feature: new routine screens — **Z Surface** (Z-only zero), **XYZ Corner** (with cycle selectors for which corner and which of X/Y/Z to set), **Bore** (inside-circle centre find) and **Boss** (outside-circle centre find), each with a live sequence outline and tap-to-edit settings
* Feature: per-type **Configure** screen — 3D-probe stylus parameters (ball dia, stylus length, deflection, pre-travel) or touch-plate dimensions (thickness, plus width and XY offsets for the XYZ plate); a schematic illustration of the selected probe type is drawn on each config screen for reference
* Feature: a **shared settings** panel on the hub (probe rate, seek rate, retract, max Z travel) applies to every routine
* UX: probe routine screens share a consistent full-width **Back** button placement and drop the redundant "Probe" word from their titles; the hub's **Configure** button is teal so it stands apart from the blue navigation buttons; with the XYZ Plate type selected, the two routines are shown as full-width stacked buttons
* UX: probe warnings are **type-aware** — a 3D probe is reminded to verify the probe is connected, a touch plate to verify the plate clip; the touch-plate config screens no longer carry the clip warning (it now lives on the routine screen, where it matters)
* UX: **connection status is now staged** — the Main Menu and Status screens show "Connecting" while the link comes up, then "Syncing" while the controller's static configuration is fetched, then live machine state; previously the UI could sit on a bare "Connecting" indefinitely
* Fix: **random WiFi alarms, garbled commands and ~20 s input lag** eliminated — the transmit drain was folding realtime bytes (`?` `!` `~`) into the middle of a pending line command when one was mid-flight, corrupting both; realtime bytes are now always extracted and sent immediately
* Fix: **stuck "Connecting" when the controller was already idle** — sync completion was gated on a state-change report that never fires if the machine is already Idle; it now keys off DRO reports, which arrive on every status push
* Fix: **false "N/C" (not-connected) flashes** — a `[MSG:RST]` from the controller cleared the cached state without refreshing the human-readable status string; the status line is now kept in step on every DRO update
* Fix: **freeze / stuck connect on WiFi reconnect** — the post-reconnect setup block used blocking ack-waited sends; it now uses non-blocking sends so the UI never stalls on a flaky link
* Fix: **macros and SD file lists load reliably after a sync** — the controller file fetch was rewritten as a raw HTTP GET with a hard-bounded connect timeout and explicit error codes, replacing the client that could hang the connect
* Display: **panel backgrounds no longer have a green tint** — transient panels are now composited through a single 16-bit (rgb565) scratch sprite; the previous 8-bit (rgb332) path had only two bits of blue and crushed the near-neutral dark-gray panels toward green, and also tended to fail allocation on the heap-tight WiFi build (causing flicker)
* Feature: **WiFi signal-strength icon** in the top-left of the title bar (battery pendants) — four ascending bars dimmed to the current RSSI level, with "AP" shown while the captive portal is active
* UX: **battery charging indicator is now red** (was cyan) — a charging LiPo is far more obvious at a glance
* Reliability: WiFi reconnect hardened against router rate-limiting and dropped flow-control ACKs; a TCP staleness watchdog with a post-connect grace period recovers a silently-dead socket; small realtime sends are retried so jog flow control isn't lost
* UX: the adjustable on-screen controls now share one **consistent field style** — the Jog & Homing speed control, the Feeds & Speeds feed/spindle override dials, and the Spindle Control "Dial" toggle all use the same bordered tap-to-edit treatment as the Probe screens, highlighting yellow while their dial is active
* Feature: the pendant firmware version shown on the FluidNC screen now tracks a single `FIRMWARE_VERSION` constant (bumped per release) instead of the latest git tag, so it updates correctly with every release
* Internal: added a **browser-based UI simulator** (`simulator/`) that mirrors every screen's draw/touch code line-for-line for layout work without flashing hardware, plus a `sync.py` hash tracker that flags any firmware screen whose JS port has drifted

**2026-05-27**

v1.7.1
* Architecture: **modular Comms layer** — transport-specific code split into three files: `Comms.h` / `Comms.cpp` (facade that picks one backend at boot), `CommsUart.h` / `CommsUart.cpp` (UART hardware only — ESP-IDF UART driver, XON/XOFF flow control), and `WiFiConnection.h` / `WiFiConnection.cpp` (TCP/Telnet only); the two backends share no symbols and have no awareness of each other; `Comms.cpp` is the only translation unit that includes both `CommsUart.h` and `WiFiConnection.h`
* Architecture: `fnc_putchar` / `fnc_getchar` in `SystemArduino.cpp` reduced to one-line forwarders to `comms_putchar` / `comms_getchar`; per-character `wifi_use_uart_mode()` checks eliminated — the active backend is selected once by `comms_init()` and dispatched via a function pointer for the lifetime of the run; zero NVS reads, zero mode checks, zero branches in the hot path after init
* Feature: **hardware-driven transport selection** — `comms_init()` picks the active backend by autodetecting battery hardware: IP5306 PMIC present → WiFi backend, absent → UART backend; battery presence is the definitive signal of a mobile/wireless pendant, so the firmware no longer asks the user (or relies on an NVS preference) to pick the right transport
* Fix: SD card and macro loading no longer break on capacitive CYD builds where stale NVS held `uart_mode=false` — the new autodetect supersedes the NVS key entirely, eliminating the class of bug where a pendant could boot into WiFi mode without working credentials and silently swallow every UART-bound byte
* UX: WiFi setup screen rewritten — no more UART/WiFi toggle row (there is nothing to toggle); shows the auto-detected transport at the top, then either WiFi status + a "Reconfigure WiFi" action (battery pendants) or a short note explaining UART is in use (wired pendants); the "Cancel AP Setup" action now just stops the captive portal in place rather than switching transport modes
* UX: FluidNC screen's CONNECTION panel only shows the cyan tappable border and "WiFi >" affordance on battery pendants; wired pendants still see the panel as tappable for diagnostic info, but without the misleading WiFi hint
* Removal: deprecated `wifi_use_uart_mode()`, `wifi_set_uart_mode()`, `wifi_is_first_boot()` and the NVS keys `uart_mode` / `setup_done` — none of them have a role in a hardware-detected design; saved WiFi credentials (`ssid` / `pass` / `ip`) remain the only persisted state in the `fluidwifi` namespace
* Removal: `transport_init()` (introduced earlier this release) replaced by `comms_init()` which both decides the transport AND brings up the chosen backend in one call
* Diagnostic: `pendant_hw_task` now prints `Comms: active transport = UART|WiFi` over the USB serial console on every boot so the active backend is unambiguous

**2026-05-26**

v1.7.0
* Feature: **battery status icon** (capacitive CYD only) — a 25×13 pixel battery icon appears in the top-right corner of every screen title bar when a LiPo battery is connected via the GPIO 39 voltage divider; icon colour is green above 50 %, orange at 20–50 %, and red below 20 %; the icon is hidden on resistive CYDs or when no battery hardware is detected; uses sprite rendering for flicker-free updates
* Feature: **IP5306 charging detection** — on boards with an IP5306 PMIC (I²C address 0x75, shared bus with the capacitive touch controller), the battery icon outline turns cyan while charging; charging status is sampled every 60 seconds (low-priority I²C read to minimise bus contention)
* Feature: **power off via red button hold** — holding the red button for 5 seconds draws a "POWERING OFF" shutdown screen, dims the backlight, and enters ESP32 deep sleep; pressing red once wakes the device (full reboot — not a resume); short-press red still sends soft-reset as normal
* Architecture: battery sampling runs on Core 0 (`pendant_hw_task`) every 5 seconds under `stateMutex`; Core 1 reads battery fields without a mutex (int/bool are 32-bit atomic on Xtensa LX6); `POWER_OFF` hardware event added to the Core 0 → Core 1 event queue
* Feature: **WiFi transport layer** (capacitive CYD, opt-in) — raw TCP/Telnet connection to FluidNC on port 23 replaces the UART cable; default transport remains UART (no disruption to existing installs); enabled by `-DUSE_WIFI` in `cyd_new_ui` / `cyd_new_ui_combined` environments
* Feature: **WiFi captive-portal setup** — on first WiFi-mode boot the pendant broadcasts an open WiFi network named "FluidDial"; connect a phone and browse to 192.168.4.1 to enter SSID, password, and FluidNC IP/hostname; saving restarts the pendant and connects automatically; captive portal includes a Scan button to list nearby networks
* Feature: **WiFi mDNS support** — `hostname.local` addresses are resolved via ESP-IDF `mdns_query_a()`, bypassing misconfigured upstream DNS resolvers that can return wrong addresses for `.local` names
* Architecture: WiFi polling (`wifi_poll()`) and all transport I/O (`fnc_putchar` / `fnc_getchar`) run on Core 0 — the TCP RX ring buffer is never accessed from two cores simultaneously; `wifi_init()` is called in `pendant_hw_task`'s preamble before the event loop starts
* Architecture: transport routing in `SystemArduino.cpp` — `fnc_putchar` / `fnc_getchar` check `wifi_use_uart_mode()` (NVS-cached) and dispatch to either the UART driver or `ws_putchar` / `ws_getchar`; the GrblParser connection-detection and ping logic is transport-agnostic
* Polish: `wifi_save_config()` sets `uart_mode = false` and clears the first-boot flag in one NVS write, so a captive-portal save immediately switches the pendant to WiFi mode on the next boot

**2026-05-09**

v1.6.0
* Feature: combined firmware now supports both resistive (XPT2046) and capacitive (CST816S) CYD screens in a single binary "cyd_new_ui_combined".
* Feature: first-boot screen-type detection — on initial power-up the pendant tests each touch controller in turn, displaying "Capacitive — Tap the Screen" then "Resistive — Tap the Screen"; whichever responds first is saved to flash and used on every subsequent boot without repeating the detection
* Feature: to re-detect (e.g. after swapping the CYD board), hold the GPIO 0 / BOOT button during the 1-second window at startup; the saved screen type is cleared and the detection sequence runs again
* Architecture: new `[env:cyd_new_ui_combined]` PlatformIO environment with both `-DRESISTIVE_CYD` and `-DCAPACITIVE_CYD` build flags; the `[env:cyd_new_ui]` capacitive-only environment is retained for development use; the combined binary is what the web installer and Releases page ship
* Polish: FluidNC info screen — pendant firmware panel label changed from "FLUIDDIAL" to "FluidDial-CYD", clearly distinguishing it from the "FLUIDNC" row which shows the connected controller's firmware version

**2026-04-28**

v1.5.6
* Fix: demo mode (pendant not connected to a controller) now responds to touch immediately — eliminated a 10–16 second UI freeze caused by `fnc_send_line()` busy-waiting for UART acks that never arrive; a new `rxEverSeen` flag in the hardware task prevents the CONNECTED event from firing until at least one real UART byte is received from the controller
* Fix: triple-tap fine/coarse increment toggle on the Jog & Homing screen now resets reliably on screen exit — removed inner `static` variable shadows that were hiding the file-scope counters the reset code actually wrote to
* Feature: Spindle Control dial uses 100 RPM steps per encoder detent when the controller's max spindle RPM ($30) is 10 000 or below — larger spindles (> 10 k RPM) keep the existing 1 000 RPM step
* Architecture: all static controller information ($30/$31 spindle min/max RPM, $110 jog feed cap, $130-$133 per-axis travel limits, FluidNC version, IP address, WiFi SSID) is now fetched once on the connection edge and cached in pendant memory — no per-screen UART round-trip on Jog & Homing or FluidNC entry; reconnects auto-refresh
* Reliability: Spindle Control screen still re-queries $30/$31 on entry as a defensive measure — guarantees Min/Max RPM and preset values are current even if the connect-edge fetch was dropped (UART contention at connect time would otherwise leave them at compiled defaults of 0/24000)
* Safety: jog distance per encoder tick is now clamped to half the corresponding axis travel ($130-$133) — prevents a fast wheel turn at a coarse increment from queueing a $J move that would crash into a hard stop or trip soft limits; falls back to a hard-coded 100 mm / 4 in cap until the controller reports its limits
* Reliability: navigation handlers across Macros, Feeds & Speeds, SD Card, FluidNC, and Jog & Homing screens reworked to assign currentPendantScreen instead of calling navigateTo() directly — the central dispatcher now performs the screen exit/enter exactly once, eliminating the rare double-cycle that could leave a partially-drawn screen
* Reliability: the FluidNC screen rotation toggle no longer hammers NVS flash on every encoder detent — the new value is held in memory during the spin and written to flash exactly once when leaving the screen
* Reliability: ConfigItem.init() now deduplicates the configRequests vector — repeated init() calls on the same item no longer leak duplicate pointers
* Polish: triple-tap state on the rightmost jog increment button is now reset on screen entry, so a stale partial sequence from a prior visit doesn't count toward the next fine/coarse toggle
* Polish: saved jog increment index is constrained to the valid range on load — guards against a corrupted NVS entry indexing past the 4-element increment table
* Polish: periodic 100 ms sprite refresh is suppressed for one tick after a STATE_UPDATE-driven redraw — prevents the rare back-to-back redraw when DRO updates and the timer happen to overlap
* Internal: replaced the single global `spritesInitialized` flag with per-sprite `getBuffer()` guards in every update function — a failed sprite allocation on one buffer can no longer cause a different buffer to be used without being checked
* Internal: removed unused `previousPendantScreen` state, factored out a `refreshMacros()` helper, and replaced inline `display.color565()` calls on the Spindle Control Dial button with named `COLOR_TEAL` / `COLOR_TEAL_BRIGHT` constants

**2026-04-25**

v1.5.5
* Spindle Control screen: replaced "Direction" label with a two-column RPM display — left column shows live spindle RPM from the controller ("RPM"), right column shows the user-selected target RPM ("Target RPM") in green; target RPM is set independently via preset buttons or Dial mode and is what gets sent to the controller on Start
* Spindle Control screen: Start command now uses the target RPM value rather than the live RPM readback, preventing unintended speed changes
* FluidNC screen: bottom row split into two buttons — "Main Menu" (left) and "Status" (right) for quicker navigation

**2026-04-21**

v1.5.4
* Macros screen: now reads macros directly from FluidNC preferences.json (WebUI v3) or macrocfg.json (WebUI v2) via UART JSON streaming — no SD card folder or localfs files required; both formats supported automatically with preferences.json taking priority
* Macros screen: results cached after first successful load — navigating away and back shows the list instantly without a UART fetch; Refresh button forces a full re-fetch from the controller
* Macros/SD Card screens: file list area now rendered via offscreen sprite and pushed atomically — eliminates the fillScreen flicker that previously occurred on every STATUS_UPDATE DRO poll (~200 ms cycle)
* Performance: UART receive loop now drains the full hardware FIFO on every 2 ms tick instead of reading one byte per tick — throughput increased from ~500 B/s to ~50–80 KB/s; preferences.json now loads in under 2 seconds instead of 15+
* UART RX buffer increased from 256 to 4096 bytes to absorb full JSON response bursts without dropping bytes or triggering premature XON/XOFF flow control

**2026-04-19**

v1.5.3
* Macros screen: completely rewritten — reads macro0–macro9 directly from FluidNC config.yaml (no SD card folder required); macros display as "[N] content" with Cancel / Run confirmation; Run sends $Macro=N to the controller
* Status screen: when an SD job is running the Machine Status row splits into two columns — left shows the machine state (Run / Hold / etc.), right shows job progress as a live percentage in green
* Status screen: current filename now displays correctly while a job is running (filename read from FluidNC status reports each cycle)
* SD Card: fixed Run command — was incorrectly sending /sd/filename; now correctly sends $SD/Run=filename
* SD Card: Load queues the selected file on the pendant and navigates to the Status screen; the physical green button sends the run command; Status screen shows "READY — press green to run" with the filename in green until the job starts
* Jog smoothness: removed the 25 ms accumulator — each encoder tick now dispatches a $J command immediately with a minimum 1000 mm/min feed rate, eliminating the stop/start gaps between moves
* Physical buttons: debounce reduced from 100 ms to 30 ms and task tick from 5 ms to 2 ms for much faster response during a running job; red button reset is now non-blocking
* Jog & Homing screen: coarse increment set updated — metric: 1 / 10 / 50 / 100 mm; imperial: .05 / .5 / 2.0 / 4.0 in
* Fix: firmware version now correctly shows v1.5.3 on the FluidNC info screen

**2026-04-16**

v1.5.2
* SD Card screen: file confirmation buttons changed to "Load" and "Run"
* Run sends the file to the controller immediately and navigates to the Status screen
* Load queues the file on the pendant and navigates to the Status screen — the Status screen shows "READY — press green to run" with the filename highlighted in green; pressing the physical green button sends the run command
* Jog & Homing screen: Home buttons moved above Jog Axis buttons for quicker access
* Jog & Homing screen: two increment sets now available — fine (default) and coarse; triple-tap the rightmost increment button to switch between them
  * Fine metric: 0.01 / 0.1 / 1 / 10 mm
  * Coarse metric: 0.1 / 1 / 10 / 100 mm
  * Fine imperial: .0001 / .001 / .010 / .100 in
  * Coarse imperial: .001 / .010 / .100 / 1.00 in
* Increment set selection and active increment index are saved to flash and restored on reboot

v1.5.1
* Jog & Homing screen: default jog speed reduced to 100 mm/min (10 ipm) for safer initial movement

v1.5.0
* Work Area screen: Machine Position and Work Position labels and data sources corrected — Machine Pos now shows absolute machine coordinates and Work Pos shows the DRO (work coordinate) values
* Work Area screen: Work Position values now update live as the machine moves
* SD Card screen: file selection now requires a two-step confirmation — tapping a file highlights it in green and reveals CANCEL and RUN FILE buttons; tapping RUN FILE sends the run command and navigates to the Status screen
* Jogging: 25 ms accumulator batches encoder ticks into a single $J command per window, eliminating the repeated start/stop micro-moves that caused stuttering
* Jogging: velocity scaling — turning the dial faster sends a proportionally higher feed rate (up to 8× base speed), giving a natural acceleration feel
* Jog speed cap: pendant reads $110 (X-axis max rate) from the controller on entry to the Jog & Homing screen and uses that value as the upper limit for jog feed rate

**2026-04-07**

v1.4.1
* Jog & Homing screen: speed button now shows only the numeric value (e.g. 1000) instead of the prefixed label, fitting cleanly on the button
* Jog & Homing screen: "Main Menu" and "Work Area" button text reduced to fit correctly

**2026-03-31**

v1.4.0
* Alarm state now shows a human-readable description on the Main Menu and Status screens (e.g. "Hard limit triggered", "Homing fail - pull off") — alarm codes 1–10 covered
* Motion smoothness improvement: pendant hardware task priority reduced to avoid competing with FluidNC's Core 0 motion tasks
* Motion smoothness improvement: $? ping interval extended to 1000ms while the machine is Running (200ms when idle/stopped/alarm) to reduce UART load during active motion
* Jog & Homing screen: new Speed button in the bottom row displays current jog speed (e.g. F:1000) and allows the jog dial to set the speed in 100 mm/min or 10 ipm increments; the button highlights green when active and the jog axis is deselected — tap any axis button to return to jogging
* Jog commands now use the user-configured jog speed instead of the previous hardcoded values
* Feeds & Speeds screen: the feed and spindle override percentage readouts are now tappable buttons; tapping one activates dial mode (button highlights green) and the jog dial adjusts the override in 10% increments; only one can be active at a time — tapping the other or any preset button deactivates dial mode

**2026-03-30**

v1.3.0
* SD Card screen now reads GCode files live from the controller's SD card via UART
* File list loads automatically on entering the SD Card screen, with a Refresh button to reload
* Shows "Loading..." while waiting for the file list and a clear message if no files are found
* Macros screen now reads macro files live from the controller's local filesystem (localfs)
* Macro file list loads automatically on entry with the same scroll/Refresh navigation as the SD Card screen
* Selecting a macro runs it immediately via $Localfs/Run and navigates to the Status screen
* Jog & Homing screen displays alarm state (e.g. Alarm:1) in red where the unit label normally appears when a limit switch or other alarm is triggered
* Physical button behaviour updated:
  * Red: soft reset (cancels current program/move, stops spindle, clears alarm, position retained — no rehoming needed)
  * Yellow: pause current motion (Feed Hold)
  * Green: start or resume paused operation (Cycle Start)

**2026-03-29**

v1.2.0
* Automatic metric/imperial detection — pendant reads G20/G21 modal state from the controller and switches units throughout the UI without any manual configuration
* Jog increments switch automatically between mm (0.1 / 1 / 10 / 100) and imperial thou (.001 / .010 / .100 / 1.00)
* Jog commands include explicit G20/G21 to prevent unit mismatch, with appropriate feed rates for each unit system
* Position display on Jog & Homing screen shows mm or in with correct decimal places (2 for metric, 4 for imperial)
* Increment row label updates to show (mm) or (in) to match the active unit system

v1.1.0
* Button debounce increased to 100ms for improved reliability
* Pendant firmware version displayed as numeric only (e.g. v1.1.0) on the FluidNC screen
* Web installer published at https://djos1475.github.io/FluidDial-CYD/ — flash directly from Chrome/Edge with no software required

**2026-03-26**

v1.0.0
* Spindle screen now reads min/max RPM limits directly from the controller ($30/$31)
* RPM preset buttons are now calculated dynamically at 25%, 50% and 100% of the controller's max RPM
* RPM preset labels now display in short format (e.g. 6k instead of 6000)
* Added Dial mode button on spindle screen — when active, the jog dial sets RPM in 1000 RPM increments within the controller's valid min/max range
* Jog & Homing screen now shows an "ALL" home button when 3 axes are detected, replacing the unused A-axis home button
* Probe screen now respects the number of axes detected — 4th axis is hidden on 3-axis machines
* FluidNC screen flickering fixed using sprite rendering
* All screens that display or interact with axis positions now use live axis count from the controller


**2026-03-23**

v0.3.2
* minor Bug fixes
* fixed encoder reading - manual jogging now sends the right values.

v0.3
* Feed and spindle override preset buttons now send real commands to the controller
* Work zero buttons now correctly target the selected coordinate system (G54–G57)
* Faster connection to controller on startup
* Only axes available on the connected machine are shown throughout the UI
* FluidNC screen now shows live firmware version, IP address and WiFi SSID from the controller
* Spindle Start button now correctly sends M3 (forward) or M4 (reverse) based on direction selection
* Probe screen buttons now run macro files from the controller (probe_work_z.nc / probe_tool_height.nc)
* Probe screen shows "Running..." confirmation or "Not connected" if no controller is present


**2026-03-22**

v0.2
* Fixed the FluidNC Status screen - it now reports controller info correctly

v0.1

* Rebuilt with Hardware control now running on CPU Core 0 and the UI on CPU Core 1
* Fixed UART boot loop, now talks to FluidNC controllers properly
* Broke one large screen file into separate files per screen to reduce memory use
* Fixed position display showing wrong decimal place
* Fixed boot screen rotation
* Fixed button assignments (Red=E-stop, Yellow=pause/alarm clear, Green=cycle start)
* Fixed upload speed to reliably flash the device
* Fixed UI freezing when turning the encoder with no controller connected
* There are still a lot of things needing fixing, but core functionality is working.


**2025-01-15**
* Properly debounced the physical buttons
* Moved to using cyd_new_ui for the PlatformIO tasks to keep the original cyd_buttons project intact
* Changed to sprite based rendering to remove as much screen flicker as possible
* Made the Yellow button context sensitive
* remove axis coordinates from the Main Menu screen

**2025-01-13**
* FluidNC Screen now accessible via the Status page
* Probe Screen created and button added to Main Menu
* Screen can be rotated 180 degress via the Jog Dial from the FluidNC
* Screen Rotation is saved and will be recalled on restart

