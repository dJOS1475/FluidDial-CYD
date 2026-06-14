// Copyright (c) 2026 — FluidDial-CYD
// Use of this source code is governed by a GPLv3 license.

#pragma once
#include <stdint.h>

// ── UART backend ─────────────────────────────────────────────────────────────
//
// Pure UART implementation — knows nothing about WiFi, NVS, or the comms
// facade.  Owns the ESP-IDF UART driver, the static fnc_uart_port handle,
// and the XON/XOFF software flow-control wiring.
//
// init_fnc_uart() is called once during hardware setup (Hardware2432.cpp,
// HardwareM5Dial.cpp).  After that the backend is ready and the three
// hot-path functions are safe to call from the task that drives them.

void init_fnc_uart(int uart_num, int tx_pin, int rx_pin);

void uart_backend_putchar(uint8_t c);
int  uart_backend_getchar();                 // returns -1 if no byte available
void uart_backend_reset_flow_control();      // force HW XON (recovers from XOFF)
