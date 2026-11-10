"""Host tests for src/PiBotControls.h (selector decode, debounce, pot mapping)."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <cstdio>
#include <cstdlib>
#include "PiBotControls.h"
using namespace pibot;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
int main() {
    // one-hot active low decode
    CHECK(decode_selector(false, true, true) == 0);
    CHECK(decode_selector(true, false, true) == 1);
    CHECK(decode_selector(true, true, false) == 2);
    CHECK(decode_selector(true, true, true) == selector_invalid);     // between detents
    CHECK(decode_selector(false, false, true) == selector_invalid);   // two low
    CHECK(decode_selector(false, false, false) == selector_invalid);

    // debounce: needs 3 identical samples, reports once, ignores invalid and glitches
    SelectorFilter f(3);
    CHECK(f.feed(0) == selector_none);
    CHECK(f.feed(0) == selector_none);
    CHECK(f.feed(0) == 0);
    CHECK(f.feed(0) == selector_none);                  // no repeat
    CHECK(f.feed(selector_invalid) == selector_none);   // knob moving
    CHECK(f.feed(selector_invalid) == selector_none);
    CHECK(f.feed(selector_invalid) == selector_none);
    CHECK(f.feed(0) == selector_none);                  // came back to the same axis
    CHECK(f.feed(0) == selector_none);
    CHECK(f.feed(0) == selector_none);
    CHECK(f.feed(2) == selector_none);
    CHECK(f.feed(1) == selector_none);                  // glitch resets the count
    CHECK(f.feed(2) == selector_none);
    CHECK(f.feed(2) == selector_none);
    CHECK(f.feed(2) == 2);

    // pot mapping
    CHECK(pot_permille(0) == 0);
    CHECK(pot_permille(pot_low) == 0);
    CHECK(pot_permille(pot_high) == 1000);
    CHECK(pot_permille(4095) == 1000);
    CHECK(pot_permille(2047) > 480 && pot_permille(2047) < 520);
    CHECK(pot_to_speed(0, 1000, 10000, 500) == 1000);
    CHECK(pot_to_speed(1000, 1000, 10000, 500) == 10000);
    CHECK(pot_to_speed(500, 1000, 10000, 500) == 5500);   // multiple of step above min
    CHECK(pot_to_speed(1000, 1000, 9700, 500) == 9700);   // never above max
    CHECK(pot_to_speed(300, 1000, 500, 500) == 1000);     // max below min -> min
    CHECK(pot_to_speed(2000, 40, 200, 20) == 200);        // clamps out of range input
    for (int p = 0; p <= 1000; p++) {                     // monotonic and in range
        int v = pot_to_speed(p, 1000, 10000, 500);
        CHECK(v >= 1000 && v <= 10000);
        CHECK(p == 0 || v >= pot_to_speed(p - 1, 1000, 10000, 500));
    }

    // pot filter: unarmed until the knob really moves, then hysteresis
    PotFilter pf(40, 15);
    int out = -1;
    CHECK(!pf.feed(500, &out));      // first reading: baseline only
    CHECK(!pf.feed(510, &out));      // noise
    CHECK(!pf.feed(530, &out));      // still inside arm window
    CHECK(pf.feed(560, &out) && out == 560);   // armed
    CHECK(!pf.feed(565, &out));      // inside hysteresis
    CHECK(pf.feed(580, &out) && out == 580);
    CHECK(pf.feed(0, &out) && out == 0);       // reaching the ends always works
    CHECK(!pf.feed(5, &out));
    CHECK(pf.feed(1000, &out) && out == 1000);
    std::puts("OK");
    return 0;
}
'''


class PiBotControlsTests(unittest.TestCase):
    def test_controls_logic(self):
        cxx = shlex.split(os.environ.get("CXX", "c++"))
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "t.cpp"
            exe = Path(tmp) / "t"
            src.write_text(HARNESS)
            subprocess.run(
                cxx + ["-std=c++11", "-Wall", "-Werror", "-I", str(ROOT / "src"), str(src), "-o", str(exe)],
                check=True,
            )
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
