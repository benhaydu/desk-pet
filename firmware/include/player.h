#pragma once
#include "ask.h" // Briefing

// plays a briefing card by card: face cards use the big face, the scene and the headline,
// panels use mini Steve and the panel. Each card holds for a while, a tap skips ahead
void playerStart(const Briefing &b); // ignored if one is already playing or it's empty
void updatePlayer();                 // every loop
bool playerActive();
void playerSkip();                   // a tap: end the current card early
void playerStop();                   // wrap up quickly, back to the normal face