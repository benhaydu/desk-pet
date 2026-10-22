#include <Arduino.h>
#include <TFT_eSPI.h>
#include "eyes.h"
#include "weather.h"
#include "status.h"

// the sprite lives in main.cpp
extern TFT_eSprite frame;

// strip sits right under the eye region and runs to the bottom of the screen
static const int STATUS_Y = REGION_Y + REGION_H;
static const int STATUS_H = 240 - STATUS_Y;
static const int STATUS_W = 320;
static const unsigned long STATUS_CHECK_MS = 1000;

static char lastDrawn[32] = ""; // what's on screen right now
static unsigned long lastCheck = 0;

// fills out with whatever the strip should be showing right now
static void buildStatusText(char *out, size_t outSize)
{
    char tempPart[12];
    char timePart[12];

    if (weatherValid())
    {
        snprintf(tempPart, sizeof(tempPart), "%.0f C", weatherTemp());
    }
    else
    {
        snprintf(tempPart, sizeof(tempPart), "-- C");
    }

    struct tm now;
    if (getLocalTime(&now, 0))
    {
        int hour = now.tm_hour % 12; // 0-23 becomes 0-11
        if (hour == 0)
        {
            hour = 12; // midnight and noon show as 12, not 0
        }
        snprintf(timePart, sizeof(timePart), "%d:%02d %s",
                 hour, now.tm_min, now.tm_hour < 12 ? "AM" : "PM");
    }
    else
    {
        snprintf(timePart, sizeof(timePart), "--:--");
    }
    snprintf(out, outSize, "%s   %s", tempPart, timePart);
}

void updateStatus()
{
    if (millis() - lastCheck < STATUS_CHECK_MS)
    {
        return;
    }
    lastCheck = millis();

    char text[32];
    buildStatusText(text, sizeof(text));

    // nothing changed, leave the screen alone
    if (strcmp(text, lastDrawn) == 0)
    {
        return;
    }

    frame.fillRect(0, STATUS_Y, STATUS_W, STATUS_H, TFT_BLACK);
    frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    frame.setTextDatum(MC_DATUM);
    frame.drawString(text, STATUS_W / 2, STATUS_Y + STATUS_H / 2, 4);
    frame.pushSprite(0, STATUS_Y, 0, STATUS_Y, STATUS_W, STATUS_H);

    strcpy(lastDrawn, text);
}