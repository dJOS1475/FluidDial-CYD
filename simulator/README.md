# FluidDial-CYD UI Simulator

A browser-based simulator for the FluidDial-CYD pendant's **new UI** (the
per-screen `src/screens/*` build, env `cyd_new_ui`). It lets you iterate on the
UI without flashing a physical pendant — every screen, touch target, encoder
behaviour and navigation path is reproduced in JavaScript.

It runs entirely client-side (no build step, no toolchain) and lives in this
isolated `simulator/` folder so it never touches the firmware build.

## Running it

**Recommended — live reload.** A tiny zero-dependency dev server watches every
file under `simulator/` and auto-refreshes the browser whenever you edit a
screen `.js`, the CSS, or `index.html`:

```bash
python3 simulator/dev_server.py 8777   # then open http://localhost:8777
```

Edit any file → the page reloads itself, and you stay on the same screen with
the same simulated state (it's snapshotted to `sessionStorage`). Hit **Reset
state** in the bench panel to clear that and start fresh.

**Plain static** also works (no live reload) — any static server, or open
`simulator/index.html` directly (file:// is fine):

```bash
python3 -m http.server 8777 --directory simulator
```

If you use Claude Code's preview, a launch config is already provided
(`.claude/launch.json`, server name `simulator`) and it uses the live-reload
server.

## Using it

- **Touch** — click anywhere on the screen; taps hit the same rectangles the
  firmware's `handle*Touch()` functions test.
- **Encoder / handwheel** — drag the silver knob, scroll over it, use the
  **−/＋** buttons, or the **arrow keys**. Emits the same encoder deltas the
  firmware's `handleEncoderDelta()` consumes (jog, spindle RPM dial, probe
  field adjust, feed/spindle override, FluidNC screen rotate).
- **Buttons** — the red/yellow/green hardware buttons (E-Stop / Pause /
  Cycle-Start). *These are sim approximations* — the real button handler lives
  in `ardmain.cpp`, which isn't ported; they log the realtime byte they'd send.
- **Bench panel (right)** — drives the simulated machine + comms state a real
  FluidNC controller and the pendant hardware would supply: connection/link
  state, transport (UART/WiFi), machine status (`Idle`, `Run`, `Alarm:n`, …),
  axis count, units, work/machine positions, feed & spindle, current job +
  progress, and (in WiFi mode) battery % and WiFi signal/AP state. Change any
  control and the active screen repaints.
- **Controller Log (bottom)** — every G-code line, realtime override and
  request the UI emits (`send_line`, `fnc_realtime`, file/macro requests).

## How faithful is it?

This is a **pixel-faithful re-implementation**, not the compiled firmware, but
it's built to reproduce hardware rendering quirks — especially **text sizing,
overflow and clipping**:

- **Real font.** `js/font.js` embeds the actual Adafruit GLCD 5×7 bitmap (the
  same table LovyanGFX uses as its built-in Font 0), with the device's metrics:
  `textWidth == nchars*6*size`, `fontHeight == 8*size`, and **no word
  wrapping**. A string that's too wide for its panel overflows and gets clipped
  exactly as it does on the panel.
- **No anti-aliasing.** `js/lgfx.js` renders at 1:1 device pixels and rasterises
  every primitive with integer fills. `fillRoundRect` / `drawRoundRect` /
  circles use the Adafruit-GFX midpoint algorithms, so corner pixels land where
  they do on hardware. The canvas is CSS-upscaled with nearest-neighbour, so
  you see the actual LCD pixel grid.
- **Hard 240×320 clipping.** Anything drawn past the panel edge is cut off here
  too.
- **Exact colours.** Colour constants are the firmware's raw RGB565 values
  (`js/colors.js`), converted to CSS the same way the panel converts them.

What it does **not** model: the real touch-controller calibration/jitter, exact
LovyanGFX sprite double-buffering (the sim always takes the direct-draw path,
which is what the firmware falls back to under heap pressure — visually
identical), FreeRTOS timing, and the actual button-handler logic in
`ardmain.cpp`.

## Layout / structure

```
simulator/
  index.html            page shell: chassis, screen canvas, buttons, wheel, bench, log
  css/style.css         chassis + bench styling
  js/
    colors.js           RGB565 constants  (mirrors cnc_pendant_config.h + screen_probe.h)
    font.js             Adafruit GLCD 5×7 bitmap font (= LovyanGFX Font 0)
    lgfx.js             LovyanGFX-compatible canvas engine (no AA, real metrics)
    state.js            machine/jog/probe/... state  (mirrors pendant_shared.h structs)
    stubs.js            platform/comms stand-ins (send_line, ESP, WiFi, NVS via localStorage)
    helpers.js          drawButton/drawTitle/icons + probe helpers (CNC_Pendant_UI.cpp, screen_probe.cpp)
    screens/*.js        one file per screen, a direct port of each src/screens/screen_*.cpp
    controls.js         the bench control panel
    sim.js              screen routing + touch/encoder dispatch (ports CNC_Pendant_UI.cpp)
```

## Keeping it in sync with the firmware

Each `js/screens/<name>.js` is a near-line-for-line port of the matching
`src/screens/screen_<name>.cpp`, using the same coordinates, colours and touch
rectangles. Two helpers keep the sim aligned with firmware code changes:

### `sync.py` — trigger an update from firmware code

```bash
python3 simulator/sync.py            # regen colours + report what drifted
python3 simulator/sync.py --accept   # ...and mark firmware as reviewed
```

- **Colours are regenerated automatically.** It re-reads the colour `#define`s
  from `src/cnc_pendant_config.h` and `src/screens/screen_probe.h` and rewrites
  the generated block in `js/colors.js`. Tweak a colour in the firmware → it
  shows up in the sim with zero manual work. (Don't hand-edit between the
  `// === BEGIN/END GENERATED COLORS ===` markers.)
- **Screen logic can't be auto-transpiled**, so instead `sync.py` tracks a
  content hash of every firmware screen file and reports exactly which ones
  changed since the last `--accept` — i.e. which JS ports you need to update,
  paired with their file paths. After you update the port(s), run
  `--accept` to clear the report.

### Live, while the dev server runs

When you run `dev_server.py`, it also **watches the firmware files** under
`src/`. Edit a `screen_*.cpp` (or the colour headers) and:

- colours regenerate and the page reloads automatically, and
- a **banner** appears at the top of the sim listing exactly which firmware
  files changed and which JS port each one feeds — so you always know when a
  sim screen has fallen behind the firmware. Run `python3 simulator/sync.py
  --accept` once you've reconciled them and the banner clears.

### Typical workflow

1. Prototype a layout change in the JS port and check it in the sim (live reload).
2. Mirror the same coordinate changes into the matching `src/screens/screen_*.cpp`.
   (Colour changes only need to be made in the firmware — `sync.py` pulls them
   into the sim.)
3. `python3 simulator/sync.py --accept` to record that the port matches.

Because both sides use the same constants and metrics, a layout that fits in the
sim fits on the device (and one that overflows in the sim overflows there too).

Persistent settings (probe config, jog prefs) are saved to `localStorage`;
**Reset state** in the bench panel clears them and reloads. The firmware-sync
baseline lives in `simulator/.sync_manifest.json`.
