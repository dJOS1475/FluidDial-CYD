// Copyright (c) 2023 Mitch Bradley
// Copyright (c) 2026 — FluidDial-CYD modular refactor
// Use of this source code is governed by a GPLv3 license.

// ── UART backend — pure UART hardware, no WiFi awareness ─────────────────────
//
// Moved out of SystemArduino.cpp so the comms layer can pick exactly one
// backend at boot.  All ESP-IDF UART knowledge lives here.

#include "CommsUart.h"
#include "System.h"               // dbg_print*, FNC_BAUD, ECHO_FNC_TO_DEBUG
#include "FluidNCModel.h"         // update_rx_time()

#include <driver/uart.h>
#include "hal/uart_hal.h"

// Private to this translation unit — no other file should touch the port.
static uart_port_t fnc_uart_port;

// ── Hot-path I/O ────────────────────────────────────────────────────────────

void uart_backend_putchar(uint8_t c) {
    uart_write_bytes(fnc_uart_port, (const char*)&c, 1);
#ifdef ECHO_FNC_TO_DEBUG
    dbg_write(c);
#endif
}

int uart_backend_getchar() {
    char c;
    int  res = uart_read_bytes(fnc_uart_port, &c, 1, 0);
    if (res == 1) {
        update_rx_time();
#ifdef ECHO_FNC_TO_DEBUG
        dbg_write(c);
#endif
        return c;
    }
    return -1;
}

void uart_backend_reset_flow_control() {
    uart_ll_force_xon(fnc_uart_port);
}

// ── Driver install ──────────────────────────────────────────────────────────
//
// We use the ESP-IDF UART driver instead of the Arduino HardwareSerial driver
// so we can use software (XON/XOFF) flow control.  The ESP-IDF driver
// supports the ESP32's hardware implementation of XON/XOFF, but Arduino does
// not.

#ifndef FNC_BAUD
#    define FNC_BAUD 115200
#endif

void init_fnc_uart(int uart_num, int tx_pin, int rx_pin) {
    fnc_uart_port = (uart_port_t)uart_num;
    int baudrate  = FNC_BAUD;
    uart_driver_delete(fnc_uart_port);
    uart_set_pin(fnc_uart_port, (gpio_num_t)tx_pin, (gpio_num_t)rx_pin, -1, -1);
    uart_config_t conf;
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2)
    conf.source_clk = UART_SCLK_APB;  // ESP32, ESP32S2
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    // UART_SCLK_XTAL is independent of the APB frequency
    conf.source_clk = UART_SCLK_XTAL;  // ESP32C3, ESP32S3
#endif
    conf.baud_rate = baudrate;

    conf.data_bits           = UART_DATA_8_BITS;
    conf.parity              = UART_PARITY_DISABLE;
    conf.stop_bits           = UART_STOP_BITS_1;
    conf.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    conf.rx_flow_ctrl_thresh = 0;
    if (uart_param_config(fnc_uart_port, &conf) != ESP_OK) {
        dbg_println("UART config failed");
        while (1) {}
        return;
    };
    // 4 KB RX buffer — large enough to absorb a full JSON burst without
    // triggering XON/XOFF flow control mid-stream.  Old 256-byte buffer +
    // 500 B/s poll rate caused preferences.json (10+ KB) to take 15-20
    // seconds to receive.  XON/XOFF thresholds are uint8_t (max 255); use
    // near-max values so XOFF fires rarely — the drain loop in
    // pendant_hw_task empties the buffer every 2 ms.
    uart_driver_install(fnc_uart_port, 4096, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
    uart_set_sw_flow_ctrl(fnc_uart_port, true, 128, 250);
    uint32_t baud;
    uart_get_baudrate(fnc_uart_port, &baud);
}
