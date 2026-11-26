// PiBot Pendant V4/V4.2 hardware profile. Pin numbers are pendant-side,
// not the FluidNC controller's GPIOs. See docs/PIBOT.md for sources and status.
#pragma once

#ifdef PIBOT_PENDANT
#    if !defined(CAPACITIVE_CYD) || defined(RESISTIVE_CYD)
#        error "PiBot requires its dedicated capacitive build, not CYD touch auto-detection"
#    endif
#    if defined(CYD_BATTERY_ADC)
#        error "PiBot GPIO39 is a selector input, not the CYD battery ADC"
#    endif
#    if defined(DEBUG_TO_USB)
#        error "PiBot UART0 shares USB serial; DEBUG_TO_USB is not supported by this target"
#    endif
#    if defined(LED_DEBUG)
#        error "PiBot GPIO4/16/17 are buttons, not debug LED outputs"
#    endif
#endif

// Display rotation that is "right way up" for the hardware: the PiBot V4.2 panel is
// mounted 180 degrees from the generic CYD (0 vs 2).  Bench-confirmed: the generic
// default of 2 showed the screen upside down on the PiBot pendant.
#ifdef PIBOT_PENDANT
#    define PENDANT_NORMAL_ROTATION 0
#else
#    define PENDANT_NORMAL_ROTATION 2
#endif

namespace pibot {
constexpr int backlight = 21;
constexpr int touch_sda = 32;
constexpr int touch_scl = 25;
constexpr int encoder_a = 22;
constexpr int encoder_b = 27;
constexpr int button_cancel = 4;
constexpr int button_pause = 16;
constexpr int button_start = 17;
constexpr int uart_rx = 1;
constexpr int uart_tx = 3;

// Reserved only: no automatic machine commands are attached to these controls.
constexpr int potentiometer = 26;
constexpr int selector_0 = 34;
constexpr int selector_1 = 35;
constexpr int selector_2 = 39;

static_assert(backlight != encoder_a && backlight != encoder_b,
              "Backlight PWM must never drive an encoder input");
static_assert(uart_tx != backlight && uart_tx != selector_1,
              "UART must not take over the backlight or selector");
}  // namespace pibot
