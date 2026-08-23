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
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Private to this translation unit — no other file should touch the port.
static uart_port_t fnc_uart_port;

// Event queue for the UART driver — see uart_backend_poll().
#define UART_EVT_QUEUE_LEN 8
static QueueHandle_t   _uart_events  = nullptr;
static volatile uint32_t _uart_rx_lost = 0;   // overflow events since boot

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
        // Do NOT spin here.  This used to be `while (1) {}`, which starves the
        // idle task and trips the 15 s task watchdog (panic = true) into a
        // reboot — and since dbg_println() compiles out of the shipped build,
        // it was a silent boot loop with no way to tell it from a crash.
        // Returning leaves the pendant up: the UI still runs and the screen can
        // say the link is dead, which is strictly more useful than a reset.
        dbg_println("UART config FAILED — serial link unavailable");
        return;
    }
    // 4 KB RX buffer — large enough to absorb a full JSON burst without
    // triggering XON/XOFF flow control mid-stream.  Old 256-byte buffer +
    // 500 B/s poll rate caused preferences.json (10+ KB) to take 15-20
    // seconds to receive.  The drain loop empties this every 2 ms.
    //
    // An 8-deep event queue is installed so RX overflow is VISIBLE.  With
    // queue_size 0 the driver silently discarded bytes on overrun: at 1 Mbaud
    // this buffer is only ~41 ms of data, so a stall drops data with nothing
    // anywhere reporting it — the failure mode is a garbled reply, not an error.
    esp_err_t e = uart_driver_install(fnc_uart_port, 4096, 0,
                                      UART_EVT_QUEUE_LEN, &_uart_events,
                                      ESP_INTR_FLAG_IRAM);
    if (e != ESP_OK) {
        dbg_printf("UART driver install failed: %s\n", esp_err_to_name(e));
        _uart_events = nullptr;
    }

    // XON/XOFF thresholds MUST be < SOC_UART_FIFO_LEN (128) — ESP-IDF rejects
    // anything else and returns ESP_FAIL *before* touching the hardware.  This
    // used to pass (128, 250), reasoning only about the uint8_t range, so BOTH
    // checks failed and software flow control was never enabled at all: the link
    // ran at 1 Mbaud with no backstop, and the ignored return value meant nothing
    // said so.  120/64 keeps XOFF late (rarely fires) while staying legal.
    e = uart_set_sw_flow_ctrl(fnc_uart_port, true, 64, 120);
    if (e != ESP_OK) {
        dbg_printf("UART sw flow control NOT enabled: %s\n", esp_err_to_name(e));
    }
}

// Drain the UART event queue, counting anything that means we lost bytes.
// Called from the comms poll hook so it costs nothing when the queue is empty.
void uart_backend_poll() {
    if (!_uart_events) return;
    uart_event_t ev;
    while (xQueueReceive(_uart_events, &ev, 0) == pdTRUE) {
        switch (ev.type) {
            case UART_FIFO_OVF:      // hardware FIFO overran before the DMA read it
            case UART_BUFFER_FULL:   // 4 KB ring filled before the drain loop ran
                if (++_uart_rx_lost == 1) {
                    dbg_println("UART RX OVERFLOW — bytes lost (replies will garble)");
                }
                uart_flush_input(fnc_uart_port);
                xQueueReset(_uart_events);
                break;
            default:
                break;
        }
    }
}

uint32_t uart_backend_rx_lost() { return _uart_rx_lost; }
