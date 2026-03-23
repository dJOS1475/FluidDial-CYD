# Probe Macros — Setup & Usage

These two macro files are triggered from the **Probe** screen on the FluidDial-CYD pendant.

> The example macros in this folder are sourced from the FluidNC wiki:
> **http://wiki.fluidnc.com/en/config/probe**
> Refer to that page for full documentation on probing configuration and additional macro examples.

---

## Before You Start

### 1. Configure the machine positions
Open each file and update the variables at the top to match your machine:

**Both files share these ETS settings:**
| Variable | Description |
|---|---|
| `#<ets_x_mpos_mm>` | X machine position of your tool length setter |
| `#<ets_y_mpos_mm>` | Y machine position of your tool length setter |
| `#<ets_z_mpos_min_mm>` | Maximum Z travel when probing the ETS (negative) |
| `#<safe_z_mpos_mm>` | Safe Z height for rapid XY moves |
| `#<retract_height>` | How far to retract between seek and feed passes |
| `#<probe_seek_rate_mm_per_min>` | Fast probe speed |
| `#<probe_feed_rate_mm_per_min>` | Slow/accurate probe speed |

**`probe_tool_height.nc` only:**
| Variable | Description |
|---|---|
| `#<chg_x_mpos_mm>` | X machine position of your tool change location |
| `#<chg_y_mpos_mm>` | Y machine position of your tool change location |

### 2. Upload the files to FluidNC
Upload `probe_work_z.nc` and `probe_tool_height.nc` to the FluidNC controller's **local filesystem** using the FluidNC web UI (go to Files → Upload).

> If you prefer to store them on the SD card, update `screen_probing.cpp` in the pendant firmware and change `$Localfs/Run=` to `$SD/Run=`.

---

## probe_work_z.nc — Z Surface Probe

**Purpose:** Sets work Z zero at the tip of the probe, then measures the tool length setter (ETS) to calculate the offset between work Z and the ETS. The result is stored in **G59 Z** for use by the tool change macro.

**When to run:** At the start of a job, with the probe tip positioned over the workpiece.

**Steps:**
1. Move the probe tip over the workpiece manually
2. On the pendant, go to **Probe → Z Surface**
3. The machine will:
   - Probe down to find the work surface and set Z zero
   - Move to the ETS and measure it
   - Calculate and store the ETS offset in G59 Z
4. Z zero is now set and the ETS offset is stored for tool changes

---

## probe_tool_height.nc — Tool Height (M6 Macro)

**Purpose:** Used as an M6 tool change macro. Moves to a safe tool change position, pauses for the operator to swap the tool, then re-measures the new tool against the ETS and applies a tool length offset (TLO).

**Requires:** `probe_work_z.nc` must have been run first in the current job to establish the G59 Z offset.

**When to run:** When a tool change is required during a job (called automatically if configured as an M6 macro in FluidNC, or manually from the pendant Probe screen).

**Steps:**
1. Machine moves to safe Z, then to the tool change position
2. Machine pauses — **install the new tool**, then press **Green (Cycle Start)** to resume
3. Machine moves to the ETS and measures the new tool
4. Tool length offset is applied automatically via `G43.1`

---

## Configuring as an M6 Macro in FluidNC (optional)

To have FluidNC call `probe_tool_height.nc` automatically on every `M6` command, add the following to your FluidNC `config.yaml`:

```yaml
macros:
  on_m6: "probe_tool_height.nc"
```

This means you no longer need to trigger it manually from the pendant — it will run automatically during any job that contains an `M6` tool change command.
