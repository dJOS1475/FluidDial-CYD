# FluidDial-CYD: 
The CYD_Buttons version has been cloned into CYD_New_UI - the UI has been rebuilt from scratch and optimised for CYD equipped FluidDial CNC Pendants with 3 physical buttons and a jog dial, and is designed to work with the FluidNC Firmware.

*** **This Firmware is a Work In-progress** - FluidNC Control is now mostly functional ***

**Updates:**
See CHANGELOG.md

**Design Goals:**

All Menu navigation and as many features as possible will be managed via the touch screen. The Physical jog dial will only move the CNC machine when in the Jog & Homing screen (safety feature). The Jog Dial will also be able to scroll through selected features depending on the screen (WIP). eg on the FluidNC screen, the jog Dial will rotate the display. 

The 3 physical buttons will always be:
* Red: e-stop
* Yellow: context sensitive
  * In ALARM → Clear Alarm
  * Otherwise → Pause/Hold
* Green: Cycle Start

<img src="https://raw.githubusercontent.com/dJOS1475/FluidDial-CYD/refs/heads/main/new_ui/Pendant3.jpeg" alt="CYD Dial Pendant With Buttons and Jog Dial" height="500">

Firmware in Action: https://youtu.be/SAHOKBj9q10

Wiki pages for more information: CYD Dial Pendant (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant).

See the **Compiling Firmware** section on how to test this version - use **cyd_new_ui**: (http://wiki.fluidnc.com/en/hardware/official/CYD_Dial_Pendant)

