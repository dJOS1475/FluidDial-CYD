// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for the Arduino framework

#include "System.h"
#include "CommsUart.h"   // init_fnc_uart()

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <driver/i2c.h>

#include "Hardware2432.hpp"
#include "Drawing.h"
#include "NVS.h"

#include <driver/uart.h>
#include "hal/uart_hal.h"
#include <driver/rtc_io.h>

// This pin is connected to a photoresistor that is ostensibly used
// for ambient light sensing.  It is possible to repurpose it as a UI
// lock by connecting a normally-open switch between the photoresistor
// and 3V3. Some people use the pushbutton switch on the side of the
// CYD that is supposed to control battery charging, cutting the
// traces that connect it to the battery circuit and running a wire
// over to the photoresistor.
int lockout_pin = GPIO_NUM_34;

m5::Touch_Class  xtouch;
m5::Touch_Class& touch = xtouch;

static uint32_t read_panel_reg(lgfx::IBus* bus, int32_t pin_cs, uint_fast16_t cmd, uint8_t dummy_bits, uint8_t real_reads) {
    size_t        dlen     = 8;
    uint_fast16_t read_cmd = cmd;

    lgfx::pinMode(pin_cs, lgfx::pin_mode_t::output);

    bus->beginTransaction();

    // Dummy clocks with CS- high in hopes of clearing out any junk
    // It is unclear whether this is necessary, but it probably doesn't hurt
    lgfx::gpio_hi(pin_cs);
    bus->writeCommand(0, dlen);
    bus->wait();

    // printf("cmd:%02x", read_cmd);
    lgfx::gpio_lo(pin_cs);
    bus->writeCommand(read_cmd, dlen);
    bus->beginRead(dummy_bits);
    uint32_t res = 0;
    for (size_t i = 0; i < real_reads; ++i) {
        auto data = bus->readData(dlen);
        // printf(" %02x", data);
        res += ((data >> (dlen - 8)) & 0xFF) << (i * 8);
    }
    bus->endTransaction();
    lgfx::gpio_hi(pin_cs);

    // printf(" res %08x\n", (unsigned int)res);
    return res;
}

// xdisplay is an instance of the concrete wrapper class "LGFX" with knowledge
// of various details of the panel and touch
LGFX_Device  xdisplay;
LGFX_Device& display = xdisplay;

LGFX_Sprite canvas(&display);
LGFX_Sprite buttons[3] = { &display, &display, &display };
LGFX_Sprite locked_button(&display);

uint8_t base_rotation = 2;

lgfx::Bus_SPI bus;
static void   init_bus() {
      auto cfg       = bus.config();
      cfg.freq_write = 55000000;
      cfg.freq_read  = 16000000;
      cfg.use_lock   = true;

      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.spi_host    = HSPI_HOST;
      cfg.pin_mosi    = GPIO_NUM_13;
      cfg.pin_miso    = GPIO_NUM_12;
      cfg.pin_sclk    = GPIO_NUM_14;
      cfg.pin_dc      = GPIO_NUM_2;
      cfg.spi_mode    = 0;
      cfg.spi_3wire   = false;

      bus.config(cfg);
      bus.init();
}

lgfx::Light_PWM light;

void init_light() {
    auto cfg        = light.config();
    cfg.pin_bl      = GPIO_NUM_21;
    cfg.freq        = 12000;
    cfg.pwm_channel = 7;
    cfg.offset      = 0;
    cfg.invert      = false;
    light.config(cfg);
}

void setBacklightPin(uint8_t pinnum) {
    auto cfg   = light.config();
    cfg.pin_bl = pinnum;
    light.config(cfg);
    light.init(255);
}

lgfx::Panel_ST7789  _panel_st7789;
lgfx::Panel_ILI9341 _panel_ili9341;

static void init_panel_st7789() {
    base_rotation = 0;

    auto& p = _panel_st7789;
    p.bus(&bus);

    auto cfg            = p.config();
    cfg.pin_cs          = GPIO_NUM_15;
    cfg.offset_rotation = base_rotation;
    cfg.bus_shared      = false;
    p.config(cfg);

    p.light(&light);

    display.setPanel(&p);
}

static void init_panel_ili9341() {
    base_rotation = 2;

    auto& p = _panel_ili9341;
    p.bus(&bus);

    auto cfg            = p.config();
    cfg.pin_cs          = GPIO_NUM_15;
    cfg.offset_rotation = base_rotation;
    cfg.bus_shared      = false;
    p.config(cfg);

    p.light(&light);

    display.setPanel(&p);
}

#ifdef DEBUG_TO_USB
Stream& debugPort = Serial;
#endif

int red_button_pin   = -1;
int dial_button_pin  = -1;
int green_button_pin = -1;

int enc_a, enc_b;

#ifdef CAPACITIVE_CYD
lgfx::Touch_CST816S _touch_cst816s;

void init_capacitive_cyd() {
    {
        auto cfg            = _touch_cst816s.config();
        cfg.i2c_port        = I2C_NUM_0;
        cfg.pin_sda         = GPIO_NUM_33;
        cfg.pin_scl         = GPIO_NUM_32;
        cfg.pin_rst         = GPIO_NUM_25;
        cfg.pin_int         = -1;
        cfg.offset_rotation = base_rotation;
        cfg.freq            = 400000;
        cfg.x_max           = 240;
        cfg.y_max           = 320;
        _touch_cst816s.config(cfg);
        display.getPanel()->setTouch(&_touch_cst816s);
        display.getPanel()->initTouch();
    }
    setBacklightPin(GPIO_NUM_27);

    pinMode(lockout_pin, INPUT);

#    ifdef CYD_BUTTONS
    enc_a = GPIO_NUM_22;
    enc_b = GPIO_NUM_21;
    // rotary_button_pin = GPIO_NUM_35;
    // pinMode(rotary_button_pin, INPUT);  // Pullup does not work on GPIO35

    red_button_pin   = GPIO_NUM_4;   // RGB LED Red
    dial_button_pin  = GPIO_NUM_17;  // RGB LED Blue
    green_button_pin = GPIO_NUM_16;  // RGB LED Green
    pinMode(red_button_pin, INPUT_PULLUP);
    pinMode(dial_button_pin, INPUT_PULLUP);
    pinMode(green_button_pin, INPUT_PULLUP);
#    else
    enc_a          = GPIO_NUM_22;
    enc_b          = GPIO_NUM_17;  // RGB LED Blue
#    endif
}
#endif  // CAPACITIVE_CYD

#ifdef RESISTIVE_CYD
lgfx::Touch_XPT2046 _touch_xpt2046;

void init_resistive_cyd() {
    {
        auto cfg            = _touch_xpt2046.config();
        cfg.x_min           = 300;
        cfg.x_max           = 3900;
        cfg.y_min           = 3700;
        cfg.y_max           = 200;
        cfg.pin_int         = -1;
        cfg.bus_shared      = false;
        cfg.spi_host        = -1;  // -1:use software SPI for XPT2046
        cfg.pin_sclk        = GPIO_NUM_25;
        cfg.pin_mosi        = GPIO_NUM_32;
        cfg.pin_miso        = GPIO_NUM_39;
        cfg.pin_cs          = GPIO_NUM_33;
        cfg.offset_rotation = base_rotation ^ 2;
        _touch_xpt2046.config(cfg);
        display.getPanel()->setTouch(&_touch_xpt2046);
        display.getPanel()->initTouch();
    }

    setBacklightPin(GPIO_NUM_21);

    enc_a = GPIO_NUM_22;
    enc_b = GPIO_NUM_27;
#    ifdef CYD_BUTTONS
    red_button_pin   = GPIO_NUM_4;   // RGB LED Red
    dial_button_pin  = GPIO_NUM_17;  // RGB LED Blue
    green_button_pin = GPIO_NUM_16;  // RGB LED Green
#    else
    red_button_pin = dial_button_pin = green_button_pin = -1;
#    endif
}
#endif  // RESISTIVE_CYD

bool round_display = false;

const int n_buttons      = 3;
const int button_w       = 80;
const int button_h       = 80;
const int button_half_wh = button_w / 2;
const int sprite_wh      = 240;
Point     button_wh(button_w, button_h);

int button_colors[] = { RED, YELLOW, GREEN };
class Layout {
private:
    int _rotation;

public:
    Point buttonsXY;
    Point buttonsWidth;
    Point buttonsHeight;
    Point buttonsWH;
    Point spritePosition;
    Layout(int rotation, Point spritePosition, Point firstButtonPosition) :
        _rotation(rotation), spritePosition(spritePosition), buttonsXY(firstButtonPosition) {
        if (_rotation & 1) {  // Vertical
            buttonsWH = { button_w, sprite_wh };
        } else {
            buttonsWH = { sprite_wh, button_h };
        }
    }
    Point buttonOffset(int n) { return (_rotation & 1) ? Point(0, n * button_h) : Point(n * button_w, 0); }

    int rotation() { return _rotation; }
};

// clang-format off
Layout layouts[] = {
// rotation  sprite_XY        button0_XY
    { 0,      { 0, 0 },        { 0, sprite_wh } }, // Buttons below
    { 0,      { 0, button_h }, { 0, 0 }         }, // Buttons above
    { 1,      { 0, 0 },        { sprite_wh, 0 } }, // Buttons right
    { 1,      { button_w, 0 }, { 0, 0 }         }, // Buttons left
    { 2,      { 0, 0 },        { 0, sprite_wh } }, // Buttons below
    { 2,      { 0, button_h }, { 0, 0 }         }, // Buttons above
    { 3,      { button_w, 0 }, { 0, 0 }         }, // Buttons left
    { 3,      { 0, 0 },        { sprite_wh, 0 } }, // Buttons right
};
int num_layouts = sizeof(layouts)/sizeof(layouts[0]);
// clang-format on

Layout* layout;
int32_t layout_num = 0;

Point sprite_offset;
void  set_layout(int n) {
     layout = &layouts[n];
     display.setRotation(layout->rotation());
     sprite_offset = layout->spritePosition;
}

nvs_handle_t hw_nvs;

int32_t display_num = 0;

#if defined(RESISTIVE_CYD) && defined(CAPACITIVE_CYD)
bool try_touch(const char* message) {
    dbg_printf("Trying %s\n", message);
    display.fillRect(0, 0, display.width(), display.height(), WHITE);
    display.setFont(&fonts::FreeSansBold18pt7b);
    display.setTextDatum(middle_center);
    display.setTextColor(BLACK);
    display.drawString(message, display.width() / 2, 30);
    display.drawString("Tap", display.width() / 2, 100);
    display.drawString("the", display.width() / 2, 140);
    display.drawString("Screen", display.width() / 2, 180);
    uint32_t timeout = millis() + 2000;
    touch.begin(&display);
    while (millis() < timeout) {
        lgfx::touch_point_t t;
        if (display.getTouch(&t, 1)) {
            dbg_printf("Touched\n");
            return true;
        }
        delay(50);
    }
    return false;
}

int try_touch_chips() {
    // Initially turn on both backlight possibilities to light up the screen
    pinMode(21, OUTPUT);
    digitalWrite(21, 1);
    pinMode(27, OUTPUT);
    digitalWrite(27, 1);

    while (true) {
        init_capacitive_cyd();
        if (try_touch("Capacitive -")) {
            return 2;
        }
        init_resistive_cyd();
        if (try_touch("Resistive -")) {
            return 1;
        }
    }
}
void choose_board() {
    uint32_t timeout = millis() + 1000;
    pinMode(0, INPUT);
    while (millis() < timeout) {
        if (digitalRead(0) == 0) {
            nvs_set_i32(hw_nvs, "display", 0);
            nvs_set_i32(hw_nvs, "layout", 0);
            break;
        }
        delay(50);
    }

    nvs_get_i32(hw_nvs, "display", &display_num);

    switch (display_num) {
        case 0:
            display_num = try_touch_chips();
            nvs_set_i32(hw_nvs, "display", display_num);
            esp_restart();
            break;
        case 1:
            init_resistive_cyd();
            break;
        case 2:
            init_capacitive_cyd();
            break;
    }
}
#else
void choose_board() {
#    ifdef RESISTIVE_CYD
    init_resistive_cyd();
#    endif
#    ifdef CAPACITIVE_CYD
    init_capacitive_cyd();
#    endif
}
#endif

void initButton(int n) {
    buttons[n].setColorDepth(display.getColorDepth());
    buttons[n].createSprite(button_w, button_h);
    buttons[n].fillRect(0, 0, 80, 80, BLACK);
    const int   radius = 28;
    const char* filename;
    int         color;
    switch (n) {
        case 0:
            color    = RED;
            filename = "/red_button.png";
            break;
        case 1:
            color    = YELLOW;
            filename = "/orange_button.png";
            break;
        case 2:
            color    = GREEN;
            filename = "/green_button.png";
            break;
    }
    buttons[n].fillCircle(button_half_wh, button_half_wh, radius, color);
    // If the image file exists the image will overwrite the circle
    buttons[n].drawPngFile(LittleFS, filename, 10, 10, 60, 60, 0, 0, 0.0f, 0.0f, datum_t::top_left);
}

void initLockedButton() {
    locked_button.setColorDepth(display.getColorDepth());
    locked_button.createSprite(button_w, button_h);

    locked_button.fillRect(0, 0, button_w, button_h, BLACK);
    const int radius = 28;
    locked_button.fillCircle(button_half_wh, button_half_wh, radius, DARKGREY);
}

static void initButtons() {
    // On-screen buttons
    for (int i = 0; i < 3; i++) {
        initButton(i);
    }
    // Greyed-out button for locked state
    initLockedButton();
}

void init_hardware() {
#ifdef DEBUG_TO_USB
    Serial.begin(115200);
#endif
    hw_nvs = nvs_init("hardware");

    init_light();
    init_bus();
    // or ... 0x04, 1, 3) == 0x0
    if (read_panel_reg(&bus, GPIO_NUM_15, 0xda, 0, 1) == 0x0) {
        dbg_printf("ILI9341 panel\n");
        init_panel_ili9341();
    } else {
        dbg_printf("ST7789 panel\n");
        init_panel_st7789();
    }
    display.init();

    choose_board();

    nvs_get_i32(hw_nvs, "layout", &layout_num);

    set_layout(layout_num);

    touch.begin(&display);  // installs ESP-IDF I2C driver for I2C_NUM_0

    init_encoder(enc_a, enc_b);
    init_fnc_uart(FNC_UART_NUM, PND_TX_FNC_RX_PIN, PND_RX_FNC_TX_PIN);

    touch.setFlickThresh(10);

    battery_init();  // after touch.begin(): display_num set + I2C_NUM_0 driver installed

#ifdef LED_DEBUG
    // RGB LED pins
    pinMode(4, OUTPUT);   // Red
    pinMode(16, OUTPUT);  // Green
    pinMode(17, OUTPUT);  // Blue
#endif
}

int last_locked = -1;

void redrawButtons() {
    display.startWrite();
    for (int i = 0; i < n_buttons; i++) {
        Point position = layout->buttonsXY + layout->buttonOffset(i);
        printf("button position %d,%d\n", position.x, position.y);
        auto& sprite = last_locked == 1 ? locked_button : buttons[i];
        sprite.pushSprite(position.x, position.y);
    }
    display.endWrite();
}

void show_logo() {
    display.clear();
    display.drawPngFile(
        LittleFS, "/fluid_dial.png", sprite_offset.x, sprite_offset.y, sprite_wh, sprite_wh, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
}

void base_display() {
    initButtons();
    redrawButtons();
}
void next_layout(int delta) {
    layout_num += delta;
    while (layout_num >= num_layouts) {
        layout_num -= num_layouts;
    }
    while (layout_num < 0) {
        layout_num += num_layouts;
    }
    dbg_printf("Layout %d\n", layout_num);
    delay(200);
    set_layout(layout_num);
    nvs_set_i32(hw_nvs, "layout", layout_num);
    redrawButtons();
}

void system_background() {
    drawBackground(BLACK);
}

bool switch_button_touched(bool& pressed, int& button) {
    static int last_red   = -1;
    static int last_green = -1;
    static int last_dial  = -1;
    bool       state;
    if (red_button_pin != -1) {
        state = digitalRead(red_button_pin);
        if ((int)state != last_red) {
            last_red = state;
            button   = 0;
            pressed  = !state;
            return true;
        }
    }
    if (dial_button_pin != -1) {
        state = digitalRead(dial_button_pin);
        if ((int)state != last_dial) {
            last_dial = state;
            button    = 1;
            pressed   = !state;
            return true;
        }
    }
    if (green_button_pin != -1) {
        state = digitalRead(green_button_pin);
        if ((int)state != last_green) {
            last_green = state;
            button     = 2;
            pressed    = !state;
            return true;
        }
    }
    return false;
}

bool screen_encoder(int x, int y, int& delta) {
    return false;
}

struct button_debounce_t {
    bool    debouncing;
    bool    skipped;
    int32_t timeout;
} debounce[n_buttons] = { { false, false, 0 } };

bool    touch_debounce = false;
int32_t touch_timeout  = 0;

bool ui_locked() {
    bool locked = digitalRead(lockout_pin);
    if ((int)locked != last_locked) {
        last_locked = locked;
        redrawButtons();
    }
    return locked;
}

bool in_rect(Point test, Point xy, Point wh) {
    return test.x >= xy.x && test.x < (xy.x + wh.x) && test.y >= xy.y && test.y < (xy.y + wh.y);
}
bool in_button_stripe(Point xy) {
    return in_rect(xy, layout->buttonsXY, layout->buttonsWH);
}
bool screen_button_touched(bool pressed, int x, int y, int& button) {
    Point xy(x, y);
    if (!in_button_stripe(xy)) {
        return false;
    }
    xy -= layout->buttonsXY;
    for (int i = 0; i < n_buttons; i++) {
        if (in_rect(xy, layout->buttonOffset(i), button_wh)) {
            button = i;
            if (!pressed) {
                touch_debounce = true;
                touch_timeout  = milliseconds() + 100;
            }
            return true;
        }
    }
    return false;
}

void update_events() {
    auto ms = lgfx::millis();
    if (touch.isEnabled()) {
        if (touch_debounce) {
            if ((ms - touch_timeout) < 0) {
                return;
            }
            touch_debounce = false;
        }
        touch.update(ms);
    }
}

void ackBeep() {}

void deep_sleep(int us) {
    // Wake source: red button (GPIO4, INPUT_PULLUP, LOW when pressed).
    // GPIO4 = RTC_GPIO10 — one of only a handful of RTC-capable GPIOs on the classic
    // ESP32.  GPIO16 (green) and GPIO17 (yellow) are NOT RTC GPIOs and cannot be used
    // as ext0/ext1 wakeup sources; attempts to do so silently fail.
    // On wakeup the chip performs a full reset and boots normally; the red button press
    // is processed as a regular E-stop in pendant_hw_task once the UI is running.
    rtc_gpio_init(GPIO_NUM_4);
    rtc_gpio_set_direction(GPIO_NUM_4, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(GPIO_NUM_4);
    rtc_gpio_pulldown_dis(GPIO_NUM_4);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 0);    // LOW = button pressed = wake
    if (us > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)us);
    }
    esp_deep_sleep_start();
    // Never returns — ESP32 resets on wakeup and boots normally.
}

// ── Battery ADC ───────────────────────────────────────────────────────────────
// GPIO39 reads the battery voltage through a resistor divider (×1.534 correction).
// Only supported on capacitive CYD — on resistive boards GPIO39 is the XPT2046
// MISO line and must not be reconfigured as an ADC input.
// Compile-time enable: add -DCYD_BATTERY_ADC to the build flags.
#ifdef CYD_BATTERY_ADC

static bool  bat_ready      = false;
static bool  ip5306_present = false;  // true when IP5306 PMIC is found on I2C bus
static float bat_ema_mv     = 0.0f;  // exponential moving average, millivolts

// 10-point LiPo discharge curve {voltage_mV, percent}, high→low order
static const int16_t bat_curve[10][2] = {
    { 4200, 100 }, { 4100, 91 }, { 4000, 79 }, { 3900, 65 },
    { 3800, 52  }, { 3700, 38 }, { 3600, 23 }, { 3500, 11 },
    { 3400,  4  }, { 3300,  0 }
};

// One-shot I2C register read on the LovyanGFX-owned I2C_NUM_0 bus (SDA=33, SCL=32).
// Does NOT re-initialise the port — the touch driver already installed the ESP-IDF driver.
static esp_err_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t* out) {
    return i2c_master_write_read_device(
        I2C_NUM_0, addr, &reg, 1, out, 1, pdMS_TO_TICKS(10));
}

void battery_init() {
    if (display_num == 1) return;  // resistive: GPIO39 = XPT2046 MISO — do not touch

    // ADC channel for voltage measurement via the on-board resistor divider
    analogSetPinAttenuation(GPIO_NUM_39, ADC_11db);
    bat_ready = true;

    // Probe IP5306 battery management IC at I2C address 0x75.
    // It shares I2C_NUM_0 with the CST816S touch controller (different address: 0x15).
    //
    // The probe is retried up to 5 times because:
    //   • Some IP5306 silicon needs ~30 ms after power-up before responding.
    //   • Touch.begin() leaves transactions in flight; the first probe right
    //     after it can collide on the bus and time out spuriously.
    //   • The 10 ms i2c_master_write_read_device() timeout can be too tight
    //     when the touch controller is mid-poll on the shared bus.
    //
    // We log every attempt so users diagnosing "battery pendant boots in UART
    // mode" can see exactly what the bus reported.
    ip5306_present = false;
    uint8_t status = 0;
    for (int attempt = 1; attempt <= 5 && !ip5306_present; ++attempt) {
        esp_err_t err = i2c_read_reg(0x75, 0x70, &status);
        dbg_printf("IP5306 probe %d/5: err=%d (%s) reg70=0x%02x\n",
                   attempt, err, esp_err_to_name(err), status);
        if (err == ESP_OK) {
            ip5306_present = true;
            break;
        }
        delay(20);  // give the chip / bus a moment before retrying
    }
    dbg_printf("IP5306 %s after retries (reg70=0x%02x)\n",
               ip5306_present ? "found" : "NOT FOUND", status);
}

int battery_millivolts() {
    if (!bat_ready) return 0;
    int raw_mv = analogReadMilliVolts(GPIO_NUM_39);
    int scaled = (int)((long)raw_mv * 1534 / 1000);  // resistor-divider correction
    if (scaled < 3000 || scaled > 4400) return (int)bat_ema_mv;  // reject out-of-range
    bat_ema_mv = (bat_ema_mv < 1.0f) ? (float)scaled
                                      : bat_ema_mv * 0.75f + (float)scaled * 0.25f;
    return (int)bat_ema_mv;
}

int battery_level() {
    if (!bat_ready) return -1;
    int mv = battery_millivolts();
    if (mv <= 0) return -1;
    // Linear interpolation between curve points
    for (int i = 0; i < 9; i++) {
        if (mv >= bat_curve[i][0]) return bat_curve[i][1];
        if (mv >= bat_curve[i + 1][0]) {
            int span_mv  = bat_curve[i][0]     - bat_curve[i + 1][0];
            int span_pct = bat_curve[i][1]      - bat_curve[i + 1][1];
            return bat_curve[i + 1][1] + (mv - bat_curve[i + 1][0]) * span_pct / span_mv;
        }
    }
    return 0;  // below lowest curve point
}

// True iff the IP5306 PMIC was detected on the I2C bus at boot.  This is
// the definitive signal that this hardware is a battery-equipped (i.e.
// mobile / wireless) pendant.  The Comms layer uses it to decide whether
// to bring up the WiFi stack — wired pendants without the PMIC always use
// UART and never start WiFi.
bool battery_hardware_present() { return ip5306_present; }

// Charging detection by battery-VOLTAGE TREND (Li-Ion cells only).
//
// The IP5306's I2C charge-status bits (0x70/0x71 bit 3) proved unreliable on
// these CYD boards — the chip ACKs on the bus (which is why transport autodetect
// works) but those bits don't track the real charge state.  So instead of asking
// the PMIC, we watch the cell voltage and infer direction.
//
// The pendant runs under a constant WiFi load, so on battery the cell sags and
// its voltage trends DOWN; only an external charger can hold the voltage HIGH or
// push it UP.  We detect that with a fast/slow EMA crossover, evaluated on every
// call (~3 s) so the indicator reacts within ~15-30 s rather than minutes:
//
//   • fast EMA leads the slow EMA while voltage is rising ⇒ charging,
//     trails it while falling ⇒ on battery (a few mV of separation is enough on
//     a Li-Ion cell — no need to wait out a long window).
//   • "pinned near full" (≥ ~4.15 V under load) ⇒ on charger, caught instantly.
//   • Hysteresis: a flat reading holds the previous state, so the icon doesn't
//     flicker as charge current tapers near full.
//
// Caveat: a battery that is *already full* when you plug in (flat ~4.2 V, no
// rise) is only caught by the "pinned near full" rule, not the trend.
bool battery_charging() {
    if (display_num == 1) return false;   // resistive — no battery hardware
    if (!bat_ready)        return false;

    int mv = battery_millivolts();        // EMA-smoothed cell voltage
    static float fast_mv = 0.0f;          // fast EMA (~15 s at 3 s cadence)
    static float slow_mv = 0.0f;          // slow EMA (~60 s)
    static bool  state   = false;

    if (mv <= 0) return state;            // bad reading — hold last decision
    if (fast_mv < 1.0f) {                 // first valid call: seed, assume not charging
        fast_mv = slow_mv = (float)mv;
        return false;
    }

    fast_mv = fast_mv * 0.80f + (float)mv * 0.20f;
    slow_mv = slow_mv * 0.95f + (float)mv * 0.05f;
    float diff = fast_mv - slow_mv;       // + rising (charging), - falling (on battery)

    const float DRIFT_MV = 3.0f;          // Li-Ion: a few mV of rise is a clear signal
    const int   FULL_MV  = 4150;          // held this high under load ⇒ on charger

    if      (mv >= FULL_MV)     state = true;    // pinned near full ⇒ external power
    else if (diff >=  DRIFT_MV) state = true;    // rising ⇒ charging
    else if (diff <= -DRIFT_MV) state = false;   // falling ⇒ on battery
    // else flat ⇒ hold previous state (hysteresis)

    static uint32_t last_log = 0;
    if (millis() - last_log > 5000) {
        last_log = millis();
        dbg_printf("Batt charge-trend: mv=%d fast=%.0f slow=%.0f diff=%+.1f → charging=%d\n",
                   mv, fast_mv, slow_mv, diff, state);
    }
    return state;
}

#else  // ── stubs — compiles on all targets; returns "not available" values ──

void battery_init()              {}
int  battery_millivolts()        { return 0; }
int  battery_level()             { return -1; }
bool battery_charging()          { return false; }
bool battery_hardware_present()  { return false; }   // no PMIC → wired pendant

#endif  // CYD_BATTERY_ADC
