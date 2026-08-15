#include "pendant_shared.h"
#include "screen_espnow.h"
#include "screen_probe.h"          // PROBE_* palette + confirm overlay style
#include "../Comms.h"

#ifdef USE_ESPNOW
#include "../PeerLink.h"

// ── ESP-NOW pairing wizard (ESPNOW_SPEC.md §6b) ──────────────────────────────
//
// The pairing ACTION happens on the controller ($espnow/pair), not here — which
// is unusual enough that the screen leads with the instructions rather than
// burying them.  The pendant only advertises and waits.
//
// State is derived from PeerLink rather than tracked locally, so the screen can
// never disagree with the link.

static const int STEP_Y = 40,  STEP_H = 92;
static const int STAT_Y = 138, STAT_H = 86;
static const int ACT_Y  = 236, ACT_H  = 42;
static const int NAV_Y  = 280, NAV_H  = 38;

// Pairing window is opened on entry and closed on exit so the radio is never
// left advertising after the user walks away from the screen.
//
// A window that never ends is worse than one that fails: on hardware the screen
// sat in "Waiting..." indefinitely with no automatic way out.  Give it a bound
// and surface a Retry/Cancel state when it expires.
static const unsigned long PAIR_TIMEOUT_MS = 90000;   // 90 s to go run $espnow/pair
static unsigned long pairStartedMs = 0;

void enterEspNowPair() {
    releasePanelSprites();
    pairStartedMs = millis();
    espnow_start_pairing();
}

void exitEspNowPair() {
    // Only cancel if it did not succeed — cancelling a completed pair would
    // throw away the profile we just stored.
    if (!espnow_pairing_complete()) espnow_cancel_pairing();
    releasePanelSprites();
}

enum PairView { PV_WAIT, PV_WORKING, PV_OK, PV_FAIL, PV_NORADIO };

static PairView pairView() {
    // If the radio never came up there is nothing to listen with, so say so
    // instead of counting down 90 s of "Waiting..." that cannot succeed.
    if (!espnow_radio_ready())      return PV_NORADIO;
    if (espnow_pairing_complete())  return PV_OK;
    if (espnow_is_reconnecting())   return PV_WORKING;
    // Only the idle wait times out — a handshake already in progress is left to
    // finish rather than being cut off part-way.
    if (pairStartedMs && (millis() - pairStartedMs) >= PAIR_TIMEOUT_MS) return PV_FAIL;
    return PV_WAIT;
}

static void drawSteps(PairView v) {
    display.fillRoundRect(5, STEP_Y, 230, STEP_H, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, STEP_Y + 3);
    display.print("ON THE CONTROLLER");

    struct { const char* text; bool done; } steps[3] = {
        { "Open FluidNC console", v != PV_WAIT },
        { "Run:  $espnow/pair",   v == PV_OK || v == PV_WORKING },
        { "Keep pendant nearby",  v == PV_OK },
    };
    for (int i = 0; i < 3; i++) {
        const int y = STEP_Y + 18 + i * 18;
        display.fillCircle(18, y + 4, 7,
                           steps[i].done ? PROBE_C_GREEN : COLOR_BUTTON_GRAY);
        display.setTextSize(1);
        display.setTextColor(COLOR_WHITE);
        display.setCursor(15, y + 1);
        display.print(steps[i].done ? "*" : String(i + 1));
        display.setTextColor(steps[i].done ? COLOR_GRAY_TEXT : COLOR_WHITE);
        display.setCursor(32, y + 1);
        display.print(steps[i].text);
    }
    display.setTextColor(PROBE_C_DIMBLUE);
    display.setCursor(32, STEP_Y + 72);
    display.print("or WebUI > Settings > ESP-NOW");
}

static void drawPairStatus(PairView v) {
    display.fillRoundRect(5, STAT_Y, 230, STAT_H, 4, PROBE_BG_PANEL);
    display.setTextSize(1);
    display.setTextColor(PROBE_C_LBLUE);
    display.setCursor(10, STAT_Y + 3);
    display.print("STATUS");

    const char* head;
    uint16_t    col;
    const char* sub;
    switch (v) {
        case PV_OK:      head = "Paired";     col = PROBE_C_GREEN;  sub = nullptr; break;
        case PV_WORKING: head = "Pairing";    col = PROBE_C_BLUE;   sub = "Exchanging keys (P-256)"; break;
        case PV_FAIL:    head = "Timed out";  col = PROBE_C_RED;    sub = "No controller responded"; break;
        case PV_NORADIO: head = "Radio down"; col = PROBE_C_RED;    sub = "ESP-NOW failed to start"; break;
        default:         head = "Waiting...";  col = PROBE_C_YELLOW; sub = "Listening for controller"; break;
    }
    display.setTextSize(3);
    display.setTextColor(col);
    display.setCursor(14, STAT_Y + 20);
    display.print(head);

    display.setTextSize(1);
    display.setTextColor(COLOR_GRAY_TEXT);
    if (v == PV_OK) {
        ESPNowProfileInfo info;
        const int idx = espnow_active_profile_index();
        if (idx >= 0 && espnow_get_profile((size_t)idx, info)) {
            display.setCursor(14, STAT_Y + 54);
            display.print(info.hostname[0] ? info.hostname : "(unnamed)");
            char buf[40];
            snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X  ch%u",
                     info.mac[0], info.mac[1], info.mac[2],
                     info.mac[3], info.mac[4], info.mac[5], (unsigned)info.channel);
            display.setCursor(14, STAT_Y + 68);
            display.print(buf);
        }
    } else if (sub) {
        display.setCursor(14, STAT_Y + 54);
        display.print(sub);
        if (v == PV_FAIL) {
            display.setCursor(14, STAT_Y + 68);
            display.print("Check FluidNC is v4.0.4+");
        }
    }
}

static void drawPairActions(PairView v) {
    display.fillRect(5, ACT_Y, 230, ACT_H, COLOR_BACKGROUND);
    if (v == PV_OK) {
        drawButton(5, ACT_Y, 230, ACT_H, "Use This Machine", PROBE_BTN_GREEN, COLOR_WHITE, 2);
        drawButton(5, NAV_Y, 230, NAV_H, "Machines", COLOR_INDIGO, COLOR_WHITE, 2);
    } else if (v == PV_FAIL) {
        drawButton(rowBtnX(2, 0), ACT_Y, rowBtnWAt(2, 0), ACT_H, "Retry",  PROBE_BTN_GREEN,   COLOR_WHITE, 2);
        drawButton(rowBtnX(2, 1), ACT_Y, rowBtnWAt(2, 1), ACT_H, "Cancel", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
        drawButton(5, NAV_Y, 230, NAV_H, "Back", COLOR_BLUE, COLOR_WHITE, 2);
    } else {
        drawButton(5, ACT_Y, 230, ACT_H, "Cancel", COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
        drawButton(5, NAV_Y, 230, NAV_H, "Back", COLOR_BLUE, COLOR_WHITE, 2);
    }
}

void drawEspNowPairScreen() {
    const PairView v = pairView();
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("ESP-NOW PAIRING");
    drawSteps(v);
    drawPairStatus(v);
    drawPairActions(v);
}

void updateEspNowPairDisplay() {
    if (currentPendantScreen != PSCREEN_ESPNOW_PAIR) return;
    // Redraw the changing regions only — never fillScreen on a periodic tick.
    static PairView last = PV_WAIT;
    const PairView  v    = pairView();
    drawPairStatus(v);
    if (v != last) {          // step ticks and buttons only change on transition
        drawSteps(v);
        drawPairActions(v);
        last = v;
    }
}

void handleEspNowPairTouch(int x, int y) {
    const PairView v = pairView();

    if (v == PV_OK) {
        if (isTouchInBounds(x, y, 5, ACT_Y, 230, ACT_H)) {   // Use This Machine
            currentPendantScreen = PSCREEN_CONNECTION;
            return;
        }
        if (isTouchInBounds(x, y, 5, NAV_Y, 230, NAV_H)) {
            currentPendantScreen = PSCREEN_ESPNOW_MACHINES;
            return;
        }
        return;
    }

    if (v == PV_FAIL) {
        if (isTouchInBounds(x, y, rowBtnX(2, 0), ACT_Y, rowBtnWAt(2, 0), ACT_H)) {
            pairStartedMs = millis();        // Retry — restart the window
            espnow_start_pairing();
            drawEspNowPairScreen();
            return;
        }
        if (isTouchInBounds(x, y, rowBtnX(2, 1), ACT_Y, rowBtnWAt(2, 1), ACT_H)) {
            espnow_cancel_pairing();
            currentPendantScreen = PSCREEN_CONNECTION;
            return;
        }
    } else if (isTouchInBounds(x, y, 5, ACT_Y, 230, ACT_H)) {   // Cancel
        espnow_cancel_pairing();
        currentPendantScreen = PSCREEN_CONNECTION;
        return;
    }

    if (isTouchInBounds(x, y, 5, NAV_Y, 230, NAV_H)) {
        currentPendantScreen = PSCREEN_CONNECTION;
    }
}

// ── Paired-machine list (ESPNOW_SPEC.md §6c) ─────────────────────────────────

static const int ROW0_Y   = 44;
static const int MROW_H   = 52;
static const int MROW_GAP = 4;
static const int MACT_Y   = 232, MACT_H = 42;

static int  selectedRow   = -1;    // row targeted by Forget
static bool confirmForget = false;

void enterEspNowMachines() {
    releasePanelSprites();
    selectedRow   = espnow_active_profile_index();
    confirmForget = false;
}

void exitEspNowMachines() {
    releasePanelSprites();
}

static int machineRowY(int i) { return ROW0_Y + i * (MROW_H + MROW_GAP); }

static void drawMachineRow(int i) {
    ESPNowProfileInfo info;
    if (!espnow_get_profile((size_t)i, info)) return;

    const int  y      = machineRowY(i);
    const bool active = (espnow_active_profile_index() == i);
    const bool picked = (selectedRow == i);

    display.fillRoundRect(5, y, 230, MROW_H, 4,
                          active ? PROBE_SEL_BG : PROBE_BG_PANEL);
    display.drawRoundRect(5, y, 230, MROW_H, 4,
                          active ? PROBE_C_YELLOW
                                 : (picked ? COLOR_ORANGE : PROBE_C_TAPBDR));

    display.setTextSize(2);
    display.setTextColor(active ? PROBE_C_YELLOW : COLOR_WHITE);
    display.setCursor(12, y + 7);
    display.print(info.hostname[0] ? info.hostname : "(unnamed)");

    char buf[40];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             info.mac[0], info.mac[1], info.mac[2],
             info.mac[3], info.mac[4], info.mac[5]);
    display.setTextSize(1);
    display.setTextColor(COLOR_GRAY_TEXT);
    display.setCursor(12, y + 28);
    display.print(buf);
    display.setCursor(12, y + 39);
    display.printf("ch%u", (unsigned)info.channel);

    if (active) {
        display.setTextColor(PROBE_C_GREEN);
        display.setCursor(44, y + 39);
        display.print("ACTIVE");
    }

    // Signal is only meaningful for the machine we are actually linked to;
    // the others show as unknown rather than implying they are unreachable.
    const bool live = active && espnow_is_connected();
    const int  bars = live ? espnow_signal_bars() : 0;
    for (int b = 0; b < 4; b++) {
        const int h  = 4 + b * 4;
        const int bx = 196 + b * 9;
        const int by = y + 26 - h;
        display.fillRoundRect(bx, by, 6, h, 1,
                              b < bars ? PROBE_C_GREEN : PROBE_BG_SCREEN);
    }
    display.setTextSize(1);
    display.setTextColor(live ? COLOR_GRAY_TEXT : PROBE_C_DIMBLUE);
    display.setCursor(196, y + 30);
    display.print(live ? "ok" : "--");
}

void drawEspNowMachinesScreen() {
    display.fillScreen(COLOR_BACKGROUND);
    drawTitle("MACHINES");

    const size_t n = espnow_profile_count();
    for (size_t i = 0; i < n; i++) drawMachineRow((int)i);

    if (n == 0) {
        display.fillRoundRect(5, ROW0_Y, 230, MROW_H, 4, PROBE_BG_PANEL);
        display.setTextSize(1);
        display.setTextColor(COLOR_GRAY_TEXT);
        display.setCursor(14, ROW0_Y + 22);
        display.print("No machines paired yet");
    }

    display.setTextSize(1);
    display.setTextColor(PROBE_C_DIMBLUE);
    display.setCursor(8, 216);
    display.printf("Tap a machine to use it  -  %u/5 paired", (unsigned)n);

    const bool full = (n >= 5);
    drawButton(rowBtnX(2, 0), MACT_Y, rowBtnWAt(2, 0), MACT_H,
               full ? "Full" : "Pair New",
               full ? COLOR_BUTTON_GRAY : PROBE_BTN_TEAL, COLOR_WHITE, 2);
    drawButton(rowBtnX(2, 1), MACT_Y, rowBtnWAt(2, 1), MACT_H, "Forget",
               COLOR_BUTTON_GRAY, COLOR_WHITE, 2);
    drawButton(5, NAV_Y, 230, NAV_H, "Back", COLOR_BLUE, COLOR_WHITE, 2);

    if (confirmForget) probeDrawConfirmOverlay("FORGET MACHINE");
}

void updateEspNowMachinesDisplay() {
    if (currentPendantScreen != PSCREEN_ESPNOW_MACHINES) return;
    if (confirmForget) return;            // never repaint under an open dialog
    const size_t n = espnow_profile_count();
    for (size_t i = 0; i < n; i++) drawMachineRow((int)i);
}

void handleEspNowMachinesTouch(int x, int y) {
    // Confirm overlay owns all input while it is up (v2.1.10 lesson).
    if (confirmForget) {
        if (isTouchInBounds(x, y, 28, 175, 78, 32)) {          // CANCEL
            confirmForget = false;
            drawEspNowMachinesScreen();
        } else if (isTouchInBounds(x, y, 114, 175, 98, 32)) {  // CONFIRM
            if (selectedRow >= 0) espnow_remove_profile((size_t)selectedRow);
            confirmForget = false;
            selectedRow   = espnow_active_profile_index();
            drawEspNowMachinesScreen();
        }
        return;
    }

    const size_t n = espnow_profile_count();
    for (size_t i = 0; i < n; i++) {
        if (isTouchInBounds(x, y, 5, machineRowY((int)i), 230, MROW_H)) {
            selectedRow = (int)i;
            espnow_select_profile(i);      // switch machine; link re-establishes
            drawEspNowMachinesScreen();
            return;
        }
    }

    if (isTouchInBounds(x, y, rowBtnX(2, 0), MACT_Y, rowBtnWAt(2, 0), MACT_H)) {
        if (n < 5) currentPendantScreen = PSCREEN_ESPNOW_PAIR;
        return;
    }
    if (isTouchInBounds(x, y, rowBtnX(2, 1), MACT_Y, rowBtnWAt(2, 1), MACT_H)) {
        if (selectedRow >= 0 && (size_t)selectedRow < n) {
            confirmForget = true;
            drawEspNowMachinesScreen();
        }
        return;
    }
    if (isTouchInBounds(x, y, 5, NAV_Y, 230, NAV_H)) {
        currentPendantScreen = PSCREEN_CONNECTION;
    }
}

#else   // !USE_ESPNOW — screens compile away to no-ops

void enterEspNowPair() {}
void exitEspNowPair() {}
void drawEspNowPairScreen() {}
void handleEspNowPairTouch(int, int) {}
void updateEspNowPairDisplay() {}
void enterEspNowMachines() {}
void exitEspNowMachines() {}
void drawEspNowMachinesScreen() {}
void handleEspNowMachinesTouch(int, int) {}
void updateEspNowMachinesDisplay() {}

#endif  // USE_ESPNOW
