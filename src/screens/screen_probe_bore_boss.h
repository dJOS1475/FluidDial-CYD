#pragma once
// SCR3a — bore (inside circle) centre probe
void enterProbeBore();
void exitProbeBore();
void drawProbeBoreScreen();
void updateProbeBoreScreen();
void handleProbeBoreTouch(int x, int y);

// SCR3b — boss (outside circle) centre probe
void enterProbeBoss();
void exitProbeBoss();
void drawProbeBossScreen();
void updateProbeBossScreen();
void handleProbeBossTouch(int x, int y);
