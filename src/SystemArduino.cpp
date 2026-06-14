// Copyright (c) 2023 Mitch Bradley
// Copyright (c) 2026 — FluidDial-CYD modular comms refactor
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for the Arduino framework.
//
// All transport-specific code now lives in the Comms layer:
//   • Comms.h / Comms.cpp        — facade (picks one backend at boot)
//   • CommsUart.h / CommsUart.cpp — UART backend (ESP-IDF UART driver)
//   • WiFiConnection.h / .cpp     — WiFi backend (WebSocket to FluidNC)
//
// fnc_putchar() and fnc_getchar() are GrblParserC's only byte-level hooks;
// they now forward into the comms facade.  Nothing in this file knows
// whether the active transport is UART or WiFi.

#include "System.h"
#include "FluidNCModel.h"
#include "NVS.h"
#include "Comms.h"

#include <Esp.h>  // ESP.restart()
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ── GrblParserC byte hooks ───────────────────────────────────────────────────

extern "C" void fnc_putchar(uint8_t c) {
    comms_putchar(c);
}

// Byte consumption is owned by Core 0 (pendant_comms_task drains the ring
// buffer into GrblParserC's collect()).  But the GrblParserC library's
// fnc_send_line() spins waiting for the previous command's "ok" ack and
// calls fnc_poll() -> fnc_getchar() -> collect() from whichever task it
// was invoked on.  When called from Core 1 (touch handlers, button
// handlers, etc.), that's a SECOND task calling collect() on the same
// static _report buffer that Core 0 is writing into — a race that
// corrupts the parser and causes file-list / DRO / ack messages to be
// mis-parsed or silently lost.
//
// Solution: when called from Core 1, return -1 immediately (no byte
// available) and yield 1 tick.  fnc_send_line's spin still sees
// _ackwait clear when Core 0's drain processes "ok" — exactly the
// behaviour we want — but Core 1 never touches collect()'s state.
//
// The equivalent path in UART mode is naturally safe because the
// ESP-IDF UART driver serializes uart_read_bytes() internally; only
// the WiFi ring buffer + collect() combination is vulnerable to this.
extern "C" int fnc_getchar() {
    if (xPortGetCoreID() != 0) {
        // Not Core 0 — yield and report no byte.  The caller (almost
        // always fnc_poll() inside fnc_send_line's _ackwait spin) re-loops
        // and re-checks _ackwait, which Core 0's drain will have cleared.
        vTaskDelay(1);
        return -1;
    }
    return comms_getchar();
}

void ledcolor(int n) {
    digitalWrite(4, !(n & 1));
    digitalWrite(16, !(n & 2));
    digitalWrite(17, !(n & 4));
}

extern "C" void poll_extra() {
#ifdef DEBUG_TO_USB
    if (debugPort.available()) {
        char c = debugPort.read();
        if (c == 0x12) {  // CTRL-R
            ESP.restart();
            while (1) {}
        }
        fnc_putchar(c);  // So you can type commands to FluidNC
    }
#endif
}

void drawPngFile(const char* filename, int x, int y) {
    drawPngFile(&canvas, filename, x, y);
}
void drawPngFile(LGFX_Sprite* sprite, const char* filename, int x, int y) {
    // When datum is middle_center, the origin is the center of the canvas and the
    // +Y direction is down.
    std::string fn { "/" };
    fn += filename;
    sprite->drawPngFile(LittleFS, fn.c_str(), x, -y, 0, 0, 0, 0, 1.0f, 1.0f, datum_t::middle_center);
}

#define FORMAT_LITTLEFS_IF_FAILED true

extern void init_hardware();

void init_system() {
    init_hardware();

    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
        dbg_println("LittleFS Mount Failed");
        return;
    }

    // Make an offscreen canvas that can be copied to the screen all at once
    canvas.setColorDepth(8);
    canvas.createSprite(240, 240);  // display.width(), display.height());
}

// ── Flow-control reset ──────────────────────────────────────────────────────
// Send a soft-XON byte through the active transport, then — if and only if
// the active transport is UART — also force the UART hardware out of XOFF
// state.  In WiFi mode the byte is dropped by ws_putchar() (which filters
// XON/XOFF since TCP doesn't need them) and the hardware-XON call is
// skipped entirely.
#include "CommsUart.h"  // uart_backend_reset_flow_control()
void resetFlowControl() {
    comms_putchar(0x11);
    if (comms_active_mode() == COMMS_MODE_UART) {
        uart_backend_reset_flow_control();
    }
}

extern "C" int milliseconds() {
    return millis();
}

void delay_ms(uint32_t ms) {
    delay(ms);
}

void dbg_write(uint8_t c) {
#ifdef DEBUG_TO_USB
    if (debugPort.availableForWrite() > 1) {
        debugPort.write(c);
    }
#endif
}

void dbg_print(const char* s) {
#ifdef DEBUG_TO_USB
    if (debugPort.availableForWrite() > strlen(s)) {
        debugPort.print(s);
    }
#endif
}

nvs_handle_t nvs_init(const char* name) {
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(name, NVS_READWRITE, &handle);
    return err == ESP_OK ? handle : 0;
}
