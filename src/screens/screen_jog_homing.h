#pragma once

// Slot 3 of the jog increment row is a dial box holding the coarse increments
// (10/50/100 mm, .5/2.0/4.0 in); pendantJog.coarseIdx picks which one.
#define JOG_COARSE_COUNT 3

// Re-applies slot 3's value to pendantJog.increment after coarseIdx changes.
// No-op unless slot 3 is the selected increment.
void jogApplyCoarseIncrement();

void enterJogHoming();
void exitJogHoming();
void drawJogHomingScreen();
void updateJogAxisDisplay();
void redrawJogAxisButtons();
void redrawJogIncrementButtons();
void redrawJogSpeedButton();
void requestJogConfig();
void handleJogHomingTouch(int x, int y);
