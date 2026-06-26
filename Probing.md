# FluidDial-CYD — Probing Guide

A per-screen reference for the probing workflow: what each screen shows, what every
option does, and how each routine actually moves the machine.

Probing is reached from **Main Menu → Probing**. You land on the **Probe hub**,
pick a **probe type**, optionally open **Configure** to set up that probe, then
launch one of the **routines** (Z Surface, XYZ Corner, Bore, Boss). Each routine
generates its own G-code on the pendant and runs it on the controller — no probe
macro files need to live on the FluidNC SD card.

---

## Concepts to know first

These apply across all the routine screens.

**Probe types.** Three are supported, chosen on the hub:
- **Z-Height Touch Plate** — a conductive plate of known thickness laid on the work.
- **XYZ Touch Plate** — an L-shaped corner block that wraps a corner of the stock.
- **3D Touch Probe** — a spindle-mounted probe with a ruby ball on a stylus.

The probe type gates which routines are offered (see the hub, below).

**Trigger compensation.** When a probe triggers, the machine position is the
*contact* point, not the workpiece surface/edge. The pendant offsets the zero it
writes so the WCS lands on the real surface:
- **3D probe** → offset = **ball radius** (`Ball dia ÷ 2`).
- **Touch plate** → offset = **plate thickness**.

> The **XYZ-plate XY offsets** are also applied — they compensate the corner probe
> for the plate's wall thickness on each axis (see [Calibration](#calibration)).
> The remaining config fields (stylus length, deflection, pre-travel, plate width)
> are saved for reference but are **not currently applied** to the generated moves.

**Two-pass, crash-safe probing.** Every wall/surface is found with two moves:
a **fast seek** (at *Seek rate*) to make first contact, a small back-off, then a
**slow re-probe** (at *Probe rate*) for an accurate trigger. Crucially, *every*
approach toward material is a `G38.2` probing move that **stops on contact** —
there are no blind rapids into a wall, so a wrong "nominal diameter" can't crash
the tip (it just changes how far the seek travels).

**Confirm before it moves.** On every routine screen, tapping **Probe** raises a
confirmation overlay (Cancel / Confirm). Nothing moves until you confirm. When the
routine starts, the pendant switches to the **Status** screen so you can watch it.

**Work Area button (which WCS gets zeroed).** Every routine screen has a **WORK
AREA** button (bottom-right) showing **G54–G57**. Tap it to cycle the coordinate
system the probe will write its zeros into (`G10 L20 P1…P4`). This is a *selection
only* — it does not change the machine's currently-active WCS, it just chooses
which system the routine zeroes.

**Sequence list + diagram.** The left column of each routine screen shows a
numbered **sequence** of what the routine will do, and a small **diagram** of the
probe move (a probe descending onto the feature, with arrows showing the probe
directions).

**Shared settings** (set once on the hub, used by every routine):

| Setting | Units | Meaning |
|---|---|---|
| **Probe rate** | mm/min | Feed for the slow, accurate *re-probe* pass. |
| **Seek rate** | mm/min | Feed for the initial fast *seek*. Default **500 mm/min**. |
| **Retract** | mm | How far the probe backs off / lifts after a touch. |
| **Max Z travel** | mm | Maximum distance a Z probe searches before giving up (a safety limit — the probe alarms if it finds nothing within this distance). |

---

## 1. Probe hub — the **PROBE** screen

The entry point. Top to bottom:

- **Probe-type selector** (3 segmented buttons): **Z Plate · XYZ Plate · 3D Probe**.
  Tap one to select it (it highlights yellow). The available routines change to
  match the selected type.
- **SHARED SETTINGS** panel — the 2×2 grid of *Probe rate · Seek rate · Retract ·
  Max Z travel*. Tap a field to focus it (it highlights), then turn the dial to
  adjust; tap again to release. These values apply to every routine.
- **PROBE ROUTINES** panel — the routines available for the selected type:
  - **Z-Height Plate** → **Z Surface** only.
  - **XYZ Plate** → **Z Surface**, **XYZ Corner**.
  - **3D Probe** → **Z Surface**, **XYZ Corner**, **Bore**, **Boss**.
- **Bottom row**: **Main Menu** (left) · **Configure** (right). *Configure* opens
  the setup screen for the currently-selected probe type.

---

## 2. Configure screens

Opened with **Configure** on the hub. Which one appears depends on the selected
probe type. Each shows a diagram of the probe for reference. Tap a field to focus,
dial to adjust.

### 3D Touch Probe config (**PROBE CONFIG**)

| Field | Meaning |
|---|---|
| **Ball dia.** | Diameter of the ruby ball. Its **radius** is the trigger offset applied to every routine, so this is the one field that affects results — set it accurately. |
| **Stylus length** | Physical stylus length. Recorded; not applied to moves. |
| **Deflection** | Stylus deflection allowance. Recorded; not applied to moves. |
| **Pre-travel** | Probe pre-travel allowance. Recorded; not applied to moves. |

### Touch Plate config (**PROBE CONFIG**)

| Field | Applies to | Meaning |
|---|---|---|
| **Thickness** | both plate types | Plate thickness — used as the Z trigger offset so Z0 lands on the work surface *under* the plate. |
| **Width** | XYZ plate only | Plate dimension. Recorded; not applied to moves. |
| **XY offset X** | XYZ plate only | The plate's wall thickness on the X face. **Applied** to the corner probe so X0 lands on the workpiece edge, not the plate face. |
| **XY offset Y** | XYZ plate only | The plate's wall thickness on the Y face. **Applied** to the corner probe so Y0 lands on the workpiece edge. |

---

## 3. Z Surface

Zeroes **Z** on a flat surface (works with any probe type).

**Position before probing:** jog the probe/tool a little above the surface (or the
plate) you want to zero on.

**Left column** — Sequence: ① *Fast seek -Z* → ② *Slow re-probe* → ③ *Set Z0*,
with a diagram of the probe descending onto a surface.

**Right column — SETTINGS:**

| Field | Meaning |
|---|---|
| **Max Z travel** | How far down to search for the surface before giving up. |
| **Retract dist** | How far to lift after setting Z0. |

**Result line:** *Sets Z0* (into the selected Work Area).

**How it works:** seeks down (fast) until contact, backs off, re-probes down
(slow), then `G10 L20 P# Z<offset>` sets Z0 with the ball-radius / plate-thickness
offset applied, and lifts by *Retract dist*.

---

## 4. XYZ Corner

Finds a workpiece **corner** and sets **X0, Y0 and Z0** there. (Always probes all
three axes.) Available for the XYZ plate and the 3D probe.

**Position before probing:** place the probe just **above and outside** the corner
you're zeroing, slightly off both edges.

**Left column** — Sequence: ① *Touch top→Z0* → ② *Probe X & Y* → ③ *Set X0 Y0 Z0*,
with a top-down diagram of the corner and the X/Y probe arrows.

**Right column — SETTINGS:**

| Field | Meaning |
|---|---|
| **CORNER** (selector) | Tap to cycle **Bot-Left · Bot-Right · Top-Left · Top-Right** — which corner you're on. This sets the directions the probe approaches the two edges. |
| **Probe depth** | How far *below the top surface* to drop before probing the side edges, so the ball is beside the material when it reaches across. |
| **Overshoot** | Extra approach distance past the nominal edge, so the seek reliably reaches the wall. The probe first steps out by this amount, then seeks back in. |
| **XY retract** | How far to back off an edge after probing it. |

**Result line:** *Sets X0 Y0 Z0*.

**How it works:** probes the **top** to set Z0 (with offset), retracts, drops by
*Probe depth*, then probes the **X** edge (two-pass) and sets X0, and the **Y**
edge (two-pass) and sets Y0 — each with edge compensation (ball radius for the 3D
probe, or the plate's XY-offset wall thickness for the XYZ plate) — then lifts.

---

## 5. Bore (3D probe only)

Finds the **centre of an inside circle / hole** and sets **X0/Y0** there. **No Z
motion at all** — set Z separately with the Z Surface routine.

**Position before probing:** lower the probe tip **inside the bore**, roughly
centred, at any comfortable depth. The warning on screen reads *"Place tip inside
the bore."*

**Left column** — Sequence: ① *Probe XY walls* → ② *Re-centre on X* → ③ *Set X0 Y0*,
with a cross-section diagram of the probe inside a pocket and outward arrows.

**Right column — SETTINGS:**

| Field | Meaning |
|---|---|
| **Nominal dia.** | Approximate bore diameter. Only a **travel hint** for how far the seek reaches toward a wall — because every move is a `G38.2` probe, this doesn't have to be exact and a wrong value won't crash the tip. |
| **Wall offset** | Extra seek over-travel margin added to the nominal radius, so the seek comfortably reaches the wall. |

The right column also notes that **Z work-zero is set via the Z Surface routine**.

**Result line:** *Sets X0 Y0*.

**How it works:** two-pass probes **+X** then **−X** (probing *across* the hole,
never rapiding), **moves to the computed X centre**, then two-pass probes **+Y**
and **−Y** *through that true centre* (so Y is square to the wall), moves to the
final XY centre, and `G10 L20 P# X0 Y0`. Because the centre is the average of
opposing walls, the ball radius cancels — no edge offset is needed.

---

## 6. Boss (3D probe only)

Finds the **centre of an outside circle / round stock** and sets **X0, Y0 and Z0**.

**Position before probing:** start with the probe **above the centre of the boss**.
The warning reads *"Start above centre of boss."*

**Left column** — Sequence: ① *Touch top→Z0* → ② *Probe XY walls* → ③ *Set X0 Y0*,
with a cross-section diagram of the probe over a raised boss and inward arrows.

**Right column — SETTINGS:**

| Field | Meaning |
|---|---|
| **Nominal dia.** | Approximate boss diameter — a travel hint for the inward seeks. |
| **Probe depth** | How far *below the boss top* to probe the side walls. |
| **Clearance** | How far *outside* the nominal radius to move before plunging down beside the boss. **Must exceed any under-estimate of the diameter** — this is the one number that has to be safe, because the downward plunge beside the boss can't be probe-protected. |

**Result line:** *Sets X0 Y0 Z0*.

**How it works:** touches the **flat top** to set **Z0** (with offset). Then for
each side it moves clear of the boss by *Clearance*, plunges to *Probe depth*
beside it, and two-pass probes inward to the wall. It **re-centres on X before the
Y pair** (so Y runs through the true centre), lifts straight up between sides (so
the tip is never carried back over the boss), then moves to the computed centre and
`G10 L20 P# X0 Y0`. Z0 from the top is preserved.

---

## 7. Work Area — manual work-zero (the **Probing Work** screen)

Reached from **Main Menu → Work Area**. This is the manual companion to the probe
routines — use it to pick the coordinate system and zero axes by hand (e.g. after
jogging to a known point).

- **G54 · G55 · G56 · G57** buttons — tap to select the active coordinate system
  (the selected one is highlighted). This is the same WCS the probe routines'
  **Work Area** button selects. Selecting here also switches the machine's active
  system on the controller.
- Live **work-coordinate readout** for the current axes.
- **Set Work Zero** row — one button per axis (**X · Y · Z · A**) plus **ALL**.
  Tapping a button zeroes that axis (or all axes) in the selected WCS at the
  current position (`G10 L20 P# …0`).
- **Main Menu** · **Jog** at the bottom.

---

## Calibration

The only config values that change *where a zero lands* are the **ball radius**
(3D probe) and the **plate thickness / XY offsets** (touch plate). Get these right
and every routine is accurate — the rest (stylus length, deflection, pre-travel,
plate width) don't affect the result.

> Centre-finding (**Bore**, **Boss**) is self-calibrating for radius: the offset
> cancels when opposing walls are averaged, so radius accuracy only matters for
> **Z Surface** and the **Corner** edges.

### 3D probe — effective ball radius

The number that matters is the *effective* radius — the nominal ball radius plus the
probe's pre-travel (the tiny lag between contact and trigger). Calibrate it against a
known reference rather than trusting the printed ball size:

1. Set **Ball dia.** to the manufacturer's nominal value.
2. Run **XYZ Corner** against a known straight edge / precision square at a known position.
3. Check where the set **X0** (or Y0) actually landed versus the true edge — touch off
   with an edge finder/dowel, or sweep an indicator. Call that single-edge error **Δ**.
4. Adjust **Ball dia.** by **2 × Δ** (the radius is half the diameter), in the
   direction that moves the zero onto the true edge: if the zero sits *outside* the
   material the probe over-compensated → **reduce** Ball dia.; if it's *inside* →
   **increase** it.
5. Re-probe and confirm; iterate once or twice. Keep the final pass slow (a low
   **Probe rate**) so pre-travel stays small and repeatable.

A ring/plug gauge or a 1-2-3 block makes a good reference. This single calibrated
ball-diameter value absorbs pre-travel, which is why those fields aren't entered
separately.

### XYZ touch plate — plate offsets

The plate adds material between the tool and the true edge, so each offset is just a
physical thickness:

- **Thickness** (Z) — the plate's flat thickness. Measure it with calipers and enter it.
- **XY offset X / Y** — the thickness of each *vertical* wall of the L-block. Measure
  each wall with calipers and enter it, or calibrate exactly like the 3D probe above
  but adjust the offset by **1 × Δ** (the plate offset is applied directly, not halved).

---

## Quick reference — what each routine sets

| Routine | Probe types | Sets | Z motion |
|---|---|---|---|
| **Z Surface** | all | Z0 | probes down |
| **XYZ Corner** | XYZ plate, 3D | X0 Y0 Z0 | top touch + side edges |
| **Bore** | 3D | X0 Y0 | none (Z via Z Surface) |
| **Boss** | 3D | X0 Y0 Z0 | top touch + plunge beside walls |

All zeros are written to the coordinate system shown on the screen's **Work Area**
button (G54–G57), using two-pass crash-safe probing throughout.
