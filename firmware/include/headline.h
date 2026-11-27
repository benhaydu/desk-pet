#pragma once
#include "eyes.h" // Rect

void headlineShow(const char *text);  // copy it, pick a font that fits, start the timer
void updateHeadline();                // once per frame, clears it when time's up
void drawHeadline(Rect pushed);       // once per frame, after the main push: draws when it changed or that push covered it
bool headlineActive();                // true while it's on screen
void headlineHide();                  // clear it right away (a new question came in)