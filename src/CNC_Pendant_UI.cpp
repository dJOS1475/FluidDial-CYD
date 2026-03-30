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

void navigateTo(PendantScreen next) {
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
    if (currentPendantScreen == PSCREEN_SPINDLE_CONTROL && pendantSpindle.dialMode) {
        int maxRPM = pendantMachine.spindleMaxRPM > 0 ? pendantMachine.spindleMaxRPM : 24000;
        int minRPM = pendantMachine.spindleMinRPM;
        pendantMachine.spindleRPM = constrain(pendantMachine.spindleRPM + delta * 1000, minRPM, maxRPM);
        updateSpindleRPMDisplay();
        return;
    } else if (currentPendantScreen == PSCREEN_JOG_HOMING) {
        if (!pendantConnected) return;
        // delta is already in whole detents (converted in pendant_hw_task)
        String axisNames[] = { "X", "Y", "Z", "A" };
        float  distance    = (float)delta * pendantJog.increment;
        char   cmd[64];
        if (pendantMachine.inInches) {
            snprintf(cmd, sizeof(cmd), "$J=G91 G20 %s%.4f F100",
                     axisNames[pendantJog.selectedAxis].c_str(), distance);
        } else {
            snprintf(cmd, sizeof(cmd), "$J=G91 G21 %s%.3f F1000",
                     axisNames[pendantJog.selectedAxis].c_str(), distance);
        }
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

// ===== Spindle config items — request $30 (max RPM) and $31 (min RPM) from FluidNC =====
static IntConfigItem spindleMaxItem("$30");
static IntConfigItem spindleMinItem("$31");

// Called from enterSpindleControl() on Core 1 — sends $30/$31 queries to FluidNC
void requestSpindleConfig() {
    spindleMaxItem.init();
    spindleMinItem.init();
}

// ===== PendantScene: bridges FluidNC callbacks → pendantMachine (Core 0) =====
class PendantScene : public Scene {
public:
    PendantScene() : Scene("Pendant") {}

    void onDROChange() override {
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
        // Called from Core 0 (fnc_poll) when FluidNC responds to $Files/ListGCode
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            pendantSdCard.fileCount    = 0;
            pendantSdCard.scrollOffset = 0;
            pendantSdCard.loading      = false;
            for (auto& fi : fileVector) {
                if (!fi.isDir() && pendantSdCard.fileCount < 20) {
                    pendantSdCard.files[pendantSdCard.fileCount++] = String(fi.fileName.c_str());
                }
            }
            xSemaphoreGive(stateMutex);
        }
        if (hwEventQueue) {
            HwEvent ev = { HwEvent::STATE_UPDATE, 0 };
            xQueueSend(hwEventQueue, &ev, 0);
        }
    }

    void reDisplay() override {
        // Copy any newly-received config values into pendantMachine
        if (spindleMaxItem.known() || spindleMinItem.known()) {
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (spindleMaxItem.known()) pendantMachine.spindleMaxRPM = spindleMaxItem.get();
                if (spindleMinItem.known()) pendantMachine.spindleMinRPM = spindleMinItem.get();
                xSemaphoreGive(stateMutex);
            }
        }
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

        // Drive ping + connection state from Core 0.
        // fnc_is_connected() is backed by update_rx_time() (called per UART byte) so it
        // reliably reflects whether FluidNC is actually responding.
        unsigned long nowMs = millis();
        if (nowMs - lastPingMs >= 200) {
            bool connected = fnc_is_connected();
            if (connected != pendantConnected) {
                pendantConnected = connected;
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    pendantMachine.connectionStatus = connected ? "Connected" : "N/C";
                    xSemaphoreGive(stateMutex);
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

            if ((now - lastDebounce[i]) > 100) {
                bool pressed = !raw;
                if (pressed && btnState[i] && !btnHandled[i]) {
                    btnHandled[i] = true;
                    switch (i) {
                        case 0:  // Red → soft reset (cancels program, clears buffer, stops spindle)
                            //        $X clears the post-reset alarm so no rehoming is needed.
                            //        Position is retained; Green cannot resume after this.
                            fnc_realtime(Reset);
                            vTaskDelay(pdMS_TO_TICKS(500));  // wait for controller to finish reset
                            send_line("$X");
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
                } else if (currentPendantScreen == PSCREEN_SD_CARD) {
                    drawSDCardScreen();
                }
                break;
        }
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
