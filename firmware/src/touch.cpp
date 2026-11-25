#include <Arduino.h>
#include "pins.h"
#include "touch.h"

static const unsigned long SAMPLE_MS = 20;
// a touch read 4 to 5 times the resting value, so double is a safe line.
// letting go has to drop below 1.5x, so a reading near the line can't flicker
static const float PRESS_RATIO = 2.0f;
static const float RELEASE_RATIO = 1.5f;

// a bare wire is noisy right as a finger lands or lifts: the reading can dip and come back,
// which used to count as a second tap. So a change only counts once it has lasted a few
// samples in a row. Pressing needs 40 ms, letting go 80 ms: long enough to ride out the
// flicker, short enough that a real double tap (about 100 ms apart) still counts as two
static const int PRESS_SAMPLES = 2;
static const int RELEASE_SAMPLES = 4;

static float baseline = 0; // the resting reading, follows slow drift
static bool touching = false;
static bool pressPending = false;
static int streak = 0;     // samples in a row that disagree with `touching`
static unsigned long lastSample = 0;

static bool debugOn = false;
static unsigned long lastDebug = 0;

void touchInit()
{
    // average a few readings so one odd sample doesn't set the baseline
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++)
    {
        sum += touchRead(PET_PIN_TOUCH);
        delay(5); // fine in setup, nothing is animating yet
    }
    baseline = sum / 16.0f;
    Serial.printf("touch baseline: %.0f\n", baseline);
}

void updateTouch()
{
    if (millis() - lastSample < SAMPLE_MS)
    {
        return;
    }
    lastSample = millis();
    uint32_t v = touchRead(PET_PIN_TOUCH);

    // two different lines for pressing and letting go
    bool now = touching ? (v > baseline * RELEASE_RATIO) : (v > baseline * PRESS_RATIO);

    if (now == touching)
    {
        streak = 0; // agrees with what we have, any flicker is over
    }
    else
    {
        streak++;
        if (streak >= (touching ? RELEASE_SAMPLES : PRESS_SAMPLES))
        {
            touching = now;
            streak = 0;
            if (touching)
            {
                pressPending = true; // only the moment it goes down counts as a tap
            }
        }
    }

    // while untouched, let the baseline creep toward the reading so slow drift doesn't matter
    if (!touching && v < baseline * 1.3f)
    {
        baseline += (v - baseline) * 0.01f;
    }

    if (debugOn && millis() - lastDebug >= 200)
    {
        lastDebug = millis();
        Serial.printf("touch %lu  baseline %.0f  (%.1fx)%s\n", (unsigned long)v, baseline, v / baseline,
                      touching ? "  touching" : "");
    }
}

bool touchTakePress()
{
    bool p = pressPending;
    pressPending = false;
    return p;
}

bool touchHeld()
{
    return touching;
}

void touchDebug(bool on)
{
    debugOn = on;
}