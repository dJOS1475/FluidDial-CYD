# FluidDial-CYD: 
FluidDial-CYD is a custom firmware for CYD-equipped FluidDial CNC pendants. The UI has been rebuilt from the ground up for devices with 3 physical buttons and a jog dial. Supports both **resistive (XPT2046)** and **capacitive (CST816S)** CYD screen variants.

Feedback is welcome - If you find an issue or have a request for improvements, please log an [Issue](https://github.com/dJOS1475/FluidDial-CYD/issues) and I'll investigate.

**Updates:**
See [CHANGELOG.md](https://github.com/dJOS1475/FluidDial-CYD/blob/main/CHANGELOG.md)

<img src="https://raw.githubusercontent.com/dJOS1475/FluidDial-CYD/refs/heads/main/new_ui/Pendant5.jpeg" alt="CYD Dial Pendant With Buttons and Jog Dial" height="500">

**Web Installer** (use Chrome/Edge): 
https://djos1475.github.io/FluidDial-CYD/

* Unplug the RJ12 connection between the CYD and the FluidNC controller
* Connect the CYD USB port to your computer
* Click one of:
  * **Update** — installs the latest firmware while keeping all saved settings (WiFi credentials, screen-type detection, transport override, jog preferences, rotation). Use this for ALL firmware upgrades after the first install. **When the installer prompts, leave the "Erase device" box unchecked** — ticking it does a full-chip erase that wipes your saved WiFi credentials.
  * **Factory Reset** — wipes the entire device, then installs firmware fresh. Use this for first-time installs on a new CYD, or to recover from a stuck WiFi configuration. After a factory reset you'll re-run the touch-screen detection tap and re-enter WiFi credentials via the captive portal.

**Why two buttons?** WiFi credentials, the chosen screen type, and the transport override live in the ESP32's NVS partition at flash address `0x9000`. The single-blob "Factory Reset" image covers offset `0x0` upward and therefore wipes that region; the multi-part "Update" install writes only the bootloader / partition table / app / filesystem at their specific offsets, leaving NVS untouched. The result: once you've configured WiFi via the captive portal, you never need to re-enter it across subsequent firmware upgrades — provided you leave the installer's "Erase device" checkbox **unchecked** (a full erase clears NVS regardless of which install you pick).

**First boot after installing:** the pendant will display "Capacitive — Tap the Screen" followed by "Resistive — Tap the Screen". Tap the screen when your screen type is shown — the result is saved to flash and the pendant reboots into normal operation. This detection only runs once; subsequent boots go straight to the main menu. To re-detect (e.g. after swapping the CYD board), hold the **BOOT** button during the first second of startup.

If you have problems with the Web Installer, close VS Code / PlatformIO first — if the serial monitor is open it will block the installer from accessing the COM port. Also try an InPrivate/Incognito browser tab. The precompiled binary images can be downloaded from the [Releases section](https://github.com/dJOS1475/FluidDial-CYD/releases) .  They can be installed with any “esptool” ESP32 firmware download program. The **merged-flash.bin** image should be downloaded to FLASH at address 0x0000.  One such download program is this [web installer](https://espressif.github.io/esptool-js/); there are many others.

## 🎯 Design Goals

All menu navigation and as many features as possible are managed via the touch screen. The physical jog dial context-switches depending on the active screen — it only moves the CNC machine on the Jog & Homing screen (a safety feature), and serves other purposes elsewhere.

The 3 physical buttons always perform the same function regardless of the active screen:
* **Red:** Cancel — soft reset, stops all motion and spindle, clears alarm state. Position is retained and no rehoming is required. The cancelled operation cannot be resumed with Green.
* **Yellow:** Pause — holds current motion (Feed Hold). Spindle remains running. Green will resume.
* **Green:** Start / Resume — starts a new job or resumes after a Yellow pause.
* **Red (held 5 seconds): Power Off** — draws a shutdown screen, dims the backlight, and enters deep sleep. Press Red once to wake and resume normal operation.

---

## 🖥️ Screens & Features

**Main Menu** — touch navigation to all screens. Shows live machine status; alarm states display a human-readable description in red (e.g. "Hard limit triggered").

**Status** — live DRO showing machine position, feed rate, spindle RPM, active file, and machine state. Axis count is detected automatically from the connected controller. Alarm states show a human-readable description in red. When an SD job is running, the status row splits into two columns — left shows machine state (Run / Hold / etc.), right shows live job progress as a percentage in green. When a file has been queued via the SD Card Load button, the screen shows "READY — press green to run" with the filename highlighted in green until the job starts.

**Jog & Homing**
* Jog dial moves the selected axis by the chosen increment; the DRO shows **machine coordinates** (MPos)
* **Handwheel stop** — a single deliberate detent completes its full move, but during a **continuous spin** the axis stops the moment you stop turning (the pendant sends a real-time jog-cancel), so it doesn't coast through queued moves — like a full-size MPG
* Two increment sets — **Fine** (default) and **Coarse** — triple-tap the rightmost increment button to switch between them; the active set and selected increment are saved to flash and restored on reboot
  * Fine metric: 0.01 / 0.1 / 1 / 10 mm — Fine imperial: .0001 / .001 / .010 / .100 in
  * Coarse metric: 1 / 10 / 50 / 100 mm — Coarse imperial: .05 / .5 / 2.0 / 4.0 in
* Units detected automatically from the controller (G20/G21) — no manual switching needed
* The **A axis** (4th axis) is treated as rotary — its DRO shows a **degree symbol (°)** instead of mm/in; X/Y/Z use mm/in as normal
* Axis selection and increment buttons on screen
* Home buttons for each detected axis, plus an "ALL" home button on 3-axis machines
* Only axes present on the connected machine are shown
* Alarm state (e.g. from a limit switch) is displayed in red where the unit label normally appears
* **Speed field** (bottom row, between Main Menu and Work Area) — a tap-to-edit field (same style as the Probe screens) showing the current jog speed; tap to adjust it with the dial in 100 mm/min steps (metric) or 10 ipm steps (imperial); the field highlights while active; tap any axis to return to jogging
* Velocity scaling — turning the dial faster proportionally increases the jog feed rate up to 8× the base speed, giving a natural acceleration feel
* Jog feed rate is capped at the controller's X-axis maximum (`$110`), read automatically on entry to the screen
* **Soft-limit clamp** — once the machine is homed, the pendant clamps every jog so an axis's **absolute machine position can't leave its travel envelope**, preventing a fast or repeated jog from driving into a hard stop. The per-axis envelope is read from the controller's per-axis travel (`$130–$132`) and homing-direction mask (`$23`), so machines that home in different directions per axis work correctly. This engages only for homed X/Y/Z axes; it does not replace controller soft limits.
  * **Recommended:** also enable FluidNC's own soft limits as the authoritative backstop — set `$20=1` (soft limits) with `$22=1` (homing) in your FluidNC config. With `$20=1`, FluidNC itself rejects any over-travel move regardless of source. FluidNC ships with `$20=0` (off) by default.

**Work Area (Probing Work)**
* Coordinate system selection (G54–G57)
* Displays Machine Position (absolute machine coordinates) and Work Position (DRO / work coordinate values); Work Position updates live as the machine moves
* Zero individual axes or all at once for the selected coordinate system

**Probing**
* Opens on a hub with a **probe-type selector** — choose **Z-Height Touch Plate**, **XYZ Touch Plate**, or **3D Touch Probe**; the routines on offer change to match the selected type
* Routines:
  * **Z Surface** — probes Z and zeroes it (all probe types)
  * **XYZ Corner** — finds a workpiece corner and sets X0/Y0/Z0; cycle which corner (Bot-Left … Top-Right) (XYZ Plate and 3D Probe)
  * **Bore** — finds the centre of an inside circle and sets X0/Y0 (place the tip inside at any depth — there's no Z motion; set Z separately with Z Surface) (3D Probe)
  * **Boss** — finds the centre of a raised feature; touches the flat top for Z0 and sets X0/Y0/Z0 (3D Probe). **Tap the diagram** to switch between **circular** boss (one nominal diameter) and **rectangular/square** boss (independent X size / Y size); the diagram and field labels change to show the active mode
* Probe G-code is **generated on the pendant** (`G38.2` straight-probe moves) — no probe macro files need to live on the controller. Every wall/surface is reached with a **crash-safe two-pass move** (a fast seek then a slow re-probe), so a wrong nominal size can't drive the tip into a wall; the trigger offset is computed automatically from the configured plate thickness or 3D-probe ball radius, so a routine zeroes correctly whichever probe you use
* Each routine screen shows a **sequence list** and a **diagram** of the probe move, and a **Work Area** button to pick the coordinate system the probe will zero — tap it to cycle **G54–G57** right from the routine screen
* **Configure** opens a per-type setup screen — for the 3D probe: ball diameter plus an optional deflection correction (added to the ball radius; default 0) with an on-pendant **Deflection Cal** routine that measures it against a known gauge (e.g. the 2″ face of a 1-2-3 block); for a touch plate: plate dimensions (thickness, plus width and XY offsets for the XYZ plate)
* Shared settings (probe rate, seek rate, retract distance, max Z travel) apply to every routine; the default seek rate is 500 mm/min
* **For more info** — see the [Probing Guide](Probing.md) for a detailed, per-screen breakdown of every probing screen, what each option does, and how each routine moves the machine

**Feeds & Speeds**
* Feed and spindle override preset buttons (50% / 75% / 100% / 125% / 150%)
* **Dial mode** — tap the live percentage field to activate dial mode (the field highlights, matching the Probe screens' tap-to-edit style); the jog dial then adjusts that override in 10% increments; only one (feed or spindle) can be active at a time; tapping a preset or the other field deactivates it
* Overrides ramp to the target with **paced coarse+fine steps** anchored off the current value (one realtime byte every ~60 ms) rather than a burst — gentle on a Modbus VFD and safe to use during a running job

**Spindle Control**
* Direction selection (Forward / Reverse → M3 / M4)
* Live RPM display panel — left column shows current spindle RPM reported by the controller; right column shows the **Target RPM** (the speed that will be sent on Start), highlighted in green
* RPM presets at 25%, 50% and 100% of the controller's maximum RPM (read live from `$30`); preset labels shown in short format (e.g. 6k, 12k, 24k)
* Minimum and maximum RPM values displayed, read directly from the controller (`$30` / `$31`)
* **Dial mode** — tap the "Dial" field (same tap-to-edit style as the Probe screens; highlights while active) to set Target RPM with the encoder; steps in 100 RPM increments for spindles ≤ 10 000 RPM, or 1 000 RPM increments for larger spindles; clamped to the controller's valid range
* Start / Stop buttons send M3/M4/M5 with the Target RPM

**Macros** — reads macros directly from the FluidNC controller via UART JSON streaming. Supports both WebUI v3 (preferences.json) and WebUI v2 (macrocfg.json) formats automatically, with preferences.json taking priority. List loads on first entry and is cached — subsequent visits are instant. Refresh button fetches a fresh copy from the controller. Scroll and tap-to-confirm navigation; Run sends the command and navigates to the Status screen.

**SD Card** — browse and run G-code files live from the controller's SD card. File list loads automatically on entry with a Refresh button to reload. Tap a file to arm it (Load / Run buttons appear), then Load queues the file and navigates to the Status screen while Run sends the command immediately.

**FluidNC** — shows live info in two panels: pendant firmware (FluidDial-CYD version), FluidNC controller firmware version, IP address, WiFi SSID, free heap, connection status, and display rotation. Jog dial rotates the display 180°; rotation is saved across restarts. Tap the **CONNECTION** panel to open the WiFi setup / transport-info screen. Bottom row has two navigation buttons — Main Menu and Status.

---

## 🔋 Battery Status (capacitive CYD only)

On capacitive-screen CYDs a small battery icon appears in the top-right corner of every screen title bar when a LiPo battery is connected to the CYD's 3.3 V rail (via GPIO 39 voltage divider):

* **Icon colour** — green above 50 %, orange 20–50 %, red below 20 %
* **Charging indicator** — a yellow lightning-bolt overlay appears on the battery icon while charging. Charging is inferred from the battery-voltage trend (rising/held-high ⇒ on charger), so it reacts within ~30 s of plugging in. A short post-boot settling window suppresses it for the first couple of minutes after power-up, so the normal boot-load voltage recovery isn't mistaken for charging
* **Update rate** — battery level refreshes every 5 seconds; charging status every 3 seconds (cheap ADC read)
* **Screen sleep** — to save power on battery, the display backlight switches off after **15 minutes** with the CNC Idle (or stuck "Connecting", eg your CNC is powered down and pendant is charging); **a touch anywhere wakes it** back to the screen you left, without sending any command to the controller. This is backlight-off only — the pendant stays connected and running. Wired (UART) pendants don't sleep
* The icon is hidden on resistive CYDs or when no battery hardware is detected

No wiring changes are needed on CYD boards that already have the battery circuit populated. Battery monitoring is built into the shipping firmware — no opt-in required.

> ⚠️ **"Off" still drains the battery.** Holding the red button powers the pendant *off* by putting the ESP32 into deep sleep, but the CYD's voltage regulator and other on-board parts keep drawing a few milliamps from the battery — there is no hardware switch that fully isolates the cell. A charged LiPo will therefore slowly self-drain over a day or two even while "off", and can end up flat. **If you run on battery, put the pendant on USB charge after use** (or unplug the battery) so it's ready next time.

---

## 📡 WiFi Transport

> **Battery-powered pendants only.** WiFi replaces the UART cable with a **WebSocket** connection to FluidNC, over the same HTTP port (80) that serves its WebUI. Wired pendants always use UART — there is no user-facing toggle.

### How transport selection works

The pendant decides which transport to use at every boot, based on the hardware it detects:

| Hardware detected | Active transport |
|---|---|
| **IP5306 battery PMIC present** (battery-equipped capacitive CYD) | **WiFi** |
| No battery PMIC (wired CYD, M5Dial, resistive CYD) | **UART** |

This means:

- **Upgrading firmware on a wired pendant** does nothing — UART still works exactly as before, the WiFi stack never starts.
- **A battery pendant always wants WiFi**, so on first boot (no saved credentials) it automatically starts the captive-portal access point so you can configure it from a phone.
- **There is no way to get stuck in the wrong mode** — the hardware physically determines the choice. No NVS toggle, no "did I save the right setting?", no boot prompt.

### First-time WiFi setup (battery pendants)

1. Power on the pendant. With no saved credentials it broadcasts an open WiFi network named **FluidDial**.
2. Connect a phone or laptop to **FluidDial** and browse to **http://192.168.4.1**.
3. Enter your network SSID, password, and the IP address (or `hostname.local`) of your FluidNC controller, then tap **Save & Connect**.
4. The pendant restarts, joins your network, and connects to FluidNC over the WebSocket.

### Reconfiguring WiFi

From the FluidNC screen, tap the **CONNECTION** panel to open the WiFi setup screen. Tap **Reconfigure WiFi** — saved credentials are cleared and the pendant restarts back into the captive portal so you can pick a new network or change the FluidNC IP. (On wired pendants the same screen just confirms UART is in use; there is no reconfigure action because there is nothing to configure.)

### Credentials persist across firmware updates

Saved WiFi credentials live in the ESP32's NVS partition. Use the **Update** button on the web installer (the default) to flash a new firmware version without touching NVS — your network, password and FluidNC IP carry over. Only use **Factory Reset** when you want a clean slate (e.g. moving the pendant to a different network and unable to reach the captive portal).

### Signal strength and status

A signal-strength icon (four ascending bars) sits in the top-left of every title bar on battery pendants, dimmed to the current RSSI level — it shows "AP" while the captive portal is active. The WiFi Setup screen shows live SSID, signal bars, current FluidNC IP, and connection state. The FluidNC info screen also shows the IP address and SSID once connected.

### Testing with hard-coded credentials

For development / quick testing without the captive portal, set `HARDCODE_TEST_WIFI 1` in `src/WiFiConnection.cpp` and fill in `TEST_WIFI_SSID`, `TEST_WIFI_PASS`, and `TEST_FLUIDNC_IP`. This bypasses NVS at compile time. Note that even with this flag, the firmware still requires the battery PMIC to be present at runtime — on a wired board the WiFi backend is never invoked. Remember to reset to `0` for production firmware.

---

Wiki pages for more information: CYD Dial Pendant (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant).

### Building from source

The only supported build target is **`cyd_new_ui_combined`** — a single binary that runs on every supported CYD variant (resistive XPT2046, capacitive CST816S, with or without the IP5306 battery PMIC). It is the binary published via the web installer and on the [Releases page](https://github.com/dJOS1475/FluidDial-CYD/releases).

```
pio run -e cyd_new_ui_combined
pio run -e cyd_new_ui_combined -t upload
```

Other PlatformIO environments in `platformio.ini` (`cyd_new_ui`, `cyd_base`, `cyd_resistive`, `cyd_nodebug`, `cyd_buttons`, `cyddial`, `m5dial`, `windows`) are retained as historical / development references and are **not** maintained for end-user use. Build them at your own risk.

FluidNC Discord Topic: https://discord.com/channels/780079161460916227/1453176703009423514

## 💛 Project Supporters

A huge thank you to the following people who have supported this project:

| Supporter | Platform |
|---|---|
| simrim1 | Discord |
| Janusz  | Discord |
| karlpe  | GitHub  |
 
---

[![Donate](https://img.shields.io/badge/Donate-PayPal-blue?logo=paypal)](https://paypal.me/derekjosborn)

Firmware in Action connected to my CNC Machine Simulator (v1.0): 
[![Watch the video](https://img.youtube.com/vi/d5PogiUiiaw/maxresdefault.jpg)](https://youtu.be/d5PogiUiiaw)

Firmware in Action onnected to my CNC Machine Simulator (v2.1.7): 
[![Watch the video](https://img.youtube.com/vi/rT5MI35Bp68/maxresdefault.jpg)](https://youtu.be/rT5MI35Bp68)

