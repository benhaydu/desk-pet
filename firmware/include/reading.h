#pragma once
#include "eyes.h" // Rect

// Steve reading the paper while the morning briefing waits for a tap. The paper rises in
// front of him, his eyes move along the lines, and every so often he lowers it to peek at you
void readingShow();             // the paper rises into view. Safe to call every loop
void readingHide();             // the paper drops away. Safe to call every loop
void updateReading();           // once per frame, before working out the push region
void drawReading();             // once per frame, after drawFace(): the paper is in front of him
Rect readingRegion();           // where the paper is now and was last frame, empty when off screen
bool readingTakeClear(Rect &out); // true once after the paper has gone, with the area to wipe
bool readingActive();           // up, rising or dropping