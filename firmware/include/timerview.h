#pragma once
#include "eyes.h"  // Rect
#include "timer.h" // TimerKind

// how a timer looks. A plain timer is a ring around a slightly smaller Steve with the time
// inside, then "Time's up!" until a tap. A focus session is a vine climbing out of a pot
// beside him, which flowers at the end, and a tap then starts a 5 minute break.
// It watches the timer itself to know when to show; main tells it about endings and taps
void updateTimerView();                 // once per frame, before working out the push region
void drawTimerView(Rect pushed);        // once per frame, after drawFace(), with the region being pushed
bool timerViewActive();                 // on screen right now
bool timerViewOwnsFace();               // Steve has settled into place: this view's region covers him too
Rect timerViewRegion();                 // what needs pushing this frame
bool timerViewTakeClear(Rect &out);     // true once after a look goes away, with the area to wipe
void timerViewSetAside(bool aside);     // true while Steve needs his whole face: answers, the paper, sleep
void timerViewFinished(TimerKind kind); // the timer just ran out, show its ending
bool timerViewTap();                    // a tap. True if the view used it
bool timerViewDismiss();                // /cancel with no timer running. True if there was an ending to put away
bool timerViewBusy();                   // a timer or its ending is on, even set aside, so he stays up for it