# FluidDial-CYD: 
The CYD_Buttons version has been cloned into CYD_New_UI - the UI has been rebuilt from scratch and optimised for CYD equipped FluidDial CNC Pendants with 3 physical buttons and a jog dial, and is designed to work with the FluidNC Firmware.


**Updates:**
See [CHANGELOG.md](https://raw.githubusercontent.com/dJOS1475/FluidDial-CYD/refs/heads/main/CHANGELOG.md)

**Web Installer** (use Chrome/Edge): 
https://djos1475.github.io/FluidDial-CYD/

**Design Goals:**

All menu navigation and as many features as possible are managed via the touch screen. The physical jog dial context-switches depending on the active screen — it only moves the CNC machine on the Jog & Homing screen (a safety feature), and serves other purposes elsewhere.

The 3 physical buttons always perform the same function regardless of the active screen:
* Red: Cancel — soft reset, stops all motion and spindle, clears alarm state. Position is retained and no rehoming is required. The cancelled operation cannot be resumed with Green.
* Yellow: Pause — holds current motion (Feed Hold). Spindle remains running. Green will resume.
* Green: Start / Resume — starts a new job or resumes after a Yellow pause.

---

**Screens & Features:**

**Main Menu** — touch navigation to all screens.

**Status** — live DRO showing machine position, feed rate, spindle RPM, active file, and machine state. Axis count is detected automatically from the connected controller.

**Jog & Homing**
* Jog dial moves the selected axis by the chosen increment
* Metric: 0.1 / 1 / 10 / 100 mm — Imperial: .001 / .010 / .100 / 1.00 in
* Units detected automatically from the controller (G20/G21) — no manual switching needed
* Axis selection and increment buttons on screen
* Home buttons for each detected axis, plus an "ALL" home button on 3-axis machines
* Only axes present on the connected machine are shown

**Work Area (Probing Work)**
* Coordinate system selection (G54–G57)
* Zero individual axes or all at once for the selected coordinate system

**Probing**
* Runs probe macro files stored on the FluidNC controller
* Z Surface probe: runs `probe_work_z.nc`
* Tool Height setter: runs `probe_tool_height.nc`
* Example macro files are included in the `/macros` folder — see `macros/INSTRUCTIONS.md`

**Feeds & Speeds**
* Feed and spindle override controls with fine adjustment and reset buttons

**Spindle Control**
* Direction selection (Forward / Reverse → M3 / M4)
* RPM presets at 25%, 50% and 100% of the controller's maximum RPM (read live from `$30`)
* Preset labels shown in short format (e.g. 6k, 12k, 24k)
* Minimum and maximum RPM values displayed, read directly from the controller (`$30` / `$31`)
* **Dial mode** — tap the "Dial" button to enable jog-dial RPM selection: the encoder adjusts the target RPM in 1000 RPM steps, clamped to the controller's valid range
* Start / Stop buttons send M3/M4/M5 with the selected RPM

**Macros** — runs up to 10 user-configured macros stored on the controller.

**SD Card** — browse and run G-code files live from the controller's SD card. File list loads automatically on entry with a Refresh button to reload. Selecting a file starts it immediately and navigates to the Status screen.

**FluidNC** — shows live controller info: firmware version, WiFi SSID, IP address, connection status, free heap. Jog dial rotates the display 180°; rotation is saved across restarts.

<img src="https://raw.githubusercontent.com/dJOS1475/FluidDial-CYD/refs/heads/main/new_ui/Pendant3.jpeg" alt="CYD Dial Pendant With Buttons and Jog Dial" height="500">

Wiki pages for more information: CYD Dial Pendant (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant).

See the **Compiling Firmware** section on how to test this version - use **cyd_new_ui**: (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant)

Firmware in Action: 
[![Watch the video](https://img.youtube.com/vi/d5PogiUiiaw/maxresdefault.jpg)](https://youtu.be/d5PogiUiiaw)


