#include <Arduino.h>
#include <TFT_eSPI.h>
#include "eyes.h" // REGION_Y and REGION_H
#include "headline.h"

extern TFT_eSprite frame;

// the strip below the face, the face loop never draws here
static const int STRIP_Y = REGION_Y + REGION_H; // first row below the face (195)
static const int STRIP_H = 240 - STRIP_Y;       // the rest of the screen (45)
static const int STRIP_W = 320;
static const int MARGIN = 8; // keep text off the very edge

// how long a headline stays up: a base, plus a bit more for longer ones, with a cap
static const unsigned long HOLD_BASE_MS = 3000;
static const unsigned long HOLD_PER_CHAR_MS = 100;
static const unsigned long HOLD_MAX_MS = 7000;

// biggest first, headlineShow uses the first one that fits
static const GFXfont *FONTS[] = {&FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSansBold9pt7b};

static char text[40];
static const GFXfont *chosenFont;
static bool showing = false;
static bool dirty = false;
static unsigned long shownAt = 0;
static unsigned long holdMs = 0;

void headlineShow(const char *t)
{
    snprintf(text, sizeof(text), "%s", t);

    // try each size, biggest first, and keep the first one that fits the strip
    size_t count = sizeof(FONTS) / sizeof(FONTS[0]);
    chosenFont = FONTS[count - 1]; // smallest, in case nothing fits
    for (size_t i = 0; i < count; i++)
    {
        frame.setFreeFont(FONTS[i]);
        if (frame.textWidth(text) <= STRIP_W - 2 * MARGIN)
        {
            chosenFont = FONTS[i];
            break;
        }
    }

    // longer headlines get a little longer on screen
    holdMs = HOLD_BASE_MS + HOLD_PER_CHAR_MS * strlen(text);
    if (holdMs > HOLD_MAX_MS)
    {
        holdMs = HOLD_MAX_MS;
    }

    showing = true;
    shownAt = millis();
    dirty = true;
}

void updateHeadline()
{
    if (showing && millis() - shownAt >= holdMs)
    {
        headlineHide();
    }
}

void drawHeadline()
{
    if (!dirty)
    {
        return;
    }

    frame.fillRect(0, STRIP_Y, STRIP_W, STRIP_H, TFT_BLACK);

    if (showing)
    {
        frame.setFreeFont(chosenFont);
        frame.setTextColor(TFT_WHITE, TFT_BLACK);
        frame.setTextDatum(MC_DATUM);
        frame.drawString(text, STRIP_W / 2, STRIP_Y + STRIP_H / 2);
    }

    // send just the strip to the screen, whether we drew text or only cleared it
    frame.pushSprite(0, STRIP_Y, 0, STRIP_Y, STRIP_W, STRIP_H);
    dirty = false;
}

bool headlineActive()
{
    return showing;
}

void headlineHide()
{
    // drawHeadline does the actual clearing next frame
    showing = false;
    dirty = true;
}