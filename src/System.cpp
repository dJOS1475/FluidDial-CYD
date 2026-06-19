// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "FluidNCModel.h"

#if 0
// Helpful for debugging touch development.
const char* M5TouchStateName(m5::touch_state_t state_num) {
    static constexpr const char* state_name[16] = { "none", "touch", "touch_end", "touch_begin", "___", "hold", "hold_end", "hold_begin",
                                                    "___",  "flick", "flick_end", "flick_begin", "___", "drag", "drag_end", "drag_begin" };

    return state_name[state_num];
}
#endif

void dbg_printf(const char* format, ...) {
    // Route through dbg_print() — NOT vprintf().  vprintf writes to stdout, which
    // on the ESP32 is UART0; on a wired pendant the FluidNC controller link is
    // ALSO on UART0, so every dbg_printf() was injecting debug text straight into
    // the control stream (bytes the controller reads as feed-override realtime
    // commands → feed rate dropping mid-job).  dbg_print() honours DEBUG_TO_USB:
    // it is a no-op in the shipped "nodebug" build, and in a debug build the
    // controller is moved to UART1 so the USB/UART0 console is safe.
    char buf[192];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (n > 0) {
        dbg_print(buf);
    }
}

void dbg_print(const std::string& s) {
    dbg_print(s.c_str());
}

void dbg_println(const std::string& s) {
    dbg_println(s.c_str());
}

void dbg_println(const char* s) {
    dbg_print(s);
    dbg_print("\r\n");
}
