// Copyright (c) 2023 -	Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "FileParser.h"
#include "Scene.h"
#include "AboutScene.h"

#ifdef USE_NEW_UI
#include "CNC_Pendant_UI.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "screens/pendant_shared.h"
#endif

extern void base_display();
extern void show_logo();

extern const char* git_info;

extern AboutScene aboutScene;

void setup() {
    init_system();

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

    // Start hardware task on Core 0: UART, encoder, buttons
    xTaskCreatePinnedToCore(
        pendant_hw_task,   // task function
        "PendantHW",       // name
        4096,              // stack size (bytes)
        nullptr,           // parameter
        1,                 // priority — same as loop; keep below FluidNC Core 0 tasks
        nullptr,           // handle (not needed)
        0                  // core 0
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
