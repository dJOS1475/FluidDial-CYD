# PiBot Pendant V4.2 target

**Status: experimental, not hardware-validated.** This is firmware for the
pendant's classic ESP32, not firmware for the PiBot V5.88 motion controller.
Successful builds do not establish safe behaviour on a machine.

## Scope

`pibot_v42` shares the new UI, ESP-NOW protocol, UART backend and partition
layout with the standard CYD target. PiBot-only hardware changes are guarded
by `PIBOT_PENDANT`; standard CYD touch detection and battery support remain intact.
The default PlatformIO environments have not been changed.

Implemented for initial bring-up:

- ILI9341 display, PiBot display inversion, backlight on GPIO21.
- FT6336U capacitive touch through LovyanGFX's FT5x06 driver, polling on I2C1.
- Handwheel on GPIO22/27.
- Three active-low buttons: Cancel, Pause and Start/Resume.
- Existing ESP-NOW pairing and transport-selection UI; UART remains the fresh-install default.
- Screen orientation: PiBot default rotation is 0 (the generic CYD default of 2 showed
  the PiBot screen upside down on the bench). An unsaved pendant uses 0; a pendant that
  already stored a rotation keeps it, and the Rotate toggle on the FluidNC screen works.
- PiBot-specific development version label (`v2.3.3-pibot-dev`).
- UART0 at 1,000,000 baud, preserving the PiBot source's RX/TX assignment.
- Red-button deep-sleep wake pad released back to digital input at boot.

Selector switch and potentiometer (Jog screen only, never sends a command by itself):

- **3-position selector (GPIO34 / 35 / 39) = jog axis.** Assumes one pin pulled to GND
  per position (X, Y, Z in pin order; reorder `pibot::selector_axis` in
  `src/PiBotControls.h` if the knob turns the other way). Only a debounced *change*
  (3 x 20 ms) selects an axis, so touch selection still works until the knob is
  turned; no pin low or several low (knob between detents) is ignored. An axis the
  machine does not have is ignored, and it is not applied while the controller
  reports `Jog`.
- **Potentiometer (GPIO26) = jog speed cap**, the same value the speed box and
  handwheel adjust (metric 1000 mm/min up to $110 in 500 steps; imperial 40 ipm up to
  the equivalent in 20 ipm steps). It is *armed* only after moving about 4 % from its
  power-up position, so booting never overrides the speed you set; afterwards 1.5 %
  hysteresis. Fully clockwise reaches the maximum. Ignored while the handwheel is in
  speed or coarse-increment adjust mode.
- **Known risk, ADC2:** GPIO26 is on ADC2, which the Wi-Fi driver also uses; ESP-NOW
  keeps the radio running. Failed ADC reads are discarded (never read as 0), but the
  pot may be sluggish or unresponsive while ESP-NOW is active. If so, UART mode works
  reliably; GPIO26 cannot be remapped on this board.
- The PiBot manual only says "three-position switch, pull-ups" and does not say how
  positions map to pins; PiBot's own firmware does not read either control. All of the
  above is therefore **untested on hardware**. Bench check: turn the knob through its
  positions on the Jog screen and confirm X, Y, Z highlight in turn; sweep the pot and
  watch the Speed box. Tell us if the order is reversed or a position is skipped.

Deliberately not implemented:

- Battery percentage, charge detection or PMIC control. GPIO39 is a selector input,
  **not** the standard CYD battery ADC. The battery icon is hidden rather than invented.
- Speaker support, dedicated toolsetter routines or manual-ATC integration.
- USB debug builds: the generic CYD debug UART steals PiBot backlight/selector pins;
  using `DEBUG_TO_USB` with this target fails compilation instead.

## Pin map (pendant side)

| Function | GPIO / configuration |
|---|---|
| Display SCLK / MOSI / MISO | 14 / 13 / 12 |
| Display CS / DC / backlight | 15 / 2 / 21 |
| Touch SDA / SCL | 32 / 25, I2C1, 400 kHz |
| Touch IRQ / reset | Not used; polling |
| Encoder A / B | 22 / 27 |
| Cancel / Pause / Start | 4 / 16 / 17, active low with pull-ups |
| UART RX / TX | 1 / 3, UART0, 1,000,000 8N1 |
| Potentiometer | 26 (ADC2 ch 9), jog speed |
| Selector | 34 / 35 / 39, jog axis, external pull-ups |

GPIO21 must never be used as encoder B on this target, and GPIO27 must never be
driven as backlight PWM. GPIO34 is not the generic CYD photoresistor/UI lock.
Button placement, encoder direction, touch alignment after the rotation change and UART jumper direction
still require checking against the actual V4.2 unit.

## Build

With PlatformIO Core installed:

```sh
pio run -e pibot_v42
pio run -e pibot_v42 -t build_merged
```

The merged factory image is `.pio/build/pibot_v42/merged-flash.bin`, intended for
offset `0x0`. It contains the bootloader, partitions, application and LittleFS UI
assets. **Writing this merged image replaces the NVS region**, losing settings
and pairings. Application-only upload does not install the UI assets on a fresh unit.

The merge script copies PiBot images into `docs/pibot_v42/` (ignored build output),
never over the generic CYD images in `docs/`. There is no PiBot web-installer entry
yet; the upstream installer still serves generic CYD firmware.

Regression build and host checks:

```sh
python3 -m unittest discover -s test -p 'test_*.py' -v
pio run -e pibot_v42 -e cyd_new_ui_combined
```

The host checks require PlatformIO and a host C++ compiler (`c++`, or `CXX`).

### Local verification (2026-10-22)

- `pibot_v42` and `cyd_new_ui_combined`: firmware builds passed.
- `pibot_v42 -t build_merged`: passed; generated a 4 MiB image and copied all six
  install artifacts to the PiBot-specific directory without changing generic CYD images.
- Seven host regression tests passed, including unsafe-flag rejection and artifact isolation.
- PiBot application: 93,136 bytes static RAM (28.4%), 1,133,829 bytes flash (43.3%
  of the app partition). These figures do not describe runtime free heap.
- Resolved build dependencies included Arduino-ESP32 2.0.17, GrblParser `9108f54`
  and LovyanGFX `6feea10` (the upstream `develop` reference remains moving).
- Merged image SHA-256:
  `88c861b3970097f027d6e9737967d4d75c461d4bebb4542ffa6051c07a166d5c`.

No flashing, pairing, motion tests or on-device validation have been performed.
Rebuilding after source or dependency changes will produce a different checksum.

## Bench and machine commissioning

1. Back up the pendant's existing firmware/settings before replacing them. Disconnect
   RJ12 and switch the battery off before connecting USB for installation. V4.2 normally
   enters download mode automatically; no firmware is flashed by the build commands above.
2. With the pendant disconnected from the machine, verify display colours, brightness,
   touch alignment across the whole screen, encoder direction, and each button's role.
   Turn the selector and potentiometer on the Jog screen (see above) and confirm axis and speed follow.
3. Verify red-button sleep/wake and repeated cold boots. Battery readings are unavailable;
   use the hardware's indicator and physical battery-disconnect switch as appropriate.
4. On the controller, verify `$espnow/list` is accepted. Stock FluidNC Wi-Fi builds from
   v4.0.4 support this channel; the V5.88 needs the ESP32-S3 build, not this pendant image.
   No UART YAML configuration is needed for ESP-NOW.
5. Select **Main Menu → FluidNC → CONNECTION → ESP-NOW** on the pendant, let it restart,
   then choose **Pair New**. Run `$espnow/pair` on the controller and select the saved machine.
   Keep the controller's Wi-Fi AP/STA mode stable; switching modes can require re-pairing.
6. With spindle/laser power disconnected, verify status updates and button functions
   before enabling motor power. Then test small, low-speed jogs with ample clearance.
7. Test loss of radio, pendant power loss, reconnection, and cold boots. Never assume
   radio loss stops an active job. Verify no buffered or unexpected motion on reconnect.

Cancel is a **software reset**, not a safety-rated emergency stop. Preserve an independent,
hard-wired machine E-stop. Initial testing must not depend on the pendant for safety.

For later RJ12 testing, disconnect all machine power before wiring, use the approved
PiBot cable, and keep the pendant battery switch down (electrically disconnected).
The UART shares USB serial; do not attach RJ12 while flashing or using a USB serial console.
Match the controller's UART baud rate and correct RX/TX direction at one jumper point only.

## Hardware references

- [PiBot Pendant V4.2 manual and developer pin map](https://wiki.pibot.com/doku.php?id=pibot_cnc_laser_series:cnc_pendant_v42:start)
- [PiBot FluidDial hardware implementation](https://github.com/abcpibot/FluidDial/blob/v1.5.0-pibot/src/Hardware2432.cpp)
- [PiBot FluidDial UART assignment](https://github.com/abcpibot/FluidDial/blob/v1.5.0-pibot/src/Hardware2432.hpp)

The vendor capacitive code configures backlight GPIO27 even though its PiBot encoder
also uses GPIO27. This target intentionally uses the manual's GPIO21 backlight instead.
