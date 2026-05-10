# FluidDial-CYD: 
FluidDial-CYD is a custom firmware for CYD-equipped FluidDial CNC pendants. The UI has been rebuilt from the ground up for devices with 3 physical buttons and a jog dial. Supports both **resistive (XPT2046)** and **capacitive (CST816S)** CYD screen variants.

Feedback is welcome - If you find an issue or have a request for improvements, please log an [Issue](https://github.com/dJOS1475/FluidDial-CYD/issues) and I'll investigate.

**Updates:**
See [CHANGELOG.md](https://github.com/dJOS1475/FluidDial-CYD/blob/main/CHANGELOG.md)

**Web Installer** (use Chrome/Edge): 
https://djos1475.github.io/FluidDial-CYD/

* Unplug the RJ12 connection between the CYD and the FluidNC controller
* Connect the CYD USB port to your computer
* Click “Connect & Install”

**First boot after installing:** the pendant will display “Capacitive — Tap the Screen” followed by “Resistive — Tap the Screen”. Tap the screen when your screen type is shown — the result is saved to flash and the pendant reboots into normal operation. This detection only runs once; subsequent boots go straight to the main menu. To re-detect (e.g. after swapping the CYD board), hold the **BOOT** button during the first second of startup.

If you have problems with the Web Installer, close VS Code / PlatformIO first — if the serial monitor is open it will block the installer from accessing the COM port. Also try an InPrivate/Incognito browser tab. The precompiled binary images can be downloaded from the [Releases section](https://github.com/dJOS1475/FluidDial-CYD/releases) .  They can be installed with any “esptool” ESP32 firmware download program. The **merged-flash.bin** image should be downloaded to FLASH at address 0x0000.  One such download program is this [web installer](https://espressif.github.io/esptool-js/); there are many others.

## 🎯 Design Goals

All menu navigation and as many features as possible are managed via the touch screen. The physical jog dial context-switches depending on the active screen — it only moves the CNC machine on the Jog & Homing screen (a safety feature), and serves other purposes elsewhere.

The 3 physical buttons always perform the same function regardless of the active screen:
* Red: Cancel — soft reset, stops all motion and spindle, clears alarm state. Position is retained and no rehoming is required. The cancelled operation cannot be resumed with Green.
* Yellow: Pause — holds current motion (Feed Hold). Spindle remains running. Green will resume.
* Green: Start / Resume — starts a new job or resumes after a Yellow pause.

<img src="https://raw.githubusercontent.com/dJOS1475/FluidDial-CYD/refs/heads/main/new_ui/Pendant3.jpeg" alt="CYD Dial Pendant With Buttons and Jog Dial" height="400">

---

## 🖥️ Screens & Features

**Main Menu** — touch navigation to all screens. Shows live machine status; alarm states display a human-readable description in red (e.g. "Hard limit triggered").

**Status** — live DRO showing machine position, feed rate, spindle RPM, active file, and machine state. Axis count is detected automatically from the connected controller. Alarm states show a human-readable description in red. When an SD job is running, the status row splits into two columns — left shows machine state (Run / Hold / etc.), right shows live job progress as a percentage in green. When a file has been queued via the SD Card Load button, the screen shows "READY — press green to run" with the filename highlighted in green until the job starts.

**Jog & Homing**
* Jog dial moves the selected axis by the chosen increment
* Two increment sets — **Fine** (default) and **Coarse** — triple-tap the rightmost increment button to switch between them; the active set and selected increment are saved to flash and restored on reboot
  * Fine metric: 0.01 / 0.1 / 1 / 10 mm — Fine imperial: .0001 / .001 / .010 / .100 in
  * Coarse metric: 1 / 10 / 50 / 100 mm — Coarse imperial: .05 / .5 / 2.0 / 4.0 in
* Units detected automatically from the controller (G20/G21) — no manual switching needed
* Axis selection and increment buttons on screen
* Home buttons for each detected axis, plus an "ALL" home button on 3-axis machines
* Only axes present on the connected machine are shown
* Alarm state (e.g. from a limit switch) is displayed in red where the unit label normally appears
* **Speed button** (bottom row, between Main Menu and Work Area) — tap to set jog speed with the dial: 100 mm/min steps (metric) or 10 ipm steps (imperial); button highlights green when active; tap any axis to return to jogging
* Velocity scaling — turning the dial faster proportionally increases the jog feed rate up to 8× the base speed, giving a natural acceleration feel
* Jog feed rate is capped at the controller's X-axis maximum (`$110`), read automatically on entry to the screen

**Work Area (Probing Work)**
* Coordinate system selection (G54–G57)
* Displays Machine Position (absolute machine coordinates) and Work Position (DRO / work coordinate values); Work Position updates live as the machine moves
* Zero individual axes or all at once for the selected coordinate system

**Probing**
* Runs probe macro files stored on the FluidNC controller
* Z Surface probe: runs `probe_work_z.nc`
* Tool Height setter: runs `probe_tool_height.nc`
* Example macro files are included in the `/macros` folder — see [`macros/INSTRUCTIONS.md`](https://github.com/dJOS1475/FluidDial-CYD/blob/main/macros/INSTRUCTIONS.md)

**Feeds & Speeds**
* Feed and spindle override preset buttons (50% / 75% / 100% / 125% / 150%)
* **Dial mode** — tap the live percentage readout to activate dial mode (highlights green); the jog dial then adjusts that override in 10% increments; only one (feed or spindle) can be active at a time; tapping a preset or the other readout deactivates it

**Spindle Control**
* Direction selection (Forward / Reverse → M3 / M4)
* Live RPM display panel — left column shows current spindle RPM reported by the controller; right column shows the **Target RPM** (the speed that will be sent on Start), highlighted in green
* RPM presets at 25%, 50% and 100% of the controller's maximum RPM (read live from `$30`); preset labels shown in short format (e.g. 6k, 12k, 24k)
* Minimum and maximum RPM values displayed, read directly from the controller (`$30` / `$31`)
* **Dial mode** — tap the "Dial" button to set Target RPM with the encoder; steps in 100 RPM increments for spindles ≤ 10 000 RPM, or 1 000 RPM increments for larger spindles; clamped to the controller's valid range
* Start / Stop buttons send M3/M4/M5 with the Target RPM

**Macros** — reads macros directly from the FluidNC controller via UART JSON streaming. Supports both WebUI v3 (preferences.json) and WebUI v2 (macrocfg.json) formats automatically, with preferences.json taking priority. List loads on first entry and is cached — subsequent visits are instant. Refresh button fetches a fresh copy from the controller. Scroll and tap-to-confirm navigation; Run sends the command and navigates to the Status screen.

**SD Card** — browse and run G-code files live from the controller's SD card. File list loads automatically on entry with a Refresh button to reload. Tap a file to arm it (Load / Run buttons appear), then Load queues the file and navigates to the Status screen while Run sends the command immediately.

**FluidNC** — shows live info in two panels: pendant firmware (FluidDial-CYD version), FluidNC controller firmware version, IP address, WiFi SSID, free heap, connection status, and display rotation. Jog dial rotates the display 180°; rotation is saved across restarts. Bottom row has two navigation buttons — Main Menu and Status.

---

Wiki pages for more information: CYD Dial Pendant (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant).

See the **Compiling Firmware** section on how to test this version - use **cyd_new_ui_combined**: (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant)

FluidNC Discord Topic: https://discord.com/channels/780079161460916227/1453176703009423514

## 💛 Project Supporters

A huge thank you to the following people who have supported this project:

| Supporter | Platform |
|---|---|
| simrim1 | Discord |
| Janusz  | Discord |

---

Firmware in Action (v1.0): 
[![Watch the video](https://img.youtube.com/vi/d5PogiUiiaw/maxresdefault.jpg)](https://youtu.be/d5PogiUiiaw)

[![Donate](https://img.shields.io/badge/Donate-PayPal-blue?logo=paypal)](https://paypal.me/derekjosborn)
