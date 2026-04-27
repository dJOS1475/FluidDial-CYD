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

// Static controller config ($30, $31, $110, $130-$133, FluidNC version, IP, SSID)
// is fetched automatically on the connection edge via HwEvent::CONNECTED.
// Spindle Control re-fetches $30/$31 on entry as a defensive measure, since
// max RPM presets and dial limits would silently fall back to defaults if the
// connect-edge fetch was dropped (e.g. controller busy emitting status reports
// at the moment of edge detection).
void requestSpindleConfig();

#endif  // CNC_PENDANT_UI_H
