// Copyright (c) 2023 -	Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "FileParser.h"
#include "FluidNCModel.h"   // fnc_init_tx_lock()
#include "Scene.h"
#include "AboutScene.h"

#ifdef USE_NEW_UI
#include "CNC_Pendant_UI.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "screens/pendant_shared.h"
#include <esp_system.h>
#include <esp_attr.h>     // RTC_DATA_ATTR
#include <esp_task_wdt.h> // esp_task_wdt_init
#endif

#ifdef USE_NEW_UI
// Captured at the very start of setup() so we can show it on-screen for
// diagnostics (the USB serial console isn't visible to users on production
// builds because UART0 is hijacked for FluidNC comms).  Read by the WiFi
// Setup screen.
esp_reset_reason_t lastResetReason = ESP_RST_UNKNOWN;

// Boot-stage tracker — RTC memory survives software resets (PANIC, etc.) so
// after a crash the next boot can read the LAST milestone the previous boot
// reached.  Updated at key points; on a clean POWER_ON the noinit value is
// undefined, which is why we pair it with a magic number that we explicitly
// set to a known sentinel.  If the magic doesn't match on next boot, RTC was
// wiped (true cold boot) and the stage value is meaningless.
//
// RTC_NOINIT_ATTR places the variable in RTC slow memory but tells the
// startup code NOT to re-zero it on every boot.  This is more reliable
// than RTC_DATA_ATTR for diagnostics because there's no chance the linker
// silently demotes it to .bss on a misconfigured build.
//
// Stage map:
//   0   stage never set this boot (or RTC wiped)
//   1   setup() entered
//   2   init_system done
//   3   setup_pendant done
//   4   pendant_hw_task started
//   5   comms_init done
//   6   WiFi.begin done (wifi_init returned)
//   7   WL_CONNECTED detected by wifi_poll
//   8   tcp_begin called
//   9   tcp_open returned (success or fail)
//   10  first status report sent over TCP
//   11  first byte received from FluidNC
//   100 pendant_hw_task main loop is iterating normally (set every tick)
#define BOOT_MAGIC 0xC0DEF00Du
RTC_NOINIT_ATTR uint32_t rtcBootMagic;
RTC_NOINIT_ATTR uint32_t rtcLastBootStage;
RTC_NOINIT_ATTR uint32_t rtcCore1Stage;
RTC_NOINIT_ATTR uint32_t rtcCore0Iters;   // pendant_hw_task iteration count
RTC_NOINIT_ATTR uint32_t rtcCore1Iters;   // loop_pendant iteration count
uint32_t capturedBootStage  = 0;   // snapshot of previous boot's Core 0 stage
uint32_t capturedCore1Stage = 0;   // snapshot of previous boot's Core 1 stage
uint32_t capturedCore0Iters = 0;
uint32_t capturedCore1Iters = 0;

const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWER_ON";    // cold boot
        case ESP_RST_EXT:       return "EXT_RESET";   // reset pin
        case ESP_RST_SW:        return "SW_RESET";    // ESP.restart()
        case ESP_RST_PANIC:     return "PANIC";       // firmware crashed
        case ESP_RST_INT_WDT:   return "INT_WDT";     // interrupt watchdog
        case ESP_RST_TASK_WDT:  return "TASK_WDT";    // task watchdog
        case ESP_RST_WDT:       return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";   // woke from sleep
        case ESP_RST_BROWNOUT:  return "BROWNOUT";    // power dipped
        case ESP_RST_SDIO:      return "SDIO";
        case ESP_RST_UNKNOWN:
        default:                return "UNKNOWN";
    }
}
#endif

extern void base_display();
extern void show_logo();

extern const char* git_info;

extern AboutScene aboutScene;

void setup() {
#ifdef USE_NEW_UI
    // Capture reset reason BEFORE anything else — must be read fresh on each boot.
    lastResetReason = esp_reset_reason();
    // Snapshot the previous boot's last-reached stage, then reset for this boot.
    // Trust the stage only if the magic number matches — otherwise treat as 0
    // (true cold boot or RTC memory was wiped by something nasty).
    if (rtcBootMagic == BOOT_MAGIC) {
        capturedBootStage  = rtcLastBootStage;
        capturedCore1Stage = rtcCore1Stage;
        capturedCore0Iters = rtcCore0Iters;
        capturedCore1Iters = rtcCore1Iters;
    } else {
        capturedBootStage  = 0;
        capturedCore1Stage = 0;
        capturedCore0Iters = 0;
        capturedCore1Iters = 0;
    }
    rtcBootMagic      = BOOT_MAGIC;
    rtcLastBootStage  = 1;     // stage 1: setup() entered
    rtcCore1Stage     = 0;
    rtcCore0Iters     = 0;
    rtcCore1Iters     = 0;
#endif
    init_system();
#ifdef USE_NEW_UI
    rtcLastBootStage  = 2;     // stage 2: init_system done

    // Create the TX-line serialization mutex BEFORE the comms/UI tasks start,
    // while we're still single-threaded.  It guards against two cores
    // interleaving their command bytes in the WiFi TX ring (which corrupted
    // file-list commands into bogus G-code → controller Alarm).
    fnc_init_tx_lock();

    // Relax the task watchdog from the Arduino-ESP32 default of 5 s to 15 s.
    // Still well within "something is genuinely wrong" territory but gives
    // the WiFi stack plenty of headroom for retries / reconnects.
    esp_task_wdt_init(15, true);
    // Both IDLE_0 and IDLE_1 are monitored by default — we keep both.
    // Earlier diagnostics suggested IDLE_0 starvation, but that was because
    // pendant_hw_task was pinned to Core 0 doing heavy work alongside the
    // WiFi driver.  Now pendant_hw_task is on Core 1 and only the new
    // pendant_comms_task (lighter — comms_poll + byte drain only) lives on
    // Core 0 with WiFi, so IDLE_0 gets its slices normally and we no
    // longer need the disableCore0WDT() workaround.
#endif

    display.setBrightness(aboutScene.getBrightness());

#ifdef USE_NEW_UI
    display.setRotation(2);  // Boot screen rotation (overridden by saved preference in setup_pendant)
#endif
    show_logo();
    delay_ms(2000);  // view the logo and wait for the debug port to connect

#ifdef USE_NEW_UI

    // Create FreeRTOS sync objects before setup_pendant() so PendantScene
    // callbacks can post events as soon as fnc_poll() starts running.
    stateMutex   = xSemaphoreCreateMutex();
    hwEventQueue = xQueueCreate(32, sizeof(HwEvent));

    // Initialize the new pendant UI on Core 1 (current context)
    setup_pendant();
    rtcLastBootStage = 3;     // stage 3: setup_pendant done

    // ── Task topology ───────────────────────────────────────────────────────
    //
    //   Core 0  (system / network):
    //     • WiFi driver (priority 23) and lwIP tcpip_thread (priority 18) —
    //       both pinned to Core 0 by ESP-IDF by default.
    //     • pendant_comms_task (priority 1) — runs comms_poll(), the byte
    //       drain (fnc_getchar → collect → parser), the periodic '?' status
    //       ping, and the WiFi state cache.  Lives here so byte-level I/O
    //       is on the same core as the underlying drivers — no cross-core
    //       data movement and no IDLE_0 starvation from busy WiFi bursts.
    //     • IDLE_0 (priority 0) — runs in between, feeds the watchdog.
    //
    //   Core 1  (application / UI):
    //     • Arduino loop task = loop_pendant (priority 1) — touch handling,
    //       screen drawing, hwEventQueue processing, requestControllerConfig.
    //     • pendant_hw_task (priority 1) — encoder, buttons, battery,
    //       charging.  Pure hardware polling, no network I/O.  Posts
    //       HwEvents to the queue for loop_pendant to consume.
    //     • IDLE_1 (priority 0) — runs in between, feeds the watchdog.
    //
    // Both pendant tasks at priority 1 — same as the Arduino loop — so
    // FreeRTOS round-robins between them within their core when both are
    // ready.  Stack sizes are 8 KB each (was 4 KB on the old single task
    // but lwIP socket calls and the Arduino WiFi event handler push the
    // call stack deeper than 4 KB on some paths).
    xTaskCreatePinnedToCore(
        pendant_comms_task,
        "PendantComms",
        8192,
        nullptr,
        1,
        nullptr,
        0                  // Core 0 — alongside WiFi driver + lwIP + UART driver
    );
    xTaskCreatePinnedToCore(
        pendant_hw_task,
        "PendantHw",
        4096,              // smaller stack — only encoder/buttons/battery, no lwIP
        nullptr,
        1,
        nullptr,
        1                  // Core 1 — alongside the Arduino loop task
    );

    dbg_printf("FluidNC Pendant with new UI %s\n", git_info);
#else
    base_display();

    dbg_printf("FluidNC Pendant %s\n", git_info);

    extern Scene* initMenus();
    activate_scene(initMenus());
#endif

    fnc_realtime(StatusReport);  // Kick FluidNC into action
}

void loop() {
    // Core 1: UI only — touch, display, event queue processing
    // Hardware (UART, encoder, buttons) runs on Core 0 in pendant_hw_task()
#ifdef USE_NEW_UI
    loop_pendant();
#else
    fnc_poll();         // Handle messages from FluidNC
    dispatch_events();  // Handle dial, touch, buttons
#endif
}
