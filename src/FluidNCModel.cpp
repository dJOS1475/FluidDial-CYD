// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FluidNCModel.h"
#include "ConfigItem.h"
#include "FileParser.h"  // init_file_list()
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <map>
#include "System.h"
#include "Scene.h"
#include "e4math.h"
#include "HomingScene.h"

extern Scene statusScene;

// local copies of status items
const char*        my_state_string    = "N/C";
state_t            state              = Idle;
int                n_axes             = 3;
pos_t              myAxes[6]          = { 0 };
pos_t              myMachineAxes[6]   = { 0 };
bool               myLimitSwitches[6] = { false };
bool               myProbeSwitch      = false;
static char        myFileBuffer[64]   = "";  // running SD filename (safe copy)
const char*        myFile             = myFileBuffer;
const char*        myCtrlPins         = "";
file_percent_t     myPercent          = 0.0;  // percent conplete of SD file
override_percent_t myFro              = 100;  // Feed rate override
override_percent_t mySro              = 100;  // Spindle Override
uint32_t           myFeed             = 0;
uint32_t           mySpeed            = 0;
uint32_t           mySelectedTool     = 0;

std::string myModes = "no data";

int      lastAlarm = 0;
int      lastError = 0;
bool     inInches  = false;
uint32_t errorExpire;

int num_digits() {
    return inInches ? 3 : 2;
}

// clang-format off
// Maps the state strings in status reports to internal state enum values
struct cmp_str {
   bool operator()(char const *a, char const *b) const    {
      return strcmp(a, b) < 0;
   }
};

std::map<const char *, state_t, cmp_str>  state_map = {
    { "Idle", Idle },
    { "Alarm", Alarm },
    { "Hold:0", Hold },
    { "Hold:1", Hold },
    { "Run", Cycle },
    { "Jog", Jog },
    { "Home", Homing },
    { "Door:0", DoorClosed },
    { "Door:1", DoorOpen },
    { "Check", CheckMode },
    { "Sleep", GrblSleep },
};
// clang-format on

bool decode_state_string(const char* state_string, state_t& state) {
    if (strcmp(my_state_string, state_string) != 0) {
        auto found = state_map.find(state_string);
        if (found != state_map.end()) {
            my_state_string = found->first;
            state           = found->second;
            return true;
        }
    }
    return false;
}

void set_disconnected_state() {
    state           = Disconnected;
    my_state_string = "N/C";
    // If the link drops mid file/macro transfer, don't leave the JSON parser
    // half-fed: clear the in-flight flags so the parse state can't stick at
    // x1/a1.  (The next request sets parser_needs_reset itself.)
    g_expecting_json    = false;
    g_json_accumulating = false;
}

// clang-format off
std::map<int, const char*> error_map = {  // Do here so abreviations are right for the dial
    { 0, "None"},
    { 1, "GCode letter"},
    { 2, "GCode format"},
    { 3, "Bad $ command"},
    { 4, "Negative value"},
    { 5, "Setting Diabled"},
    { 10, "Soft limit error"},
    { 13, "Check door"},
    { 18, "No Homing Cycles"},
    { 20, "Unsupported GCode"},
    { 22, "Undefined feedrate"},
    { 19, "No single axis"},
    { 34, "Arc radius error"},
    { 39, "P Param Exceeded"},
};
// clang-format on

const char* decode_error_number(int error_num) {
    if (error_map.find(error_num) != error_map.end()) {
        return error_map[error_num];
    }
    static char retval[33];
    sprintf(retval, "%d", error_num);
    return retval;
}

extern "C" void begin_status_report() {
    myPercent = 0;
    myFileBuffer[0] = '\0';  // clear filename each status cycle; show_file repopulates if running
}

extern "C" void show_file(const char* filename, file_percent_t percent) {
    myPercent = percent;
    if (filename && *filename) {
        strncpy(myFileBuffer, filename, sizeof(myFileBuffer) - 1);
        myFileBuffer[sizeof(myFileBuffer) - 1] = '\0';
    } else {
        myFileBuffer[0] = '\0';
    }
}

extern "C" void show_overrides(override_percent_t feed_ovr, override_percent_t rapid_ovr, override_percent_t spindle_ovr) {
    myFro = feed_ovr;
    mySro = spindle_ovr;
}

extern "C" void show_feed_spindle(uint32_t feedrate, uint32_t spindle_speed) {
    myFeed  = feedrate;
    mySpeed = spindle_speed;
};

extern "C" void show_limits(bool probe, const bool* limits, size_t n_axis) {
    myProbeSwitch = probe;
    memcpy(myLimitSwitches, limits, n_axis * sizeof(*limits));
}

extern "C" void show_control_pins(const char* pins) {
    //dbg_printf("show_control_pins:%s\r\n", pins);
    myCtrlPins = pins;
}

// ── Deflection-calibration probe capture ──────────────────────────────────────
// The UI arms g_calCapture before running the calibration program; every [PRB:]
// report then lands here (machine coords, e4 fixed-point) and we stash the X of
// each probe in order.  The UI reads the last two as x1/x2.  (See screen_probe_cfg.)
volatile bool    g_calCapture      = false;
volatile int     g_calCount        = 0;
volatile bool    g_calAllOk        = true;
volatile int32_t g_calProbeXe4[8]  = { 0 };

extern "C" void show_probe(const pos_t* axes, const bool probe_success, size_t n_axis) {
    if (!g_calCapture) return;
    if (!probe_success) g_calAllOk = false;
    if (g_calCount < 8 && n_axis > 0) {
        g_calProbeXe4[g_calCount] = (int32_t)axes[0];   // machine X, e4 (report units)
        g_calCount++;
    }
}

#ifdef E4_POS_T
extern "C" void show_dro(const pos_t* axes, const pos_t* wco, bool isMpos, bool* limits, size_t n_axis) {
    n_axes = (int)n_axis;
    for (int axis = 0; axis < n_axis; axis++) {
        // Work pos (DRO)
        e4_t axis_val = axes[axis];
        if (isMpos) {
            axis_val -= wco[axis];
        }
        myAxes[axis] = inInches ? e4_mm_to_inch(axis_val) : axis_val;
        // Machine pos (absolute)
        e4_t mach_val = isMpos ? axes[axis] : (axes[axis] + wco[axis]);
        myMachineAxes[axis] = inInches ? e4_mm_to_inch(mach_val) : mach_val;
    }
}
#else
pos_t fromMm(pos_t position) {
    return inInches ? position / 25.4 : position;
}
pos_t toMm(pos_t position) {
    return inInches ? position * 25.4 : position;
}

extern "C" void show_dro(const pos_t* axes, const pos_t* wco, bool isMpos, bool* limits, size_t n_axis) {
    for (int axis = 0; axis < n_axis; axis++) {
        // Work pos (DRO)
        myAxes[axis] = fromMm(axes[axis]);
        if (isMpos) {
            myAxes[axis] -= fromMm(wco[axis]);
        }
        // Machine pos (absolute)
        myMachineAxes[axis] = isMpos ? fromMm(axes[axis])
                                     : fromMm(axes[axis]) + fromMm(wco[axis]);
    }
}
#endif

// Jog flow control: track in-flight nowait sends so we can throttle when
// FluidNC's motion planner / input ring is saturated.  Defined here so
// send_line_nowait() (below) can reference them; show_ok() further down
// in this file does the actual decrement on each "ok" reply.
volatile int pending_nowait_sends    = 0;
static unsigned long _last_nowait_activity = 0;   // ms — last ack or send time

// ── TX line serialization (cross-core) ───────────────────────────────────────
// A "line" command (e.g. "$Files/ListGCode=/sd\n", "$J=...\n", "$30\n") is sent
// to FluidNC one byte at a time via fnc_putchar().  In WiFi mode those bytes go
// into a shared TX ring.  Line commands originate on BOTH cores — Core 1 issues
// file-list / macro / jog / config-query commands, while Core 0 issues the
// show_state() $G/$I/$A burst and the red-button $X.  If two multi-byte lines
// are pushed concurrently their bytes INTERLEAVE in the ring, producing a
// garbled command.  A garble of "$Files/ListGCode=..." in particular can read
// as G-code motion and trip an Alarm on an unhomed machine — exactly the
// intermittent "SD/macros fail + controller alarms" symptom.
//
// This recursive mutex makes each whole-line push atomic with respect to other
// line pushes.  Single realtime bytes (fnc_realtime: '?', 0xB2, overrides) do
// NOT take it — they're one atomic ring push and tx_drain pulls them out of any
// line cleanly, so they never corrupt a line.  Recursive because the ack-wait
// inside fnc_send_line() pumps the transport, which can re-enter send_line()
// from a parser callback on the same (Core 0) task.
static SemaphoreHandle_t _txLineMutex = nullptr;
void fnc_init_tx_lock() {
    if (!_txLineMutex) _txLineMutex = xSemaphoreCreateRecursiveMutex();
}
// Bounded take (1 s) instead of portMAX_DELAY: the lock is only ever held for a
// few byte-ring pushes, so 1 s is effectively "forever" for the legitimate case
// while guaranteeing a stuck holder can never permanently freeze the caller —
// worst case a single line goes out un-serialised.  Returns true if acquired;
// callers must only txLineUnlock() when it did (a recursive mutex must see
// exactly one give per successful take).
bool txLineLock() {
    if (!_txLineMutex) return false;
    return xSemaphoreTakeRecursive(_txLineMutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}
void txLineUnlock() {
    if (_txLineMutex) xSemaphoreGiveRecursive(_txLineMutex);
}

void send_line(const char* s, int timeout) {
    bool locked = txLineLock();
    fnc_send_line(s, timeout);
    if (locked) txLineUnlock();
    dbg_println(s);
}

// See FluidNCModel.h for the full rationale.  Sends the bytes directly via
// the comms transport without touching fnc_send_line's _ackwait state so
// callers can fire commands back-to-back without serializing on round-trip
// latency.  Suitable for queued commands (jog, realtime overrides) where
// FluidNC's planner / parser handles ordering and acks come asynchronously.
//
// Increments pending_nowait_sends so callers can implement their own
// flow control (eg. jog handler skips events when the counter is high).
void send_line_nowait(const char* s) {
    bool locked = txLineLock();   // push the whole line atomically (no interleave)
    const char* p = s;
    while (*p) {
        fnc_putchar((uint8_t)*p);
        ++p;
    }
    fnc_putchar('\n');
    if (locked) txLineUnlock();
    pending_nowait_sends++;
    _last_nowait_activity = milliseconds();   // freshen the decay watchdog
    dbg_println(s);
}

static void vsend_linef(const char* fmt, va_list va) {
    static char buf[128];
    vsnprintf(buf, 128, fmt, va);
    send_line(buf);
}
void send_linef(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsend_linef(fmt, args);
    va_end(args);
}

// Non-blocking formatted send.  Same as send_linef() but routes through
// send_line_nowait(), which does NOT spin on fnc_send_line's ack-wait.
//
// Used for file-list / macro / preview requests.  Those are issued from
// scheduled actions that run on Core 1 (the UI loop).  fnc_send_line() begins
// by spinning until the PREVIOUS command's "ok" arrives — and on Core 1
// fnc_getchar() is gated to return nothing, so that spin can only end when
// Core 0 happens to clear _ackwait or the (1 s) timeout expires.  If an "ok"
// was ever lost, every subsequent request blocked the entire UI loop for a
// full second each — the "UI becomes unresponsive when opening SD/macros"
// symptom.  These requests don't need synchronous ack handshaking: the reply
// is parsed asynchronously on Core 0 and onFilesList()/onError() fire when it
// completes.  Sending them nowait removes the UI stall entirely.
void send_linef_nowait(const char* fmt, ...) {
    static char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    send_line_nowait(buf);
}

char axisNumToChar(int axis) {
    return "XYZABC"[axis];
}

const char* axisNumToCStr(int axis) {
    static char ret[2] = { '\0', '\0' };
    ret[0]             = axisNumToChar(axis);
    return ret;
}

const char* intToCStr(int val) {
    static char buffer[20];
    sprintf(buffer, "%d", val);
    return buffer;
}

const char* mode_string() {
    return myModes.c_str();
}

state_t previous_state;
bool    awaiting_alarm = false;

extern "C" void show_state(const char* state_string) {
    previous_state = state;
    state_t new_state;
    if (decode_state_string(state_string, new_state) && state != new_state) {
        if (state == Disconnected) {
            // This runs on Core 0 inside the parser callback, right as the link
            // (re)establishes.  Use the NON-BLOCKING sends: the blocking
            // send_line() spins waiting for each ack (up to 1 s apiece), and on
            // a WiFi reconnect — especially after an ungraceful reboot, while
            // the WS is still re-handshaking and acks are delayed — that stalls
            // Core 0's RX/WS servicing for several seconds.  Status then can't
            // flow (UI stuck on "Connecting") and the stall can trip the task
            // watchdog into a reboot loop.  These queries don't need a
            // synchronous ack; their replies are parsed asynchronously.
            fnc_realtime((realtime_cmd_t)0x0c);  // Ctrl-L - echo off
            send_line_nowait("$G");              // Refresh GCode modes
            send_line_nowait("$G");              // Refresh GCode modes
            send_line_nowait("$RI=200");
            send_line_nowait("$I");              // Firmware version → show_versions()
            init_file_list();
            detect_homing_info();
        }
        state = new_state;
        if (state == Alarm && lastAlarm == 0) {  // alarm code not yet known
            send_line("$A");                     // fetch the alarm code (async)
            awaiting_alarm = true;
            // Do NOT return here.  Fall through to act_on_state_change() so the
            // pendant reflects the Alarm state IMMEDIATELY.  Previously we
            // waited for the $A reply before updating the display — but over a
            // network transport that reply can be delayed or arrive in a
            // different wire format, which left the pendant showing a stale
            // "Idle" while the controller was actually back in Alarm (e.g.
            // after the red-button soft reset).  The alarm code/description
            // refines when the $A reply lands; it calls act_on_state_change()
            // again from handle_other().
        }
        act_on_state_change();
    }
}

extern "C" void handle_other(char* line) {
    // Multi-line JSON responses from $File/SendJSON: only the first line is wrapped in
    // [JSON:...]; subsequent content lines arrive here as bare text.  Route them back
    // into the streaming JSON parser while accumulation is active.
    if (json_continuation_line(line)) return;

    // Network transports (Telnet, WebSocket) emit $Files/ListGCode and
    // $File/SendJSON replies RAW — FluidNC's UartChannel wraps them in
    // "[JSON:...]" (→ handle_json) but the socket channels use the base
    // Channel::out(), which sends the JSON unwrapped, so it lands here instead.
    // While a file/macro request is in flight, feed these raw chunks into the
    // exact same streaming parser handle_json() drives.  This is THE reason
    // SD/macros listings only ever worked over UART: it was a wire-format
    // difference between FluidNC's channel types, not a transport-reliability
    // problem.  handle_json() resets the parser on the first chunk
    // (parser_needs_reset), feeds it, and emits the 0xB2 flow-control ack.
    if (g_expecting_json) {
        handle_json(line);
        return;
    }

    if (*line == '$') {
        parse_dollar(line);
        return;
    }
    int alarmlen = strlen("Active alarm: ");
    if (strncmp(line, "Active alarm: ", alarmlen) == 0) {
        lastAlarm = atoi(line + alarmlen);
        if (awaiting_alarm) {
            dbg_printf("Got alarm %d\n", lastAlarm);
            awaiting_alarm = false;
            act_on_state_change();
        }
    }
}

extern "C" void show_error(int error) {
    errorExpire = milliseconds() + 1000;
    lastError   = error;
    current_scene->reDisplay();

    // If a file/macro request got an "error:" reply instead of JSON (eg.
    // $File/SendJSON returning IdleError when the machine isn't Idle/Alarm),
    // there will be no JSON document and thus no endDocument to clear
    // g_expecting_json.  Left latched, it would route every subsequent
    // handle_other() line into the JSON parser and corrupt later parses
    // (cascading SD/macros failures).  Clear it here.
    g_expecting_json = false;

    // A jog/line command sent via send_line_nowait() consumes exactly one
    // FluidNC response — which is normally "ok" (handled in show_ok) but can
    // be "error:" instead (eg. error:15 "Travel exceeded" when a flood of
    // 1 mm jogs walks the axis into a soft limit).  If we only decremented on
    // "ok", every error would permanently inflate pending_nowait_sends, and
    // once it reached the jog throttle threshold the encoder would stop
    // commanding motion until the 1-per-second decay slowly drained it.
    // Treat an error as closing out an in-flight nowait send, same as an ok.
    _last_nowait_activity = milliseconds();
    if (pending_nowait_sends > 0) {
        --pending_nowait_sends;
    }
}

extern "C" void show_timeout() {
    dbg_println("Timeout");
}

extern "C" void show_ok() {
    _last_nowait_activity = milliseconds();
    if (pending_nowait_sends > 0) {
        --pending_nowait_sends;
    }
}

// Self-healing decay for pending_nowait_sends.  Call periodically from a
// low-frequency loop (eg. comms task at 2 ms cadence).  Mechanism:
//
//   If we've gone >1 s without either a new send_line_nowait() call OR an
//   incoming "ok" reply, the counter is presumed to be stale (the kernel
//   send buffer probably refused some bytes earlier, so no ack will ever
//   come back) and we decrement it by 1.  Continues decrementing once per
//   second until the counter reaches 0.
//
// Without this, a brief network hiccup that causes a couple of atomic-send
// failures would permanently leave pending_nowait_sends elevated, and the
// jog throttle (>=6 → drop event) would silently block all subsequent jog
// commands forever.  Symptom: DRO still updates (read path works) but
// turning the encoder doesn't move the machine.
void nowait_pending_decay() {
    if (pending_nowait_sends <= 0) {
        _last_nowait_activity = milliseconds();
        return;
    }
    if ((milliseconds() - _last_nowait_activity) >= 1000) {
        --pending_nowait_sends;
        _last_nowait_activity = milliseconds();
    }
}

extern "C" void end_status_report() {
    current_scene->onDROChange();
}

extern "C" void show_alarm(int alarm) {
    lastAlarm = alarm;
    current_scene->reDisplay();
}

extern "C" void show_gcode_modes(struct gcode_modes* modes) {
    inInches = strcmp(modes->units, "In") == 0 || strcmp(modes->units, "G20") == 0;

    myModes = modes->wcs;
    myModes += " ";
    myModes += modes->units;
    myModes += " ";
    myModes += modes->distance;
    myModes += " ";
    myModes += modes->spindle;
    if (strcmp(modes->mist, "On") == 0) {
        myModes += " Mist";
    }
    if (strcmp(modes->flood, "On") == 0) {
        myModes += " Flood";
    }

    mySelectedTool = modes->tool;
    current_scene->reDisplay();
}

int disconnect_ms = 0;
int next_ping_ms  = 0;

// If we haven't heard from FluidNC in 4 seconds for some other reason,
// send a status report request.
const int ping_interval_ms = 4000;

// If we haven't heard from FluidNC in 6 seconds for any reason, declare
// FluidNC unresponsive.  After a ping, FluidNC has 2 seconds to respond.
const int disconnect_interval_ms = 6000;

bool starting = true;

void request_status_report() {
    fnc_putchar(0x11);           // XON; request software flow control
    fnc_realtime(StatusReport);  // Request fresh status
    next_ping_ms = milliseconds() + ping_interval_ms;
}

bool fnc_is_connected() {
    int now = milliseconds();
    if (starting) {
        starting      = false;
        disconnect_ms = now + (disconnect_interval_ms - ping_interval_ms);
        request_status_report();  // sets next_ping_ms
        return false;             // Do we need a value for "unknown"?
    }
    if ((now - disconnect_ms) >= 0) {
        next_ping_ms  = now + ping_interval_ms;
        disconnect_ms = now + disconnect_interval_ms;
        return false;
    }

    if ((now - next_ping_ms) >= 0) {
        request_status_report();
    }
    return true;
}

// Set true the first time ANY real byte arrives from FluidNC, on any
// transport.  update_rx_time() is the single per-RX hook called by the UART
// backend (per byte) and by the WebSocket backend (per frame), so this is the
// transport-agnostic place to latch "we have heard from a real controller."
//
// pendant_comms_task uses this to gate the pendantConnected transition: at
// boot fnc_is_connected() briefly reports "connected" before any controller
// has spoken (it's timing-based), which would otherwise flash a false
// "Connected" in demo mode.  Previously the comms task latched its own local
// flag inside the fnc_getchar() drain loop — but the WebSocket path now feeds
// collect() directly from onWsEvent and never goes through fnc_getchar(), so
// that local flag never set and the connection "never completed" over WiFi
// even though data was flowing.  Latching here fixes both transports.
static volatile bool _rx_ever_seen = false;
bool fnc_rx_ever_seen() {
    return _rx_ever_seen;
}

void update_rx_time() {
    int now       = milliseconds();
    next_ping_ms  = now + ping_interval_ms;
    disconnect_ms = now + disconnect_interval_ms;
    _rx_ever_seen = true;
}
