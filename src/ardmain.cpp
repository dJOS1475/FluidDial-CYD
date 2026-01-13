// Copyright (c) 2023 -	Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "FileParser.h"
#include "Scene.h"
#include "AboutScene.h"
#include "CNC_Pendant_UI.h"

extern void base_display();
extern void show_logo();

extern const char* git_info;

extern AboutScene aboutScene;

void setup() {
    init_system();

    // Set display brightness
    display.setBrightness(255);

    // Set initial rotation for logo (will be overridden by pendant preferences)
    display.setRotation(2);

    show_logo();
    delay_ms(2000);  // view the logo and wait for the debug port to connect

    // Initialize the new pendant UI (loads rotation preference)
    setup_pendant();

    dbg_printf("FluidNC Pendant with new UI %s\n", git_info);

    fnc_realtime(StatusReport);  // Kick FluidNC into action
}

void loop() {
    fnc_poll();         // Handle messages from FluidNC
    loop_pendant();     // Handle pendant UI (touch, buttons, display)
}
