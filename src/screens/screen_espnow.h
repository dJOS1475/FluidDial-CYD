#pragma once

// ESP-NOW pairing wizard — ESPNOW_SPEC.md §6b
void enterEspNowPair();
void exitEspNowPair();
void drawEspNowPairScreen();
void handleEspNowPairTouch(int x, int y);
void updateEspNowPairDisplay();

// Paired-machine list (select / forget) — ESPNOW_SPEC.md §6c
void enterEspNowMachines();
void exitEspNowMachines();
void drawEspNowMachinesScreen();
void handleEspNowMachinesTouch(int x, int y);
void updateEspNowMachinesDisplay();
