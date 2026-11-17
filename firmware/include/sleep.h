#pragma once

// Steve's night: sleepy from 10:30 PM, asleep with the screen dark from 11, awake at 7:30.
// A tap at night shows him sleeping, a second tap half wakes him, a question wakes him up
void sleepInit();                 // once in setup(), after tft.init(): takes over the backlight
void updateSleep(bool steveBusy); // every loop. steveBusy: answering or a timer, so stay up (or wake up) for it
void drawSleep();                 // every frame, after the face: closed eyes and z's
bool sleepCoversFace();           // his eyes are closed, so main skips drawing the open ones
bool sleepFaceFree();             // false while asleep or waking up: main holds its reactions until true
bool sleepTap();                  // a tap at night. True if sleep used it
bool sleepScreenDark();           // the backlight is off, so main can skip drawing frames
void sleepForce(int mode);        // test: 1 = night now, 0 = day now, -1 = follow the clock
void sleepYawn();                 // test: one yawn