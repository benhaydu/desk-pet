#pragma once

void headlineShow(const char *text);  // copy it, pick a font that fits, start the timer
void updateHeadline();                // once per frame, clears it when time's up
void drawHeadline();                  // once per frame, only draws when something changed
bool headlineActive();                // true while it's on screen
void headlineHide();                  // clear it right away (a new question came in)