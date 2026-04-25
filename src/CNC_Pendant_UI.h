/*
 * CNC Pendant UI Header
 * Integrated with FluidDial-CYD project
 */

#ifndef CNC_PENDANT_UI_H
#define CNC_PENDANT_UI_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Public interface called from ardmain.cpp
void setup_pendant();
void loop_pendant();
void drawCurrentPendantScreen();

// Core 0 hardware task — created by ardmain.cpp after setup_pendant()
void pendant_hw_task(void* pvParameters);

// Request $30/$31 (spindle max/min RPM) from FluidNC — call from spindle screen entry
void requestSpindleConfig();
// Deferred version — fires in next loop_pendant() iteration, after screen is drawn
void requestSpindleConfigDeferred();

#endif  // CNC_PENDANT_UI_H
