#pragma once

void touchInit();      // call once in setup(), with nothing touching the wire
void updateTouch();    // call every loop(), samples every 20 ms
bool touchTakePress(); // true once per tap, then false until the next one
bool touchHeld();      // true while a finger is on it