#!/usr/bin/env python3
"""
sync.py — keep the simulator in step with the FluidDial-CYD firmware.

What it does automatically:
  • COLOURS — re-reads the colour #defines from src/cnc_pendant_config.h and
    src/screens/screen_probe.h and regenerates the marked block in
    js/colors.js.  A colour tweak in the firmware shows up in the sim with no
    manual work.

What it can't do automatically (and instead reports):
  • SCREEN LOGIC — each js/screens/<x>.js is a hand-port of the matching
    src/screens/screen_<x>.cpp.  Arbitrary C++ drawing/touch logic can't be
    transpiled reliably, so this script tracks a content hash of every firmware
    screen file and tells you which ones changed since you last ran
    `sync.py --accept` — i.e. exactly which JS ports need a manual update.

Usage:
    python3 simulator/sync.py            # regen colours + print staleness report
    python3 simulator/sync.py --accept   # ...and mark all firmware files as
                                         #    reviewed (clears the report)

The dev server (dev_server.py) imports this module to do the same thing live
whenever a firmware file changes, and exposes the report at /__sync_status.
"""
import hashlib
import json
import os
import re
import sys

SIM_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(SIM_DIR)
SRC = os.path.join(REPO, "src")
SCREENS = os.path.join(SRC, "screens")
COLORS_JS = os.path.join(SIM_DIR, "js", "colors.js")
MANIFEST = os.path.join(SIM_DIR, ".sync_manifest.json")

GEN_BEGIN = "// === BEGIN GENERATED COLORS (sync.py — from firmware #defines) ==="
GEN_END = "// === END GENERATED COLORS ==="

# firmware file (relative to src/)  ->  simulator JS port it feeds
PORT_MAP = {
    "screens/screen_main_menu.cpp": "js/screens/menu.js",
    "screens/screen_status.cpp": "js/screens/status.js",
    "screens/screen_jog_homing.cpp": "js/screens/jog.js",
    "screens/screen_probing_work.cpp": "js/screens/work.js",
    "screens/screen_feeds_speeds.cpp": "js/screens/feeds.js",
    "screens/screen_spindle_control.cpp": "js/screens/spindle.js",
    "screens/screen_sd_card.cpp": "js/screens/sdcard.js",
    "screens/screen_macros.cpp": "js/screens/macros.js",
    "screens/screen_fluidnc.cpp": "js/screens/fluidnc.js",
    "screens/screen_wifi_setup.cpp": "js/screens/wifi.js",
    "screens/screen_probe.cpp": "js/screens/probe.js",
    "screens/screen_probe_z.cpp": "js/screens/probe_z.js",
    "screens/screen_probe_corner.cpp": "js/screens/probe_corner.js",
    "screens/screen_probe_bore_boss.cpp": "js/screens/probe_bore_boss.js",
    "screens/screen_probe_cfg.cpp": "js/screens/probe_cfg.js",
    # shared logic
    "CNC_Pendant_UI.cpp": "js/helpers.js + js/sim.js",
    "screens/pendant_shared.h": "js/state.js",
    # colour sources (regenerated automatically, tracked so a report still notes them)
    "cnc_pendant_config.h": "js/colors.js (auto)",
    "screens/screen_probe.h": "js/colors.js (auto)",
}

_DEFINE_RE = re.compile(r"^\s*#define\s+(\w+)\s+(\S+)")


def _parse_defines(path, prefix):
    """Return ordered [(name, value)] for #defines whose name starts with prefix.
    Inline trailing comments are stripped from the value."""
    out = []
    if not os.path.isfile(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = _DEFINE_RE.match(line)
            if not m:
                continue
            name, val = m.group(1), m.group(2)
            if not name.startswith(prefix):
                continue
            out.append((name, val))
    return out


def build_colors_block():
    config = os.path.join(SRC, "cnc_pendant_config.h")
    probe_h = os.path.join(SCREENS, "screen_probe.h")
    colors = _parse_defines(config, "COLOR_")
    probes = _parse_defines(probe_h, "PROBE_")
    lines = [GEN_BEGIN]
    for name, val in colors:
        lines.append(f"const {name} = {val};")
    if probes:
        lines.append("")
    for name, val in probes:
        lines.append(f"const {name} = {val};")
    lines.append(GEN_END)
    return "\n".join(lines), len(colors), len(probes)


def regen_colors():
    """Rewrite the generated block in js/colors.js. Returns True if it changed."""
    if not os.path.isfile(COLORS_JS):
        return False
    block, nc, npb = build_colors_block()
    if nc == 0:
        return False  # firmware not found / nothing parsed — leave file as-is
    src = open(COLORS_JS, encoding="utf-8").read()
    if GEN_BEGIN in src and GEN_END in src:
        new = re.sub(
            re.escape(GEN_BEGIN) + r".*?" + re.escape(GEN_END),
            block.replace("\\", "\\\\"),
            src,
            flags=re.S,
        )
    else:
        # No markers yet — insert the block right after the file's header comment.
        new = src.rstrip() + "\n\n" + block + "\n"
    if new != src:
        open(COLORS_JS, "w", encoding="utf-8").write(new)
        return True
    return False


def _hash(path):
    if not os.path.isfile(path):
        return None
    return hashlib.sha1(open(path, "rb").read()).hexdigest()


def _load_manifest():
    try:
        return json.load(open(MANIFEST, encoding="utf-8"))
    except Exception:
        return {}


def compute_report():
    """Return {inSync, changed:[{cpp,js}], missing:[...], total}."""
    manifest = _load_manifest()
    changed, missing = [], []
    for rel, js in PORT_MAP.items():
        path = os.path.join(SRC, rel)
        h = _hash(path)
        if h is None:
            missing.append(rel)
            continue
        if manifest.get(rel) != h:
            changed.append({"cpp": "src/" + rel, "js": js})
    return {
        "inSync": len(changed) == 0,
        "changed": changed,
        "missing": missing,
        "total": len(PORT_MAP),
    }


def accept():
    manifest = {}
    for rel in PORT_MAP:
        h = _hash(os.path.join(SRC, rel))
        if h is not None:
            manifest[rel] = h
    json.dump(manifest, open(MANIFEST, "w", encoding="utf-8"), indent=2)


def main():
    accept_flag = "--accept" in sys.argv
    if not os.path.isdir(SRC):
        print(f"firmware src/ not found at {SRC} — colours not regenerated.")
    else:
        if regen_colors():
            print("colours: regenerated js/colors.js from firmware #defines")
        else:
            print("colours: already up to date")

    report = compute_report()
    if report["missing"]:
        print("missing firmware files: " + ", ".join(report["missing"]))
    if report["inSync"]:
        print("screens: all JS ports in sync with firmware ✓")
    else:
        print(f"\n{len(report['changed'])} firmware file(s) changed since last sync — "
              "update the matching JS port(s):")
        for c in report["changed"]:
            print(f"  • {c['cpp']:42s} ->  {c['js']}")

    if accept_flag:
        accept()
        print("\nmanifest updated — all firmware files marked as reviewed.")
    elif not report["inSync"]:
        print("\nAfter updating the JS ports, run:  python3 simulator/sync.py --accept")


if __name__ == "__main__":
    main()
