# FluidDial-CYD: 
The UI has been rebuilt from scratch and optimised for CYD equipped FluidDial CNC Pendants with 3 physical buttons and a jog dial, and is designed to work with the FluidNC Firmware.

**ChangeLog:**


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

