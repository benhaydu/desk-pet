#include <Arduino.h>
#include "timer.h"


static TimerKind currentKind;
static unsigned long startTime;
static unsigned long durationMs;
static bool timerrunning = false;
static bool timerfinished = false;

void timerStart(TimerKind kind, unsigned long minutes){
    currentKind = kind;
    durationMs = 60000UL * minutes;
    startTime = millis();
    timerrunning = true;
    timerfinished = false;
}

void timerCancel(){
    timerrunning = false;
    timerfinished = false;
}

void updateTimer(){
    if (timerRunning() && (millis() - startTime) >= durationMs){
        timerrunning = false;
        timerfinished = true;
    }
}

bool timerRunning(){
    return timerrunning;
}

TimerKind timerKind(){
    return currentKind;
}

unsigned long timerRemainingMs(){
    if (!timerRunning()){
        return 0;
    }
    unsigned long timePassed = millis() - startTime;
    if(timePassed >= durationMs){
        return 0;
    }
    return durationMs - timePassed;
}

unsigned long timerDurationMs(){
    return durationMs;
}

bool timerTakeFinished(){
    bool temp = timerfinished;
    timerfinished = false;
    return temp;
}