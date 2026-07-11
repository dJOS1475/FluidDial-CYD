#pragma once
// SCR3a — bore (inside circle) centre probe
void enterProbeBore();
void exitProbeBore();
void drawProbeBoreScreen();
void updateProbeBoreScreen();
void updateProbeBoreFields();   // redraw only the KV fields (flicker-free dial edit)
void handleProbeBoreTouch(int x, int y);

// SCR3b — boss (outside circle) centre probe
void enterProbeBoss();
void exitProbeBoss();
void drawProbeBossScreen();
void updateProbeBossScreen();
void updateProbeBossFields();   // redraw only the KV fields (flicker-free dial edit)
void handleProbeBossTouch(int x, int y);
