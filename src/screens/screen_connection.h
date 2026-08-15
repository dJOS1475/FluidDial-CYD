#pragma once

// Connection screen — transport selection (UART / ESP-NOW) and live link status.
// Replaces the v2.1.x WiFi Setup screen; see ESPNOW_SPEC.md §6a.
void enterConnection();
void exitConnection();
void drawConnectionScreen();
void handleConnectionTouch(int x, int y);
void updateConnectionDisplay();
