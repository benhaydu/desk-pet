#pragma once

// the focus vine: a pot on the right of the screen with a vine that climbs out of it.
// Drawing only, timerview decides how grown it is and when the flowers come out
void drawVine(float growth,  // 0 = just soil, 1 = full height, near the top of the screen
              float bloom,   // 0 = no flowers, 1 = all of them out. They pop in one after another
              int shiftX);   // moves the whole plant right, for sliding the pot off screen
void vineTip(float growth, int &x, int &y); // where the growing tip is, for Steve to watch
int vineGrowthPx(float growth);             // how tall that is in whole pixels, to notice when it changes