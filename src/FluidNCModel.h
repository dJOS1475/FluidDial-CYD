// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include "GrblParserC.h"

// Same states as FluidNC except for the last one
enum state_t {
    Idle = 0,   // Must be zero.
    Alarm,      // In alarm state. Locks out all g-code processes. Allows settings access.
    CheckMode,  // G-code check mode. Locks out planner and motion only.
    Homing,     // Performing homing cycle
    Cycle,      // Cycle is running or motions are being executed.
    Hold,       // Active feed hold
    Jog,        // Jogging mode.
    DoorOpen,
    DoorClosed,
    GrblSleep,     // Sleep state.
    ConfigAlarm,   // You can't do anything but fix your config file.
    Critical,      // You can't do anything but reset with CTRL-x or the reset button
    Disconnected,  // We can't talk to FluidNC
};

// Variables and functions to model the state of the FluidNC controller

extern state_t     state;
extern state_t     previous_state;
extern const char* my_state_string;

extern int                n_axes;
extern pos_t              myAxes[6];
extern pos_t              myMachineAxes[6];
extern bool               myLimitSwitches[6];
extern bool               myProbeSwitch;
extern const char*        myCtrlPins;
extern const char*        myFile;
extern file_percent_t     myPercent;
extern override_percent_t myFro;
extern override_percent_t mySro;
extern uint32_t           myFeed;
extern uint32_t           mySpeed;
extern int                lastAlarm;
extern int                lastError;
extern uint32_t           errorExpire;
extern bool               inInches;
extern uint32_t           mySelectedTool;

int num_digits();

// Default ack-wait timeout: 1000 ms.  fnc_send_line() spins at the START of
// every call waiting for the PREVIOUS command's "ok" reply.  Over WiFi/TCP
// the round-trip is ~50 ms on a healthy LAN, so 1 s is still 20x headroom
// for normal operation — but a stalled FluidNC reply path won't pile up
// multi-second waits on each subsequent button press.
void send_line(const char* s, int timeout = 1000);
void send_linef(const char* fmt, ...);
void send_linef_nowait(const char* fmt, ...);  // formatted, no ack-wait spin (UI-safe)

// Cross-core TX-line serialization.  fnc_init_tx_lock() must be called once at
// boot (before the comms/UI tasks start).  txLineLock()/txLineUnlock() bracket
// any code that pushes a complete multi-byte line via fnc_putchar(), so two
// cores can't interleave their command bytes in the WiFi TX ring.
void fnc_init_tx_lock();
bool txLineLock();   // true if acquired (bounded 1 s); only unlock when it did
void txLineUnlock();

// Send a line WITHOUT touching fnc_send_line's _ackwait machinery.
//
// Use this when:
//   • The command goes into FluidNC's motion planner / command queue (jog,
//     realtime overrides) — queue depth is the flow control, not acks.
//   • You're firing several quick commands where ordering matters but
//     ack timing doesn't (eg. settings queries that populate state
//     asynchronously via the parser).
//
// Why it matters: fnc_send_line() spins at the start of each call waiting
// for the previous command's "ok".  Over a WiFi link with ~100 ms round
// trips this turns rapid back-to-back jog commands into a stop-start
// sequence — each $J= waits for the previous to fully ack before the next
// is even sent, so FluidNC's planner never gets to chain moves together.
// Bypassing the ack-wait lets the planner queue multiple jogs back-to-back
// (deceleration ramps bridge them) for smooth continuous motion.
//
// FluidNC still sends "ok" for each command; the parser sees them and
// clears _ackwait normally.  The next regular send_line() will see _ackwait
// either already clear (fine) or set from some prior call (will wait for
// the ack we expect — also fine).
void send_line_nowait(const char* s);

// Counter of nowait sends that haven't been acked yet.  Incremented by
// send_line_nowait(); decremented by show_ok() when the parser observes
// an "ok" reply.  Callers that emit a stream of fire-and-forget commands
// (jog being the canonical example) check this to throttle themselves
// when FluidNC's planner / TCP RX has fallen behind — typically:
//     if (pending_nowait_sends >= 6) return;  // skip this jog event
// 6 is a reasonable threshold matching FluidNC's default planner depth.
extern volatile int pending_nowait_sends;

// Self-healing decay — call periodically (eg. from the comms task loop).
// If no acks or new sends in 1+ seconds, decrements the counter by 1.
// Prevents the throttle from sticking after silent atomic-send failures
// where a queued command was dropped before reaching FluidNC and the
// expected "ok" reply will never arrive.
void nowait_pending_decay();

const char* intToCStr(int val);
const char* axisNumToCStr(int axis);
char        axisNumToChar(int axis);

state_t     decode_state_string(const char* state_string);
const char* decode_error_number(int error_num);
const char* mode_string();

bool fnc_is_connected();
void set_disconnected_state();

void update_rx_time();
bool fnc_rx_ever_seen();   // true once any byte has arrived from FluidNC (any transport)

extern pos_t toMm(pos_t position);
extern pos_t fromMm(pos_t position);
