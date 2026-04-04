# FluidDial-CYD: 
FluidDial-CYD is a custom firmware for capacitive CYD-equipped FluidDial CNC pendants. The UI has been rebuilt from the ground up for devices with 3 physical buttons and a jog dial. Feedback is welcome.

**Updates:**
See [CHANGELOG.md](https://github.com/dJOS1475/FluidDial-CYD/blob/main/CHANGELOG.md)

**Web Installer** (use Chrome/Edge): 
https://djos1475.github.io/FluidDial-CYD/

If you have problems with FluidDial-CYD Web Installer, there are some alternative methods you can try.  The precompiled binary images can be downloaded from the [Releases section](https://github.com/dJOS1475/FluidDial-CYD/releases) .  They can be installed with any “esptool” ESP32 firmware download program   The **merged-flash.bin** image should be downloaded to FLASH at address 0x0000.  One such download program is this [web installer](https://espressif.github.io/esptool-js/); there are many others.

**Design Goals:**

All menu navigation and as many features as possible are managed via the touch screen. The physical jog dial context-switches depending on the active screen — it only moves the CNC machine on the Jog & Homing screen (a safety feature), and serves other purposes elsewhere.

The 3 physical buttons always perform the same function regardless of the active screen:
* Red: Cancel — soft reset, stops all motion and spindle, clears alarm state. Position is retained and no rehoming is required. The cancelled operation cannot be resumed with Green.
* Yellow: Pause — holds current motion (Feed Hold). Spindle remains running. Green will resume.
* Green: Start / Resume — starts a new job or resumes after a Yellow pause.

<img src="https://raw.githubusercontent.com/dJOS1475/FluidDial-CYD/refs/heads/main/new_ui/Pendant3.jpeg" alt="CYD Dial Pendant With Buttons and Jog Dial" height="400">
---

**Screens & Features:**

**Main Menu** — touch navigation to all screens. Shows live machine status; alarm states display a human-readable description in red (e.g. "Hard limit triggered").

**Status** — live DRO showing machine position, feed rate, spindle RPM, active file, and machine state. Axis count is detected automatically from the connected controller. Alarm states show a human-readable description in red.

**Jog & Homing**
* Jog dial moves the selected axis by the chosen increment
* Metric: 0.1 / 1 / 10 / 100 mm — Imperial: .001 / .010 / .100 / 1.00 in
* Units detected automatically from the controller (G20/G21) — no manual switching needed
* Axis selection and increment buttons on screen
* Home buttons for each detected axis, plus an "ALL" home button on 3-axis machines
* Only axes present on the connected machine are shown
* Alarm state (e.g. from a limit switch) is displayed in red where the unit label normally appears
* **Speed button** (bottom row, between Main Menu and Work Area) — tap to set jog speed with the dial: 100 mm/min steps (metric) or 10 ipm steps (imperial), range 100–5000 mm/min / 10–500 ipm; button highlights green when active; tap any axis to return to jogging

**Work Area (Probing Work)**
* Coordinate system selection (G54–G57)
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
* RPM presets at 25%, 50% and 100% of the controller's maximum RPM (read live from `$30`)
* Preset labels shown in short format (e.g. 6k, 12k, 24k)
* Minimum and maximum RPM values displayed, read directly from the controller (`$30` / `$31`)
* **Dial mode** — tap the "Dial" button to enable jog-dial RPM selection: the encoder adjusts the target RPM in 1000 RPM steps, clamped to the controller's valid range
* Start / Stop buttons send M3/M4/M5 with the selected RPM

**Macros** — browses and runs macro files live from the controller's local filesystem. File list loads automatically on entry with scroll and Refresh navigation. Selecting a macro runs it immediately and navigates to the Status screen.

**SD Card** — browse and run G-code files live from the controller's SD card. File list loads automatically on entry with a Refresh button to reload. Selecting a file starts it immediately and navigates to the Status screen.

**FluidNC** — shows live controller info: firmware version, WiFi SSID, IP address, connection status, free heap. Jog dial rotates the display 180°; rotation is saved across restarts.

Wiki pages for more information: CYD Dial Pendant (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant).

See the **Compiling Firmware** section on how to test this version - use **cyd_new_ui**: (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant)

FluidNC Discord Topic: https://discord.com/channels/780079161460916227/1453176703009423514

Firmware in Action (v1.0): 
[![Watch the video](https://img.youtube.com/vi/d5PogiUiiaw/maxresdefault.jpg)](https://youtu.be/d5PogiUiiaw)

[![Donate](https://img.shields.io/badge/Donate-PayPal-blue?logo=paypal)](https://paypal.me/derekjosborn)
