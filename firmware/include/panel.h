#pragma once
#include "ask.h" // Card

// the briefing's text panels: icon and title along the top, centred lines below,
// progress dots at the bottom. Mini Steve keeps the top-right corner
void panelShow(const Card &card, int index, int total); // slides in from the right
void panelHide();                                       // slides out to the left
void updatePanel();                                     // once per frame, moves the slide along
void drawPanel();                                       // once per frame, after the face push
bool panelBusy();                                       // true while sliding in or out