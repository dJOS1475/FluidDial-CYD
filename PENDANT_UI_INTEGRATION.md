# CNC Pendant UI Integration Guide

## Overview
The CNC Pendant UI has been successfully integrated into the FluidDial-CYD project. This new UI provides a comprehensive touchscreen interface for CNC machine control.

## Files Added

1. **src/cnc_pendant_config.h** - Configuration file with pin definitions and color schemes
2. **src/CNC_Pendant_UI.cpp** - Main pendant UI implementation
3. **src/CNC_Pendant_UI.h** - Header file with public interface functions

## Integration Changes

The pendant UI has been adapted to work with the existing FluidDial-CYD architecture:

- Uses existing `Hardware2432.cpp` hardware initialization
- Integrates with existing LovyanGFX display driver
- Compatible with both resistive and capacitive CYD displays
- Supports physical buttons (red, yellow, green) on the P6 connector
- Works with the rotary encoder on CN1 connector

## How to Use

### Option 1: Replace Main Loop (Complete UI Replacement)

To completely replace the existing FluidDial UI with the new Pendant UI:

1. Edit `src/ardmain.cpp` and add at the top:
```cpp
#include "CNC_Pendant_UI.h"
```

2. Modify the `setup()` function:
```cpp
void setup() {
    init_system();
    display.setBrightness(255);

    // Initialize the pendant UI
    setup_pendant();

    dbg_printf("FluidNC Pendant with new UI\n");
}
```

3. Modify the `loop()` function:
```cpp
void loop() {
    fnc_poll();         // Handle messages from FluidNC
    loop_pendant();     // Handle pendant UI
}
```

### Option 2: Add as Alternative UI Mode (Recommended)

To add the pendant UI as an alternative mode that can be switched to:

1. Edit `src/ardmain.cpp` and add at the top:
```cpp
#include "CNC_Pendant_UI.h"

bool usePendantUI = false;  // Toggle this to switch UI modes
```

2. In `setup()` function, add:
```cpp
void setup() {
    init_system();
    display.setBrightness(aboutScene.getBrightness());

    if (usePendantUI) {
        setup_pendant();
        dbg_printf("FluidNC Pendant with Alternative UI\n");
    } else {
        show_logo();
        delay_ms(2000);
        base_display();
        dbg_printf("FluidNC Pendant %s\n", git_info);
        fnc_realtime(StatusReport);
        extern Scene* initMenus();
        activate_scene(initMenus());
    }
}
```

3. In `loop()` function, add:
```cpp
void loop() {
    fnc_poll();  // Handle messages from FluidNC

    if (usePendantUI) {
        loop_pendant();  // Handle pendant UI
    } else {
        dispatch_events();  // Handle dial, touch, buttons (original UI)
    }
}
```

4. You could add a button or encoder action to toggle `usePendantUI` at runtime.

## Features

The new pendant UI includes these screens:

1. **Main Menu** - Central navigation hub showing status and axis positions
2. **Status** - Detailed machine status display
3. **Jog & Homing** - Manual axis jogging with rotary encoder, homing controls
4. **Probing & Work** - Work coordinate system management, probing functions
5. **Feeds & Speeds** - Feed rate and spindle speed override controls
6. **Spindle Control** - Spindle RPM presets, direction, start/stop
7. **Macros** - 10 programmable macro buttons
8. **SD Card** - File browser for SD card files
9. **FluidNC** - Connection info, versions, system resources

## Physical Button Functions

When `CYD_BUTTONS` is defined (as it is for `cyd_buttons` environment):

- **Red Button (GPIO 4)** - Emergency Stop (sends `!` to FluidNC)
- **Yellow Button (GPIO 17)** - Feed Hold/Pause (sends `!` to FluidNC)
- **Green Button (GPIO 16)** - Cycle Start/Resume (sends `~` to FluidNC)

## Configuration

Edit [src/cnc_pendant_config.h](src/cnc_pendant_config.h) to adjust:

- Display dimensions and rotation
- Pin assignments for buttons and encoder
- Touch calibration values
- Color scheme
- Serial baud rate

## Pin Configuration

The pendant UI uses the standard CYD pinout:

### Display (SPI)
- SCLK: GPIO 14
- MOSI: GPIO 13
- MISO: GPIO 12
- CS: GPIO 15
- DC: GPIO 2
- Backlight: GPIO 21 (resistive) or GPIO 27 (capacitive)

### Touch Screen
- Resistive: GPIO 33 (CS), 25 (SCLK), 32 (MOSI), 39 (MISO)
- Capacitive: GPIO 33 (SDA), 32 (SCL), 25 (RST)

### Physical Buttons (P6 Connector)
- Red: GPIO 4
- Yellow: GPIO 17
- Green: GPIO 16

### Rotary Encoder (CN1 Connector)
- CLK: GPIO 22
- DT: GPIO 21 (resistive) or GPIO 21 (capacitive)
- SW: GPIO 35 (optional)

## Building

Build the project for the `cyd_buttons` environment:

```bash
pio run -e cyd_buttons
```

Or use the PlatformIO IDE to build and upload.

## Touch Calibration

If touch input is misaligned, adjust these values in `src/cnc_pendant_config.h`:

```cpp
#define TS_MINX 200
#define TS_MINY 200
#define TS_MAXX 3700
#define TS_MAXY 3700
```

Typical values range from 200-3900. Test by tapping buttons and adjusting until alignment is correct.

## Integration with FluidNC Communication

The pendant UI currently uses `dbg_printf()` to output G-code commands. To integrate with actual FluidNC communication:

1. Replace `dbg_printf()` calls with proper FluidNC command functions
2. Update machine state variables from FluidNC status messages
3. Hook into the existing `fnc_poll()` message parsing

Example locations to update in `src/CNC_Pendant_UI.cpp`:
- Line ~939: Home commands
- Line ~981: Spindle start/stop commands
- Line ~1045: Set work zero commands
- Line ~1051: Probing commands

## Customization

### Adding New Screens
1. Add a new enum value to `PendantScreen`
2. Create a `draw[ScreenName]Screen()` function
3. Create a `handle[ScreenName]Touch()` function
4. Add cases to `drawCurrentPendantScreen()` and `handlePendantTouch()`

### Modifying Colors
Edit the color definitions in `src/cnc_pendant_config.h`:
```cpp
#define COLOR_BACKGROUND 0x0000
#define COLOR_TITLE 0xFD20
// etc.
```

### Changing Button Layout
Modify button positions and sizes in the `draw*Screen()` functions in `src/CNC_Pendant_UI.cpp`.

## Troubleshooting

### Display Issues
- Verify display rotation in `cnc_pendant_config.h`
- Check that LovyanGFX is properly initialized by existing hardware code

### Touch Not Responding
- Verify touch calibration values
- Check that touch driver is initialized by `Hardware2432.cpp`
- The existing touch initialization should work automatically

### Physical Buttons Not Working
- Verify `CYD_BUTTONS` is defined in `platformio.ini` for your environment
- Check button pin connections
- Button pins are shared with RGB LED, ensure no conflicts

### Compilation Errors
- Ensure all three files are in the `src/` directory
- Verify LovyanGFX library is installed (already in `platformio.ini`)
- Check that you're building for a CYD environment (not `m5dial`)

## Notes

- The pendant UI is designed to be independent of the existing FluidDial UI
- It uses the same hardware initialization and drivers
- Machine state is maintained internally and should be synchronized with FluidNC
- The UI is optimized for 240x320 portrait displays
- All drawing is done directly to the display, not through sprites

## Future Enhancements

Potential improvements:
1. Integrate with existing FluidNC communication layer
2. Add real-time machine state updates from FluidNC
3. Implement actual SD card file reading
4. Add configurable macros that can be saved/loaded
5. Implement advanced probing sequences
6. Add work offset management
7. Create settings screen for calibration and preferences

## License

This pendant UI maintains compatibility with the GPLv3 license of the FluidDial-CYD project.
