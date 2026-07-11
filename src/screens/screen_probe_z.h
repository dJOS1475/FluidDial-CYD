#pragma once
// SCR1 — Z surface probe
void enterProbeZ();
void exitProbeZ();
void drawProbeZScreen();
void updateProbeZScreen();
void updateProbeZFields();   // redraw only the KV fields (flicker-free dial edit)
void handleProbeZTouch(int x, int y);
