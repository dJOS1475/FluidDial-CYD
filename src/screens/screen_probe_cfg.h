#pragma once
// SCR0a — 3D touch-probe config
void enterProbeCfg3D();
void exitProbeCfg3D();
void drawProbeCfg3DScreen();
void handleProbeCfg3DTouch(int x, int y);
void updateProbeCfg3DScreen();
void updateProbeCfg3DFields();   // redraw only the KV fields (flicker-free dial edit)

// SCR0b — touch-plate config
void enterProbeCfgPlate();
void exitProbeCfgPlate();
void drawProbeCfgPlateScreen();
void handleProbeCfgPlateTouch(int x, int y);
void updateProbeCfgPlateFields();   // redraw only the KV fields (flicker-free dial edit)
