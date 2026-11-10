// PiBot Pendant V4.2 selector switch and potentiometer: pure decode/filter logic.
// No Arduino dependency, so it is covered by host tests (test/test_pibot_controls.py).
//
// Wiring per the PiBot manual (wiki.pibot.com, "CNC Pendant V4.2", developer section):
//   - 3-position switch on GPIO34 / GPIO35 / GPIO39, external pull-ups.
//   - Potentiometer wiper on GPIO26 (10k, 3.3 V to GND).
// The manual does NOT say how the switch positions map onto the three pins. This
// code assumes the common case: the switch connects exactly one pin to GND per
// position (one-hot, active low). Anything else (no pin low, or several low) is
// reported as "invalid" and never changes the axis. Verify on the unit with the
// PIBOT_CONTROLS_DEBUG build flag, which prints raw levels to the display log.
#pragma once

#include <stdint.h>

namespace pibot {

// Axis assigned to each selector pin, in order {GPIO34, GPIO35, GPIO39}.
// Reorder here if the knob turns the opposite way on the real unit.
constexpr int selector_axis[3] = { 0, 1, 2 };  // X, Y, Z

constexpr int selector_invalid = -1;
constexpr int selector_none    = -2;  // "no change" result of SelectorFilter::feed

// levels: true = pin reads HIGH. Returns axis (0..2) or selector_invalid.
inline int decode_selector(bool level0, bool level1, bool level2) {
    int low_count = 0, low_index = -1;
    const bool levels[3] = { level0, level1, level2 };
    for (int i = 0; i < 3; i++) {
        if (!levels[i]) {
            low_count++;
            low_index = i;
        }
    }
    return (low_count == 1) ? selector_axis[low_index] : selector_invalid;
}

// Debounce: a new value must be seen on `required` consecutive samples before it is
// reported, and only a change of the stable value is reported (as an axis 0..2).
// An invalid reading (knob between detents) never produces an event.
class SelectorFilter {
public:
    explicit SelectorFilter(int required = 3) : _required(required) {}

    // Returns the newly stable axis, or selector_none if nothing changed.
    int feed(int decoded) {
        if (decoded != _candidate) {
            _candidate = decoded;
            _count     = 1;
            return selector_none;
        }
        if (_count < _required) {
            _count++;
        }
        if (_count >= _required && decoded != selector_invalid && decoded != _stable) {
            _stable = decoded;
            return decoded;
        }
        return selector_none;
    }

private:
    int _required;
    int _candidate = selector_invalid;
    int _count     = 0;
    int _stable    = selector_invalid;
};

// ---- Potentiometer ----

constexpr int adc_max   = 4095;
constexpr int pot_low   = 100;   // raw counts treated as 0 %
constexpr int pot_high  = 3995;  // raw counts treated as 100 %

// Raw 12-bit ADC value -> 0..1000 (permille) with dead zones at both ends so the
// full range is reachable despite wiper/ADC end-stop error.
inline int pot_permille(int raw) {
    if (raw <= pot_low) return 0;
    if (raw >= pot_high) return 1000;
    return (int)(((int32_t)(raw - pot_low) * 1000) / (pot_high - pot_low));
}

// Permille -> speed in [min_v, max_v], rounded to a multiple of step (relative to
// min_v), never above max_v. Handles max_v < min_v by returning min_v.
inline int pot_to_speed(int permille, int min_v, int max_v, int step) {
    if (max_v <= min_v || step <= 0) return min_v;
    if (permille < 0) permille = 0;
    if (permille >= 1000) return max_v;  // knob fully clockwise always reaches the maximum
    int32_t span   = max_v - min_v;
    int32_t scaled = (span * permille + 500) / 1000;
    int32_t q      = ((scaled + step / 2) / step) * step;
    int32_t v      = min_v + q;
    return (int)(v > max_v ? max_v : v);
}

// Pot noise/arming filter. The pot is NOT applied until it has moved by more than
// `arm_delta` permille from its first stable reading, so that powering up with the
// knob anywhere does not override the speed the user had set. After that, changes
// smaller than `hysteresis` permille are ignored.
class PotFilter {
public:
    PotFilter(int arm_delta = 40, int hysteresis = 15) : _arm(arm_delta), _hyst(hysteresis) {}

    // Returns true and sets *out when a new permille value should be applied.
    bool feed(int permille, int* out) {
        if (!_have_first) {
            _first      = permille;
            _have_first = true;
            return false;
        }
        if (!_armed) {
            int d = permille - _first;
            if (d < 0) d = -d;
            if (d < _arm) return false;
            _armed = true;
            _last  = permille;
            *out   = permille;
            return true;
        }
        int d = permille - _last;
        if (d < 0) d = -d;
        // Always allow reaching the exact ends.
        bool at_end = (permille == 0 && _last != 0) || (permille == 1000 && _last != 1000);
        if (d >= _hyst || at_end) {
            _last = permille;
            *out  = permille;
            return true;
        }
        return false;
    }

private:
    int  _arm, _hyst;
    int  _first      = 0;
    int  _last       = 0;
    bool _have_first = false;
    bool _armed      = false;
};

}  // namespace pibot
