# FluidDial-CYD: 
The CYD_Buttons version has been cloned into CYD_New_UI - the UI has been rebuilt from scratch and optimised for CYD equipped FluidDial CNC Pendants with 3 physical buttons and a jog dial, and is designed to work with the FluidNC Firmware.

*** **This Firmware is a Work In-progress** - FluidNC Control is now mostly functional ***

**ChangeLog:**


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

