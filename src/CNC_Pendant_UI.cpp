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
volatile bool    pendantConnected  = false;
static uint32_t  lastFluidNCDataMs = 0;

// ===== Screen State =====
PendantScreen currentPendantScreen  = PSCREEN_MAIN_MENU;
PendantScreen previousPendantScreen = PSCREEN_MAIN_MENU;

// ===== Machine & UI State Variables =====
MachineState  pendantMachine;
JogState      pendantJog;
SDCardState   pendantSdCard;
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

static void navigateTo(PendantScreen next) {
    if (next == currentPendantScreen) return;
    callScreenExit(currentPendantScreen);
    previousPendantScreen = currentPendantScreen;
    currentPendantScreen  = next;
    callScreenEnter(next);
    drawCurrentPendantScreen();
}

// ===== Touch Dispatch (Core 1) =====
static void handlePendantTouch(int x, int y) {
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
    }
}

// ===== Encoder Delta Handler (Core 1) =====
static void handleEncoderDelta(int32_t delta) {
    if (currentPendantScreen == PSCREEN_JOG_HOMING) {
        if (!pendantConnected) return;
        String axisNames[] = { "X", "Y", "Z", "A" };
        float  distance    = (float)delta * pendantJog.increment;
        char   cmd[64];
        snprintf(cmd, sizeof(cmd), "$J=G91 %s%.3f F1000",
                 axisNames[pendantJog.selectedAxis].c_str(), distance);
        send_line(cmd);
    } else if (currentPendantScreen == PSCREEN_FLUIDNC) {
        static unsigned long lastRotationMs = 0;
        if (millis() - lastRotationMs > 300) {
            pendantMachine.rotation        = (pendantMachine.rotation == 2) ? 0 : 2;
            pendantMachine.displayRotation = (pendantMachine.rotation == 2) ? "Normal" : "Upside Down";
            display.setRotation(pendantMachine.rotation);
            preferences.begin("pendant", false);
            preferences.putInt("rotation", pendantMachine.rotation);
            preferences.end();
            drawCurrentPendantScreen();
            lastRotationMs = millis();
        }
    }
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
        default:
            break;
    }
}

// ===== PendantScene: bridges FluidNC callbacks → pendantMachine (Core 0) =====
class PendantScene : public Scene {
public:
    PendantScene() : Scene("Pendant") {}

    void onDROChange() override {
        pendantConnected  = true;
        lastFluidNCDataMs = millis();
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            pendantMachine.numAxes       = n_axes;
            pendantMachine.posX          = myAxes[0] / 10000.0f;
            pendantMachine.posY          = (n_axes > 1) ? myAxes[1] / 10000.0f : 0.0f;
            pendantMachine.posZ          = (n_axes > 2) ? myAxes[2] / 10000.0f : 0.0f;
            pendantMachine.posA          = (n_axes > 3) ? myAxes[3] / 10000.0f : 0.0f;
            pendantMachine.feedRate      = (int)myFeed;
            pendantMachine.spindleRPM    = (int)mySpeed;
            pendantMachine.feedOverride  = (int)myFro;
            pendantMachine.spindleOverride = (int)mySro;
            if (myFile) pendantMachine.currentFile = myFile;
            xSemaphoreGive(stateMutex);
        }
        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }

    void onStateChange(state_t /*newState*/) override {
        pendantConnected  = true;
        lastFluidNCDataMs = millis();
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            pendantMachine.status           = my_state_string;
            pendantMachine.connectionStatus = fnc_is_connected() ? "Connected" : "N/C";
            xSemaphoreGive(stateMutex);
        }
        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }

    void reDisplay() override {
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

    // Send first $? immediately — fnc_is_connected() uses a 'starting' flag so
    // the very first call fires the ping right away instead of waiting for the timer.
    fnc_is_connected();
    unsigned long lastPingMs = millis();

    for (;;) {
        // Poll FluidNC UART — calls PendantScene callbacks when data arrives
        fnc_poll();

        // Drive the ping mechanism (sends $? to FluidNC every ~4s to keep connection alive).
        // Must stay on Core 0 with UART — fnc_is_connected() writes to UART which blocks Core 1.
        unsigned long nowMs = millis();
        if (nowMs - lastPingMs >= 200) {
            fnc_is_connected();
            lastPingMs = nowMs;
        }

        // Encoder delta via PCNT (hardware, no polling/debounce needed)
        int16_t encCount = get_encoder();
        int16_t delta    = encCount - lastEncCount;
        if (delta != 0) {
            lastEncCount = encCount;
            if (hwEventQueue) {
                HwEvent ev = { HwEvent::ENCODER_DELTA, (int32_t)delta };
                xQueueSend(hwEventQueue, &ev, 0);
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

            if ((now - lastDebounce[i]) > 50) {
                bool pressed = !raw;
                if (pressed && btnState[i] && !btnHandled[i]) {
                    btnHandled[i] = true;
                    switch (i) {
                        case 0: fnc_realtime(Reset);      break;  // Red    → E-Stop (Ctrl-X)
                        case 1:                                                      // Yellow → context
                            if (state == Alarm && fnc_is_connected()) send_line("$X"); //   Alarm  → Clear Alarm
                            else if (state != Alarm)                  fnc_realtime(FeedHold); //   Other → Pause/Hold
                            break;
                        case 2: fnc_realtime(CycleStart); break;  // Green  → Cycle Start (~)
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

        vTaskDelay(pdMS_TO_TICKS(5));  // 5ms → ~200Hz polling
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

    // Load saved display rotation
    preferences.begin("pendant", false);
    int savedRotation = preferences.getInt("rotation", 2);
    preferences.end();

    pendantMachine.rotation        = savedRotation;
    pendantMachine.displayRotation = (savedRotation == 2) ? "Normal" : "Upside Down";
    display.setRotation(savedRotation);

    dbg_printf("Pendant rotation: %s (%d)\n",
               pendantMachine.displayRotation.c_str(), savedRotation);

    // Register scene so FluidNC callbacks update pendantMachine
    activate_scene(&pendantScene);

    // Enter initial screen (allocates sprites)
    callScreenEnter(currentPendantScreen);
    drawCurrentPendantScreen();

    dbg_println("CNC Pendant UI ready (Core 1)");
}

void loop_pendant() {
    // Process hardware events from Core 0
    HwEvent ev;
    while (xQueueReceive(hwEventQueue, &ev, 0) == pdTRUE) {
        switch (ev.type) {
            case HwEvent::ENCODER_DELTA:
                handleEncoderDelta(ev.value);
                break;
            case HwEvent::BUTTON_RED:
            case HwEvent::BUTTON_YELLOW:
            case HwEvent::BUTTON_GREEN:
                break;
            case HwEvent::STATE_UPDATE:
                if (currentPendantScreen == PSCREEN_FLUIDNC) {
                    updateFluidNCDisplay();
                }
                break;
        }
    }

    // Connection timeout — clear flag if no FluidNC data for 3 seconds
    if (pendantConnected && (millis() - lastFluidNCDataMs) > 3000) {
        pendantConnected = false;
    }

    // Periodic sprite refresh (100ms)
    static unsigned long lastSpriteUpdate = 0;
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
