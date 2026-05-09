# FluidDial-CYD: 

**ChangeLog:**


**2026-05-09**

v1.6.0
* Feature: combined firmware now supports both resistive (XPT2046) and capacitive (CST816S) CYD screens in a single binary — no need to choose the correct image before flashing
* Feature: first-boot screen-type detection — on initial power-up the pendant tests each touch controller in turn, displaying "Capacitive — Tap the Screen" then "Resistive — Tap the Screen"; whichever responds first is saved to flash and used on every subsequent boot without repeating the detection
* Feature: to re-detect (e.g. after swapping the CYD board), hold the GPIO 0 / BOOT button during the 1-second window at startup; the saved screen type is cleared and the detection sequence runs again
* Architecture: new `[env:cyd_new_ui_combined]` PlatformIO environment with both `-DRESISTIVE_CYD` and `-DCAPACITIVE_CYD` build flags; the `[env:cyd_new_ui]` capacitive-only environment is retained for development use; the combined binary is what the web installer and Releases page ship

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

