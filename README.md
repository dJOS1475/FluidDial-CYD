# FluidDial-CYD: 
The CYD_Buttons Version has been optimised for FluidDial CNC Pendants with 3 physical buttons 
and a jog dial, and is designed to work with the FluidNC Firmware.

*** **This Firmware is a Work In-progress** ***

**Updates:**

**2025-01-15**
* Changed to sprite based rendering to remove as much screen flicker as possible
* Made the Yellow button context sensitive
* remove axis coordinates from the Main Menu screen

**2025-01-13**
* FluidNC Screen now accessible via the Status page
* Probe Screen created and button added to Main Menu
* Screen can be rotated 180 degress via the Jog Dial from the FluidNC
* Screen Rotation is saved and will be recalled on restart

**Design Goals:**

All Menu navigation and as many features as possible will be managed via the touch screen. The Physical jog dial will only move the CNC machine when in the Jog & Homing screen (safety feature). The Jog Dial will also be able to scroll through selected features depending on the screen (WIP). eg on the FluidNC screen, the jog Dial will rotate the display. 

The 3 physical buttons will always be:
* Red: e-stop
* Yellow: context sensitive
  * In ALARM → Clear Alarm
  * Otherwise → Pause/Hold
* Green: Cycle Start

<img src="https://raw.githubusercontent.com/dJOS1475/FluidDial-CYD/refs/heads/main/new_ui/Pendant2.jpeg" alt="CYD Dial Pendant With Buttons and Jog Dial" height="500">

Wiki pages for more information: CYD Dial Pendant (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant).

See the **Compiling Firmware** Section on how to test this version: (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant)

