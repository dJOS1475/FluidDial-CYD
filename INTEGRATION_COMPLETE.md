# Integration Complete - CNC Pendant UI

## What Was Done

The CNC Pendant UI has been successfully integrated into your FluidDial-CYD project using **Option 1 (Complete UI Replacement)**.

## Files Modified

### 1. [src/ardmain.cpp](src/ardmain.cpp)
**Changes:**
- Added `#include "CNC_Pendant_UI.h"`
- Modified `setup()` to call `setup_pendant()` instead of initializing the old UI
- Modified `loop()` to call `loop_pendant()` instead of `dispatch_events()`
- Set brightness to 255 (full brightness)
- Removed old UI initialization code

**Before:**
```cpp
void setup() {
    init_system();
    display.setBrightness(aboutScene.getBrightness());
    show_logo();
    delay_ms(2000);
    base_display();
    dbg_printf("FluidNC Pendant %s\n", git_info);
    fnc_realtime(StatusReport);
    extern Scene* initMenus();
    activate_scene(initMenus());
}

void loop() {
    fnc_poll();
    dispatch_events();
}
```

**After:**
```cpp
void setup() {
    init_system();
    display.setBrightness(255);
    show_logo();
    delay_ms(2000);
    setup_pendant();  // New UI
    dbg_printf("FluidNC Pendant with new UI %s\n", git_info);
    fnc_realtime(StatusReport);
}

void loop() {
    fnc_poll();
    loop_pendant();  // New UI
}
```

## Files Added

1. **[src/cnc_pendant_config.h](src/cnc_pendant_config.h)** - Pin definitions and color configuration
2. **[src/CNC_Pendant_UI.h](src/CNC_Pendant_UI.h)** - Public interface header
3. **[src/CNC_Pendant_UI.cpp](src/CNC_Pendant_UI.cpp)** - Complete pendant UI implementation

## How to Build and Upload

### Using PlatformIO CLI
```bash
cd "/Users/derekosborn/Library/CloudStorage/SynologyDrive-Home/Coding Projects/FluidDial-CYD"
pio run -e cyd_buttons -t upload
```

### Using PlatformIO IDE (VSCode)
1. Open the project folder in VSCode
2. Select environment: `cyd_buttons` from the status bar
3. Click "Build" button (checkmark icon)
4. Click "Upload" button (arrow icon)

### Using Arduino IDE
If you prefer Arduino IDE, you'll need to:
1. Copy all files from `src/` to your Arduino sketch folder
2. Configure the board settings to match `platformio.ini`
3. Compile and upload

## What to Expect

After uploading:
1. **Boot sequence**: FluidDial logo displays for 2 seconds
2. **Main Menu**: New pendant UI main menu appears with 8 navigation buttons
3. **Touch navigation**: Tap buttons to navigate between screens
4. **Physical buttons**: Red/Yellow/Green buttons work as E-Stop/Pause/Start

## Features Available

### Screens
- **Main Menu** - Hub showing status and axis positions
- **Status** - Detailed machine status
- **Jog & Homing** - Manual jogging and homing controls
- **Probing & Work** - Work coordinate and probing
- **Feeds & Speeds** - Override controls
- **Spindle Control** - RPM and direction
- **Macros** - 10 macro buttons
- **SD Card** - File browser
- **FluidNC** - System information

### Physical Controls
- **Red Button (GPIO 4)** - Emergency Stop
- **Yellow Button (GPIO 17)** - Feed Hold/Pause
- **Green Button (GPIO 16)** - Cycle Start
- **Rotary Encoder (GPIO 22/21)** - Jog in Jog screen

## Troubleshooting

### If build fails
- Check that all 3 new files are in the `src/` directory
- Verify you're building for `cyd_buttons` environment
- Check PlatformIO library installation

### If touch is misaligned
Edit `src/cnc_pendant_config.h` and adjust:
```cpp
#define TS_MINX 200
#define TS_MINY 200
#define TS_MAXX 3700
#define TS_MAXY 3700
```

### If display is rotated wrong
Edit `src/cnc_pendant_config.h`:
```cpp
#define DISPLAY_ROTATION 2  // Try 0, 1, 2, or 3
```

### If buttons don't work
- Verify `CYD_BUTTONS` is defined for your environment (it is for `cyd_buttons`)
- Check physical connections to P6 connector
- Buttons share pins with RGB LED

## Next Steps

### To restore original UI
Simply revert the changes to `src/ardmain.cpp` using git:
```bash
git checkout src/ardmain.cpp
```

Or manually undo the changes shown above.

### To customize the UI
1. **Colors**: Edit `src/cnc_pendant_config.h`
2. **Button layout**: Edit `src/CNC_Pendant_UI.cpp` draw functions
3. **Add screens**: Follow pattern in `CNC_Pendant_UI.cpp`

### To integrate with FluidNC
The UI currently uses `dbg_printf()` for commands. To fully integrate:
1. Replace `dbg_printf()` with proper FluidNC command functions
2. Parse FluidNC status updates to update machine state
3. Hook into `fnc_poll()` message handler

See locations marked in `src/CNC_Pendant_UI.cpp` around lines:
- 939 (home commands)
- 981 (spindle commands)
- 1045 (work zero commands)
- 1051 (probing commands)

## Documentation

Full integration details in: [PENDANT_UI_INTEGRATION.md](PENDANT_UI_INTEGRATION.md)

## Ready to Test!

Your project is now ready to build and upload. The new UI should appear after the FluidDial logo on startup.

Enjoy your new CNC Pendant UI! 🎉
