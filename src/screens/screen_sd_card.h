#pragma once
void enterSDCard();
void exitSDCard();
void drawSDCardScreen();
void updateSDCardFileList();   // sprite refresh — called by updateCurrentScreenSprites()
void handleSDCardTouch(int x, int y);
void handleSDCardDial(int delta);    // jog dial pages the list (one screen per detent)
