#pragma once

enum class TimerKind
{
    Focus,
    Plain
};

void timerStart(TimerKind kind, unsigned long minutes); // starts, or restarts if one is running
void timerCancel();                                      // stops it, nothing happens at the end
void updateTimer();                                      // every loop, notices when time is up

bool timerRunning();
TimerKind timerKind();                  // only meaningful while running
unsigned long timerRemainingMs();       // 0 when nothing is running
unsigned long timerDurationMs();        // the full length, for a progress ring later

bool timerTakeFinished();               // true exactly once, right after a countdown reaches 0