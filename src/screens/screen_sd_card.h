#pragma once
void enterSDCard();
void exitSDCard();
void drawSDCardScreen();
void updateSDCardFileList();   // sprite refresh — called by updateCurrentScreenSprites()
void handleSDCardTouch(int x, int y);
