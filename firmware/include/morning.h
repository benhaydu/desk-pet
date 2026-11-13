#pragma once

// the weekday morning briefing: fetch it at 8:50, read the paper from 9:00 until Ben taps,
// play it, and forget about it at noon if he never does
void updateMorning(bool steveBusy); // every loop. steveBusy: answering a question, so step aside
bool morningTap();                  // a tap while he's waiting. True if the morning used it
bool morningWaiting();              // reading the paper, waiting for a tap
void morningForce();                // test: run the whole morning now, ignoring the clock