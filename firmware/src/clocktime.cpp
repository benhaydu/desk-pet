#include <Arduino.h>
#include "clocktime.h"

// the morning and sleep both check the time every loop, and the loop runs thousands of
// times a second. Minutes are all they need, so once a second is plenty
static const unsigned long REFRESH_MS = 1000;

// before NTP sets it, the clock counts up from 1970, so any year before this means "not set"
static const int FIRST_VALID_YEAR = 2016;

bool localNow(struct tm &out)
{
    static bool readOnce = false;
    static unsigned long lastRead = 0;
    static struct tm cached = {};

    if (!readOnce || millis() - lastRead >= REFRESH_MS)
    {
        readOnce = true;
        lastRead = millis();
        time_t now = time(nullptr);
        localtime_r(&now, &cached);
    }
    out = cached;
    return cached.tm_year + 1900 >= FIRST_VALID_YEAR; // tm_year counts from 1900
}