/*
 * CNC Pendant UI — Coordinator
 *
 * Dual-core architecture:
 *   Core 0 (pendant_hw_task): UART/FluidNC polling, PCNT encoder, button debounce
 *   Core 1 (Arduino loop):    All display/touch/UI operations
 *
 * Shared state is protected by stateMutex.
 * Hardware events travel Core 0 → Core 1 via hwEventQueue.
 */

#include "cnc_pendant_config.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include "System.h"
#include "Scene.h"
#include "FluidNCModel.h"
#include "FileParser.h"
#include "ConfigItem.h"
#include "Encoder.h"
#include "GrblParserC.h"

// Screen files
#include "screens/pendant_shared.h"
#include "screens/screen_main_menu.h"
#include "screens/screen_status.h"
#include "screens/screen_jog_homing.h"
#include "screens/screen_probing_work.h"
#include "screens/screen_probing.h"
#include "screens/screen_feeds_speeds.h"
#include "screens/screen_spindle_control.h"
#include "screens/screen_macros.h"
#include "screens/screen_sd_card.h"
#include "screens/screen_fluidnc.h"

// ===== Hardware pin externs (defined in Hardware2432.cpp) =====
extern int red_button_pin;
extern int dial_button_pin;
extern int green_button_pin;

// ===== NVS Preferences =====
Preferences preferences;

// ===== FreeRTOS Sync Objects =====
SemaphoreHandle_t stateMutex   = nullptr;
QueueHandle_t     hwEventQueue = nullptr;

// ===== Connection State =====
// Driven exclusively by fnc_is_connected() on Core 0 (backed by update_rx_time() per UART byte).
volatile bool pendantConnected = false;

// ===== Screen State =====
PendantScreen currentPendantScreen = PSCREEN_MAIN_MENU;

// ===== Machine & UI State Variables =====
MachineState  pendantMachine;
JogState      pendantJog;
SDCardState   pendantSdCard;
MacroState    pendantMacros;
SpindleState  pendantSpindle;
FeedsState    pendantFeeds;
ProbingState  pendantProbing;
ProbeState    pendantProbe;

// ===== Shared Sprite Buffers (reused across screens) =====
LGFX_Sprite spriteAxisDisplay(&display);
LGFX_Sprite spriteValueDisplay(&display);
LGFX_Sprite spriteStatusBar(&display);
LGFX_Sprite spriteFileDisplay(&display);
bool        spritesInitialized = false;

// ===== Helper Functions =====
bool isTouchInBounds(int tx, int ty, int x, int y, int w, int h) {
    return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    display.fillRoundRect(x, y, w, h, r, color);
}

void drawButton(int x, int y, int w, int h, String text, uint16_t bgColor, uint16_t textColor, int textSize) {
    drawRoundRect(x, y, w, h, 8, bgColor);
    display.setTextColor(textColor);
    display.setTextSize(textSize);
    int16_t tw = display.textWidth(text.c_str());
    int16_t th = display.fontHeight();
    display.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    display.print(text);
}

void drawMultiLineButton(int x, int y, int w, int h, String line1, String line2,
                         uint16_t bgColor, uint16_t textColor, int textSize) {
    drawRoundRect(x, y, w, h, 8, bgColor);
    display.setTextColor(textColor);
    display.setTextSize(textSize);
    int16_t fh     = display.fontHeight();
    int16_t totalH = fh * 2 + 4;
    int16_t startY = y + (h - totalH) / 2;
    int16_t tw1    = display.textWidth(line1.c_str());
    display.setCursor(x + (w - tw1) / 2, startY);
    display.print(line1);
    int16_t tw2 = display.textWidth(line2.c_str());
    display.setCursor(x + (w - tw2) / 2, startY + fh + 4);
    display.print(line2);
}

void drawTitle(String title) {
    display.fillRect(0, 0, 240, 35, COLOR_DARKER_BG);
    display.setTextColor(COLOR_TITLE);
    display.setTextSize(2);
    int16_t tw = display.textWidth(title.c_str());
    display.setCursor((240 - tw) / 2, 10);
    display.print(title);
}

void drawInfoBox(int x, int y, int w, int h, String label, String value, uint16_t valueColor) {
    display.fillRoundRect(x, y, w, h, 5, COLOR_DARKER_BG);
    display.setTextColor(COLOR_GRAY_TEXT);
    display.setTextSize(1);
    display.setCursor(x + 5, y + 5);
    display.print(label);
    display.setTextColor(valueColor);
    display.setTextSize(2);
    display.setCursor(x + 5, y + 20);
    display.print(value);
}

// ===== Screen Lifecycle Routing =====
static void callScreenExit(PendantScreen s) {
    switch (s) {
        case PSCREEN_MAIN_MENU:       exitMainMenu();        break;
        case PSCREEN_STATUS:          exitStatus();          break;
        case PSCREEN_JOG_HOMING:      exitJogHoming();       break;
        case PSCREEN_PROBING_WORK:    exitProbingWork();     break;
        case PSCREEN_PROBING:         exitProbing();         break;
        case PSCREEN_FEEDS_SPEEDS:    exitFeedsSpeeds();     break;
        case PSCREEN_SPINDLE_CONTROL: exitSpindleControl();  break;
        case PSCREEN_MACROS:          exitMacros();          break;
        case PSCREEN_SD_CARD:         exitSDCard();          break;
        case PSCREEN_FLUIDNC:         exitFluidNC();         break;
    }
}

static void callScreenEnter(PendantScreen s) {
    switch (s) {
        case PSCREEN_MAIN_MENU:       enterMainMenu();       break;
        case PSCREEN_STATUS:          enterStatus();         break;
        case PSCREEN_JOG_HOMING:      enterJogHoming();      break;
        case PSCREEN_PROBING_WORK:    enterProbingWork();    break;
        case PSCREEN_PROBING:         enterProbing();        break;
        case PSCREEN_FEEDS_SPEEDS:    enterFeedsSpeeds();    break;
        case PSCREEN_SPINDLE_CONTROL: enterSpindleControl(); break;
        case PSCREEN_MACROS:          enterMacros();         break;
        case PSCREEN_SD_CARD:         enterSDCard();         break;
        case PSCREEN_FLUIDNC:         enterFluidNC();        break;
    }
}

void drawCurrentPendantScreen() {
    switch (currentPendantScreen) {
        case PSCREEN_MAIN_MENU:       drawMainMenu();             break;
        case PSCREEN_STATUS:          drawStatusScreen();          break;
        case PSCREEN_JOG_HOMING:      drawJogHomingScreen();       break;
        case PSCREEN_PROBING_WORK:    drawProbingWorkScreen();     break;
        case PSCREEN_PROBING:         drawProbingScreen();         break;
        case PSCREEN_FEEDS_SPEEDS:    drawFeedsSpeedsScreen();     break;
        case PSCREEN_SPINDLE_CONTROL: drawSpindleControlScreen();  break;
        case PSCREEN_MACROS:          drawMacrosScreen();          break;
        case PSCREEN_SD_CARD:         drawSDCardScreen();          break;
        case PSCREEN_FLUIDNC:         drawFluidNCScreen();         break;
    }
}

void navigateTo(PendantScreen next) {
    if (next == currentPendantScreen) return;
    callScreenExit(currentPendantScreen);
    currentPendantScreen = next;
    callScreenEnter(next);
    drawCurrentPendantScreen();
}

// ===== Touch Dispatch (Core 1) =====
static uint32_t lastNavMs = 0;  // timestamp of last screen navigation

static void handlePendantTouch(int x, int y) {
    // Ignore touch events for 350 ms after a navigation to prevent the same
    // tap from registering on the newly-drawn screen (touch bounce).
    if ((uint32_t)milliseconds() - lastNavMs < 350) return;

    PendantScreen before = currentPendantScreen;

    switch (currentPendantScreen) {
        case PSCREEN_MAIN_MENU:       handleMainMenuTouch(x, y);       break;
        case PSCREEN_JOG_HOMING:      handleJogHomingTouch(x, y);      break;
        case PSCREEN_SPINDLE_CONTROL: handleSpindleControlTouch(x, y); break;
        case PSCREEN_FEEDS_SPEEDS:    handleFeedsSpeedsTouch(x, y);    break;
        case PSCREEN_SD_CARD:         handleSDCardTouch(x, y);         break;
        case PSCREEN_PROBING_WORK:    handleProbingWorkTouch(x, y);    break;
        case PSCREEN_PROBING:         handleProbingTouch(x, y);        break;
        case PSCREEN_MACROS:          handleMacrosTouch(x, y);         break;
        case PSCREEN_STATUS:          handleStatusTouch(x, y);         break;
        case PSCREEN_FLUIDNC:         handleFluidNCTouch(x, y);        break;
    }

    if (currentPendantScreen != before) {
        PendantScreen dest = currentPendantScreen;
        currentPendantScreen = before;   // restore so navigateTo sees correct previous
        navigateTo(dest);
        lastNavMs = (uint32_t)milliseconds();  // start cooldown
    }
}

// ===== Encoder Delta Handler (Core 1) =====
static void handleEncoderDelta(int32_t delta) {
    if (currentPendantScreen == PSCREEN_SPINDLE_CONTROL && pendantSpindle.dialMode) {
        int maxRPM = pendantMachine.spindleMaxRPM > 0 ? pendantMachine.spindleMaxRPM : 24000;
        int minRPM = pendantMachine.spindleMinRPM;
        pendantSpindle.targetRPM = constrain(pendantSpindle.targetRPM + delta * 1000, minRPM, maxRPM);
        updateSpindleRPMDisplay();
        return;
    } else if (currentPendantScreen == PSCREEN_JOG_HOMING) {
        if (!pendantConnected) return;
        if (pendantJog.speedDialMode) {
            // Adjust jog speed cap — metric: 500 mm/min/step, imperial: 20 ipm/step
            int maxIn = constrain((int)(pendantJog.maxFeedRate / 25.4f), 40, 400);
            if (pendantMachine.inInches) {
                pendantJog.jogSpeedIn = constrain(pendantJog.jogSpeedIn + delta * 20, 40, maxIn);
            } else {
                pendantJog.jogSpeedMm = constrain(pendantJog.jogSpeedMm + delta * 500, 1000, pendantJog.maxFeedRate);
            }
            redrawJogSpeedButton();
            updateJogAxisDisplay();
            return;
        }
        if (pendantJog.selectedAxis < 0) return;  // no axis selected — do nothing

        // Send $J immediately per tick (like cyd_buttons) so FluidNC's planner buffer
        // stays populated and the deceleration ramp bridges the gap between ticks.
        // Time-based velocity scaling: fast turns send a proportionally larger distance.
        {
            static unsigned long lastTickMs = 0;
            unsigned long now = millis();
            unsigned long interval = now - lastTickMs;
            lastTickMs = now;
            int velFactor = (interval < 80) ? 4 : (interval < 150) ? 2 : 1;

            String axisNames[] = { "X", "Y", "Z", "A" };
            float  distance    = (float)delta * velFactor * pendantJog.increment;

            // Safety clamp: never request more than half the axis travel range in a
            // single jog tick. Prevents a fast wheel turn at a coarse increment from
            // queueing a move that would crash into a hard stop or trip soft limits.
            // $13x is reported in mm regardless of G20/G21 — convert to inches if needed.
            // Falls back to a hard-coded cap (100 mm / 4 in) if the controller hasn't
            // reported $13x yet (e.g. immediately after connect).
            {
                int   axis  = pendantJog.selectedAxis;
                float capMm = (pendantJog.maxTravel[axis] > 0)
                                ? pendantJog.maxTravel[axis] * 0.5f
                                : 100.0f;
                float cap   = pendantMachine.inInches ? (capMm / 25.4f) : capMm;
                if (distance >  cap) distance =  cap;
                if (distance < -cap) distance = -cap;
            }

            char   cmd[64];
            if (pendantMachine.inInches) {
                int maxIn = constrain((int)(pendantJog.maxFeedRate / 25.4f), 40, 400);
                int speed = constrain(pendantJog.jogSpeedIn, 40, maxIn);
                snprintf(cmd, sizeof(cmd), "$J=G91 G20 %s%.4f F%d",
                         axisNames[pendantJog.selectedAxis].c_str(), distance, speed);
            } else {
                int speed = constrain(pendantJog.jogSpeedMm, 1000, pendantJog.maxFeedRate);
                snprintf(cmd, sizeof(cmd), "$J=G91 G21 %s%.3f F%d",
                         axisNames[pendantJog.selectedAxis].c_str(), distance, speed);
            }
            send_line(cmd);
        }
    } else if (currentPendantScreen == PSCREEN_FEEDS_SPEEDS) {
        if (!pendantConnected) return;
        if (pendantFeeds.dialMode == 1) {
            // Feed override — 10% per detent via 10× fine steps
            int steps = abs(delta) * 10;
            for (int i = 0; i < steps; i++)
                fnc_realtime(delta > 0 ? FeedOvrFinePlus : FeedOvrFineMinus);
            updateFeedOverrideDisplay();
        } else if (pendantFeeds.dialMode == 2) {
            // Spindle override — 10% per detent via 10× fine steps
            int steps = abs(delta) * 10;
            for (int i = 0; i < steps; i++)
                fnc_realtime(delta > 0 ? SpindleOvrFinePlus : SpindleOvrFineMinus);
            updateSpindleOverrideDisplay();
        }
        return;
    } else if (currentPendantScreen == PSCREEN_FLUIDNC) {
        // Toggle display rotation. NVS write is deferred to exitFluidNC() —
        // a rapid spin would otherwise hammer flash with redundant writes.
        // The pending flag tells exitFluidNC() that the rotation differs from
        // what's stored on flash and a putInt() is required.
        static unsigned long lastRotationMs = 0;
        if (millis() - lastRotationMs > 300) {
            int newRot = (pendantMachine.rotation == 2) ? 0 : 2;
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                pendantMachine.rotation        = newRot;
                pendantMachine.displayRotation = (newRot == 2) ? "Normal" : "Upside Down";
                xSemaphoreGive(stateMutex);
            }
            display.setRotation(newRot);
            pendantMachine.rotationDirty = true;
            drawCurrentPendantScreen();
            lastRotationMs = millis();
        }
    }
}

void saveJogPrefs() {
    preferences.begin("pendant", false);
    preferences.putBool("jogFineMode", pendantJog.fineIncrements);
    preferences.putInt("jogSelInc",    pendantJog.selectedIncrement);
    preferences.end();
}

// ===== Sprite Periodic Update (Core 1, 100ms cadence) =====
static void updateCurrentScreenSprites() {
    if (!spritesInitialized) return;
    switch (currentPendantScreen) {
        case PSCREEN_MAIN_MENU:
            updateMainMenuDisplay();
            break;
        case PSCREEN_JOG_HOMING:
            updateJogAxisDisplay();
            break;
        case PSCREEN_PROBING_WORK:
            updateWorkMachinePos();
            updateWorkAreaPos();
            break;
        case PSCREEN_FEEDS_SPEEDS:
            updateFeedsSpeedsTopDisplay();
            updateFeedOverrideDisplay();
            updateSpindleOverrideDisplay();
            break;
        case PSCREEN_SPINDLE_CONTROL:
            updateSpindleRPMDisplay();
            break;
        case PSCREEN_PROBING:
            updateProbePositionDisplay();
            updateProbeSettingsDisplay();
            break;
        case PSCREEN_STATUS:
            updateStatusMachineStatus();
            updateStatusCurrentFile();
            updateStatusAxisPositions();
            updateStatusFeedSpindle();
            break;
        case PSCREEN_FLUIDNC:
            updateFluidNCDisplay();
            break;
        case PSCREEN_SD_CARD:
            updateSDCardFileList();
            break;
        case PSCREEN_MACROS:
            updateMacrosFileList();
            break;
        default:
            break;
    }
}

// ===== Static controller config items =====
// All read once on the connection edge (HwEvent::CONNECTED) and cached.
// Screens read the cached values directly — no per-screen UART round-trips.
//   $30, $31      — spindle max / min RPM
//   $110          — X-axis max rate (jog feed cap)
//   $130-$133     — per-axis max travel (used to clamp per-tick jog distance)
// Values land in pendantMachine / pendantJog via PendantScene::reDisplay() callbacks.
static IntConfigItem spindleMaxItem ("$30");
static IntConfigItem spindleMinItem ("$31");
static IntConfigItem jogMaxRateItem ("$110");
static IntConfigItem jogMaxTravelX  ("$130");
static IntConfigItem jogMaxTravelY  ("$131");
static IntConfigItem jogMaxTravelZ  ("$132");
static IntConfigItem jogMaxTravelA  ("$133");

// Called from loop_pendant() when HwEvent::CONNECTED arrives.
// FluidNC version, IP address, WiFi SSID arrive automatically via [VER:] / status
// callbacks once a connection is established — no explicit query required.
static void requestControllerConfig() {
    spindleMaxItem.init();
    spindleMinItem.init();
    jogMaxRateItem.init();
    jogMaxTravelX.init();
    jogMaxTravelY.init();
    jogMaxTravelZ.init();
    jogMaxTravelA.init();
}

// Called from enterSpindleControl() — defensive re-fetch of $30/$31. Restores
// the v1.5.5 behaviour where the spindle screen always sees fresh values, in
// case the connect-edge fetch was dropped (the user reported max/min reverting
// to defaults after Start/Stop). Cheap: just two short UART queries on entry.
void requestSpindleConfig() {
    if (!pendantConnected) return;
    spindleMaxItem.init();
    spindleMinItem.init();
}

// ===== Macro request — reads preferences.json (then macrocfg.json fallback) via UART =====
// Macros are NOT static config — they can change as the user edits FluidNC's
// preferences. Loaded on macros-screen entry, with a Refresh button to re-fetch.
void requestMacros() {
    pendantMacros.loading  = true;
    pendantMacros.count    = 0;
    pendantMacros.selected = -1;
    request_macros();  // FileParser.h — sends $File/SendJSON=/macrocfg.json, falls back to preferences.json
}

// ===== PendantScene: bridges FluidNC callbacks → pendantMachine (Core 0) =====
class PendantScene : public Scene {
public:
    PendantScene() : Scene("Pendant") {}

    void onDROChange() override {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            pendantMachine.numAxes       = n_axes;
            // posX/Y/Z/A = work pos (DRO values, used by all position displays)
            pendantMachine.posX          = myAxes[0] / 10000.0f;
            pendantMachine.posY          = (n_axes > 1) ? myAxes[1] / 10000.0f : 0.0f;
            pendantMachine.posZ          = (n_axes > 2) ? myAxes[2] / 10000.0f : 0.0f;
            pendantMachine.posA          = (n_axes > 3) ? myAxes[3] / 10000.0f : 0.0f;
            // workX/Y/Z/A = machine pos (absolute, used by Work Area screen)
            pendantMachine.workX         = myMachineAxes[0] / 10000.0f;
            pendantMachine.workY         = (n_axes > 1) ? myMachineAxes[1] / 10000.0f : 0.0f;
            pendantMachine.workZ         = (n_axes > 2) ? myMachineAxes[2] / 10000.0f : 0.0f;
            pendantMachine.workA         = (n_axes > 3) ? myMachineAxes[3] / 10000.0f : 0.0f;
            pendantMachine.feedRate      = (int)myFeed;
            pendantMachine.spindleRPM    = (int)mySpeed;
            pendantMachine.feedOverride  = (int)myFro;
            pendantMachine.spindleOverride = (int)mySro;
            pendantMachine.currentFile = (myFile && *myFile) ? myFile : "";
            pendantMachine.jobPercent  = (int)myPercent;
            xSemaphoreGive(stateMutex);
        }
        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }

    void onStateChange(state_t /*newState*/) override {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            pendantMachine.status           = my_state_string;
            pendantMachine.connectionStatus = "Connected";
            pendantMachine.inInches         = inInches;
            if (wifi_ip.length())   pendantMachine.ipAddress = String(wifi_ip.c_str());
            if (wifi_ssid.length()) pendantMachine.wifiSSID  = String(wifi_ssid.c_str());
            xSemaphoreGive(stateMutex);
        }
        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }

    void onFilesList() override {
        // Called from Core 0 when JSON/file-list parsing completes.
        // Routes to macros or SD card depending on which screen requested data.
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (currentPendantScreen == PSCREEN_MACROS) {
                // Populate from the 'macros' vector filled by FileParser listeners
                pendantMacros.count      = 0;
                pendantMacros.loading    = false;
                pendantMacros.cacheValid = true;  // mark cache warm for re-entry
                for (auto* m : macros) {
                    if (pendantMacros.count >= 20) break;
                    if (m->name.empty()) continue;
                    pendantMacros.content[pendantMacros.count]  = String(m->name.c_str());
                    pendantMacros.filename[pendantMacros.count] = String(m->filename.c_str());
                    pendantMacros.count++;
                }
            } else {
                // SD card file list from $Files/ListGCode
                pendantSdCard.fileCount    = 0;
                pendantSdCard.scrollOffset = 0;
                pendantSdCard.loading      = false;
                for (auto& fi : fileVector) {
                    if (!fi.isDir() && pendantSdCard.fileCount < 20) {
                        pendantSdCard.files[pendantSdCard.fileCount++] = String(fi.fileName.c_str());
                    }
                }
            }
            xSemaphoreGive(stateMutex);
        }
        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }

    void onError(const char* /*errstr*/) override {
        // Called when neither preferences.json nor macrocfg.json contain macros
        if (currentPendantScreen == PSCREEN_MACROS) {
            pendantMacros.loading = false;
            pendantMacros.count   = 0;
            if (hwEventQueue) {
                HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
                xQueueSend(hwEventQueue, &ev, 0);
            }
        }
    }

    void reDisplay() override {
        // Copy any newly-received config values into pendantMachine / pendantJog
        if (spindleMaxItem.known() || spindleMinItem.known()) {
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (spindleMaxItem.known()) pendantMachine.spindleMaxRPM = spindleMaxItem.get();
                if (spindleMinItem.known()) pendantMachine.spindleMinRPM = spindleMinItem.get();
                xSemaphoreGive(stateMutex);
            }
        }
        if (jogMaxRateItem.known()) {
            int rate = jogMaxRateItem.get();
            if (rate > 0) pendantJog.maxFeedRate = rate;
        }
        // $130-$133: per-axis max travel (mm), used as a per-tick distance clamp
        if (jogMaxTravelX.known()) { int v = jogMaxTravelX.get(); if (v > 0) pendantJog.maxTravel[0] = v; }
        if (jogMaxTravelY.known()) { int v = jogMaxTravelY.get(); if (v > 0) pendantJog.maxTravel[1] = v; }
        if (jogMaxTravelZ.known()) { int v = jogMaxTravelZ.get(); if (v > 0) pendantJog.maxTravel[2] = v; }
        if (jogMaxTravelA.known()) { int v = jogMaxTravelA.get(); if (v > 0) pendantJog.maxTravel[3] = v; }

        // Macro list is populated in onFilesList() when $File/SendJSON response arrives
        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }
};

static PendantScene pendantScene;

// Called by GrblParser when a [VER:] report arrives from FluidNC
extern "C" void show_versions(const char* /*grbl_version*/, const char* fluidnc_version) {
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        pendantMachine.fluidNCVersion = fluidnc_version;
        xSemaphoreGive(stateMutex);
    }
}

// ===== Core 0 Hardware Task =====
void pendant_hw_task(void* /*pvParameters*/) {
    dbg_println("PendantHW task started on Core 0");

    int16_t       lastEncCount   = get_encoder();
    unsigned long lastDebounce[3] = { 0, 0, 0 };
    bool          lastBtnRaw[3]  = { true, true, true };
    bool          btnState[3]    = { true, true, true };
    bool          btnHandled[3]  = { false, false, false };
    int           btnPins[3]     = { red_button_pin, dial_button_pin, green_button_pin };

    // Non-blocking Red button post-reset: send $X 500ms after Reset without blocking the task
    bool          redResetPending = false;
    unsigned long redResetMs      = 0;

    // Send first $? immediately — fnc_is_connected() uses a 'starting' flag so
    // the very first call fires the ping right away instead of waiting for the timer.
    fnc_is_connected();
    unsigned long lastPingMs = millis();

    for (;;) {
        // Drain ALL available UART bytes in one task cycle.
        // The original fnc_poll() reads exactly 1 byte per call, so at 2ms/cycle
        // the old single call gave only ~500 B/s — enough for normal status reports
        // but far too slow for large JSON files (e.g. preferences.json can be 10+ KB,
        // taking 20+ seconds to receive at 500 B/s).
        // collect() and fnc_getchar() are both exported from GrblParserC.h.
        // poll_extra() (debug serial forwarding) is called once after the drain.
        {
            int c;
            while ((c = fnc_getchar()) >= 0) {
                collect((uint8_t)c);
            }
            poll_extra();
        }

        // Drive ping + connection state from Core 0.
        // Ping interval is adaptive: slow down to 1000ms while the machine is Running
        // so the $? realtime byte doesn't add UART load during active motion.
        // When idle/stopped/alarm, keep 200ms for snappy connection detection.
        unsigned long nowMs    = millis();
        bool          running  = pendantMachine.status.startsWith("Run");
        unsigned long pingInterval = running ? 1000UL : 200UL;
        if (nowMs - lastPingMs >= pingInterval) {
            bool connected = fnc_is_connected();
            if (connected != pendantConnected) {
                pendantConnected = connected;
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    pendantMachine.connectionStatus = connected ? "Connected" : "N/C";
                    xSemaphoreGive(stateMutex);
                }
                // On connect edge, ask Core 1 to fetch all static controller config.
                // Reconnects auto-refresh because the edge fires again on each transition.
                if (connected && hwEventQueue) {
                    HwEvent ev = { HwEvent::CONNECTED, 0 };
                    xQueueSend(hwEventQueue, &ev, 0);
                }
            }
            lastPingMs = nowMs;
        }

        // Encoder delta via PCNT. The encoder is wired in 4x quadrature mode so
        // one physical detent (click) = 4 raw counts. Accumulate counts until a
        // full detent is reached before sending an event, so slow turns that spread
        // counts across multiple 5ms samples still produce the correct step count.
        {
            static int32_t encAccumulator = 0;
            int16_t encCount = get_encoder();
            int16_t rawDelta = encCount - lastEncCount;
            if (rawDelta != 0) {
                lastEncCount = encCount;
                encAccumulator += rawDelta;
                int32_t steps = encAccumulator / 4;
                if (steps != 0) {
                    encAccumulator -= steps * 4;  // keep sub-detent remainder
                    if (hwEventQueue) {
                        HwEvent ev = { HwEvent::ENCODER_DELTA, steps };
                        xQueueSend(hwEventQueue, &ev, 0);
                    }
                }
            }
        }

        // Button debounce and UART commands
        unsigned long now = millis();
        for (int i = 0; i < 3; i++) {
            if (btnPins[i] < 0) continue;

            bool raw = (digitalRead(btnPins[i]) == HIGH);  // HIGH = not pressed (INPUT_PULLUP)
            if (raw != lastBtnRaw[i]) {
                lastDebounce[i] = now;
            }
            lastBtnRaw[i] = raw;

            if ((now - lastDebounce[i]) > 30) {
                bool pressed = !raw;
                if (pressed && btnState[i] && !btnHandled[i]) {
                    btnHandled[i] = true;
                    switch (i) {
                        case 0:  // Red → soft reset then $X after 500ms (non-blocking)
                            fnc_realtime(Reset);
                            redResetPending = true;
                            redResetMs      = now;
                            break;
                        case 1:  // Yellow → pause motion
                            fnc_realtime(FeedHold);
                            break;
                        case 2:  // Green → start / resume
                            fnc_realtime(CycleStart);
                            break;
                    }
                    if (hwEventQueue) {
                        static const HwEvent::Type types[] = {
                            HwEvent::BUTTON_RED, HwEvent::BUTTON_YELLOW, HwEvent::BUTTON_GREEN
                        };
                        HwEvent ev = { types[i], 0 };
                        xQueueSend(hwEventQueue, &ev, 0);
                    }
                }
                if (!pressed) btnHandled[i] = false;
                btnState[i] = !pressed;
            }
        }

        // Non-blocking post-reset $X — sent 500ms after Red button without stalling the task
        if (redResetPending && (millis() - redResetMs >= 500)) {
            send_line("$X");
            redResetPending = false;
        }

        vTaskDelay(pdMS_TO_TICKS(2));  // 2ms → ~500Hz polling for snappy button response
    }
}

extern const char* git_info;  // version.cpp

// ===== Public Interface =====
void setup_pendant() {
    // Strip build suffix — keep only "v<digits>.<digits>" (e.g. "v0.1main-abc-dirty" → "v0.1")
    {
        const char* src = git_info;
        char buf[16];
        int  j = 0;
        if (*src == 'v') buf[j++] = *src++;
        while (*src && (isdigit((unsigned char)*src) || *src == '.') && j < 15)
            buf[j++] = *src++;
        buf[j] = '\0';
        pendantMachine.fluidDialVersion = buf;
    }

    // Load saved display rotation and jog preferences
    preferences.begin("pendant", false);
    int  savedRotation = preferences.getInt("rotation",    2);
    bool savedFineInc  = preferences.getBool("jogFineMode", true);
    int  savedSelInc   = preferences.getInt("jogSelInc",    1);
    preferences.end();

    pendantJog.fineIncrements    = savedFineInc;
    // Clamp saved index to a valid increment slot — guards against a corrupted
    // NVS entry indexing past the 4-element increment table.
    pendantJog.selectedIncrement = constrain(savedSelInc, 0, 3);

    pendantMachine.rotation        = savedRotation;
    pendantMachine.displayRotation = (savedRotation == 2) ? "Normal" : "Upside Down";
    display.setRotation(savedRotation);

    dbg_printf("Pendant rotation: %s (%d)\n",
               pendantMachine.displayRotation.c_str(), savedRotation);

    // Force Disconnected state so the first real status report always triggers
    // the connection setup ($RI=200, $G, $I) — even if FluidNC is already running.
    set_disconnected_state();

    // Register scene so FluidNC callbacks update pendantMachine
    activate_scene(&pendantScene);

    // Enter initial screen (allocates sprites)
    callScreenEnter(currentPendantScreen);
    drawCurrentPendantScreen();

    dbg_println("CNC Pendant UI ready (Core 1)");
}

void loop_pendant() {
    // Execute any action deferred by schedule_action() in FileParser / Scene code.
    // In the original FluidDial this runs inside dispatch_events(); we replicate
    // just that one step here so macro file requests (and their fallbacks) fire.
    extern ActionHandler action;  // Scene.cpp
    if (action) {
        ActionHandler a = action;
        action          = nullptr;
        a();
    }

    // Periodic sprite-refresh timestamp. Declared here so the STATE_UPDATE
    // queue handler can reset it after a queue-driven sprite refresh — that
    // coalesces the periodic 100 ms tick with the event-driven update so we
    // don't redraw twice in quick succession when DRO updates arrive.
    static unsigned long lastSpriteUpdate = 0;

    // Process hardware events from Core 0
    HwEvent ev;
    while (xQueueReceive(hwEventQueue, &ev, 0) == pdTRUE) {
        switch (ev.type) {
            case HwEvent::ENCODER_DELTA:
                handleEncoderDelta(ev.value);
                break;
            case HwEvent::BUTTON_RED:
                break;
            case HwEvent::BUTTON_YELLOW:
                break;
            case HwEvent::BUTTON_GREEN:
                // If a file has been loaded via the SD card Load button, run it now
                if (pendantSdCard.loadedFile.length() > 0 && pendantConnected) {
                    String cmd = "$SD/Run=" + pendantSdCard.loadedFile;
                    send_line(cmd.c_str());
                    pendantSdCard.loadedFile = "";
                    navigateTo(PSCREEN_STATUS);
                }
                break;
            case HwEvent::STATE_UPDATE:
                // Use the sprite-only update path to avoid fillScreen flicker.
                // Full drawXxxScreen() is only called on initial entry or user touch.
                updateCurrentScreenSprites();
                lastSpriteUpdate = millis();   // suppress duplicate periodic tick
                break;
            case HwEvent::CONNECTED:
                // Connection edge: snapshot all static controller config so screens
                // never have to round-trip the UART on entry.
                requestControllerConfig();
                break;
        }
    }

    // Periodic sprite refresh (100ms) — only fires if STATE_UPDATE didn't already redraw
    if (millis() - lastSpriteUpdate >= 100) {
        updateCurrentScreenSprites();
        lastSpriteUpdate = millis();
    }

    // Touch input (200ms debounce)
    lgfx::touch_point_t tp;
    if (display.getTouch(&tp)) {
        static unsigned long lastTouch = 0;
        if (millis() - lastTouch > 200) {
            handlePendantTouch(tp.x, tp.y);
            lastTouch = millis();
        }
    }
}
