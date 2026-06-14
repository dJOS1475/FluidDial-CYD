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

// Two FreeRTOS tasks owned by the pendant, both created by ardmain.cpp:
//
//   pendant_comms_task — pinned to Core 0, alongside the WiFi driver and
//                        the ESP-IDF UART driver.  Runs comms_poll(), the
//                        byte drain (fnc_getchar → collect → parser), the
//                        periodic '?' status ping, and the WiFi state
//                        cache.  All network and serial I/O lives here so
//                        Core 0 is the single home for byte-level work.
//
//   pendant_hw_task    — pinned to Core 1, alongside the Arduino loop task.
//                        Polls the encoder + buttons + battery and posts
//                        HwEvents to the queue.  Does no network I/O.
//
// This split lets IDLE0 run normally (no more WiFi-driver-starves-idle
// crashes) and keeps button/encoder polling independent of WiFi load.
void pendant_hw_task(void* pvParameters);
void pendant_comms_task(void* pvParameters);

// Static controller config ($30, $31, $110, $130-$133, FluidNC version, IP, SSID)
// is fetched automatically on the connection edge via HwEvent::CONNECTED.
// Spindle Control re-fetches $30/$31 on entry as a defensive measure, since
// max RPM presets and dial limits would silently fall back to defaults if the
// connect-edge fetch was dropped (e.g. controller busy emitting status reports
// at the moment of edge detection).
void requestSpindleConfig();

#endif  // CNC_PENDANT_UI_H
