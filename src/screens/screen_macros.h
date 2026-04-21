#pragma once
void enterMacros();
void exitMacros();
void drawMacrosScreen();
void updateMacrosFileList();   // sprite refresh — called by updateCurrentScreenSprites()
void handleMacrosTouch(int x, int y);
extern void requestMacros();  // defined in CNC_Pendant_UI.cpp
