#pragma once
// SCR2 — XYZ corner probe
void enterProbeCorner();
void exitProbeCorner();
void drawProbeCornerScreen();
void updateProbeCornerScreen();
void updateProbeCornerFields();   // redraw only the KV fields (flicker-free dial edit)
void handleProbeCornerTouch(int x, int y);
