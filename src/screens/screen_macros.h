#pragma once
void enterMacros();
void exitMacros();
void drawMacrosScreen();
void updateMacrosFileList();   // sprite refresh — called by updateCurrentScreenSprites()
void handleMacrosTouch(int x, int y);
void handleMacrosDial(int delta);    // jog dial pages the list (one screen per detent)
extern void requestMacros();  // defined in CNC_Pendant_UI.cpp
