#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "colors.h"
#include "eyes.h"
#include "net.h"
#include "sleep.h"

extern TFT_eSprite frame;

// TFT_BL comes from the board's TFT_eSPI setup file (pin 45 on the FNK0104)
#ifndef TFT_BL
#error "TFT_BL isn't defined, sleep mode needs the backlight pin from the TFT_eSPI setup"
#endif

// times as minutes since midnight
static const int DROWSY_MIN = 22 * 60 + 30;
static const int SLEEP_MIN = 23 * 60;
static const int WAKE_MIN = 7 * 60 + 30;

// backlight: 8-bit PWM, fast enough that the flicker can't be seen
static const uint32_t PWM_HZ = 5000;
static const uint8_t PWM_BITS = 8;
static const float GAMMA = 2.2f; // eyes see brightness on a curve, so even steps look even

static const unsigned long FALL_ASLEEP_MS = 60000; // bedtime fade to dark
static const unsigned long WAKE_UP_MS = 60000;     // morning fade up
static const unsigned long SETTLE_MS = 4000;       // dozing back off after a tap
static const unsigned long REST_AFTER_ASK_MS = 10000;
static const float PEEK_LEVEL = 0.2f; // how bright a tap at night lights the screen
static const unsigned long PEEK_FADE_MS = 800;
static const unsigned long PEEK_MS = 30000;
static const unsigned long GROGGY_MS = 6000;

// yawns while he's drowsy, a few minutes apart
static const long YAWN_MIN_MS = 3L * 60 * 1000;
static const long YAWN_MAX_MS = 6L * 60 * 1000;
static const unsigned long YAWN_MS = 1600;

static const Expression GROGGY = {40, 46, 0}; // heavy lids, drooping more at the outer corners

// the closed eyes: a curve that dips in the middle, drawn as short thick segments
static const int CLOSED_HALF_W = 28;
static const int CLOSED_DIP = 10;
static const int CLOSED_Y = CENTER_Y + 8;
static const int CLOSED_THICK = 5;
static const int CLOSED_STEPS = 8;

// the z drifting up past his right eye
static constexpr uint16_t Z_COLOUR = rgb565(150, 160, 200);
static const unsigned long Z_CYCLE_MS = 2400;
static const int Z_START_X = 258, Z_START_Y = 112;
static const int Z_RISE_X = 26, Z_RISE_Y = 50;

enum class Sleep
{
    Awake,
    Drowsy,        // 10:30 to 11: sleepy lids and yawns
    FallingAsleep, // eyes closed, the screen fading to dark
    Asleep,        // dark, nothing drawn
    Peek,          // tapped: dim, sleeping face, a drifting z
    Groggy,        // tapped again: half awake for a moment
    Answering,     // a question at night: fully awake until it's done
    Waking,        // 7:30: fading up with his eyes still closed
};

static Sleep state = Sleep::Awake;
static unsigned long stateStart = 0;
static bool started = false;
static int forced = -1;

static bool closedEyes = false; // drawSleep covers the face with closed eyes
static bool showZ = false;

// the backlight fade, from one level to another over a set time
static float level = 1.0f;
static float fadeFrom = 1.0f, fadeTo = 1.0f;
static unsigned long fadeStart = 0, fadeMs = 0;

static bool wasBusy = false; // to notice the moment an answer finishes

static bool yawning = false;
static unsigned long yawnStart = 0;
static unsigned long nextYawnAt = 0;

static void goTo(Sleep next)
{
    state = next;
    stateStart = millis();
}

static unsigned long timeInState()
{
    return millis() - stateStart;
}

static float easeInOut(float t)
{
    return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}

static void writeBacklight(float l)
{
    uint32_t duty = (uint32_t)(powf(l, GAMMA) * 255 + 0.5f);
#if TFT_BACKLIGHT_ON == LOW
    duty = 255 - duty; // this panel's backlight is on when the pin is low
#endif
    ledcWrite(TFT_BL, duty);
}

static void startFade(float to, unsigned long ms)
{
    fadeFrom = level;
    fadeTo = to;
    fadeStart = millis();
    fadeMs = ms;
}

static bool fadeDone()
{
    return level == fadeTo;
}

static void updateFade()
{
    float t = fadeMs == 0 ? 1 : (millis() - fadeStart) / (float)fadeMs;
    if (t > 1)
    {
        t = 1;
    }
    float next = fadeFrom + (fadeTo - fadeFrom) * easeInOut(t);
    if (t >= 1)
    {
        next = fadeTo; // land exactly, so fadeDone() can compare
    }
    if (next != level)
    {
        level = next;
        writeBacklight(level);
    }
}

static void updateYawn()
{
    if (!yawning)
    {
        return;
    }
    // stretch tall, hold, squeeze shut, then back to normal
    float e = (millis() - yawnStart) / (float)YAWN_MS;
    float s;
    if (e < 0.31f)
    {
        s = 1 + 0.25f * easeInOut(e / 0.31f);
    }
    else if (e < 0.5f)
    {
        s = 1.25f;
    }
    else if (e < 0.69f)
    {
        s = 1.25f - 1.1f * easeInOut((e - 0.5f) / 0.19f);
    }
    else if (e < 1)
    {
        s = 0.15f + 0.85f * easeInOut((e - 0.69f) / 0.31f);
    }
    else
    {
        s = 1;
        yawning = false;
    }
    setFaceStretch(s);
}

static void startYawn()
{
    yawning = true;
    yawnStart = millis();
}

static void scheduleYawn()
{
    nextYawnAt = millis() + random(YAWN_MIN_MS, YAWN_MAX_MS);
}

static void closeEyes(bool z)
{
    closedEyes = true;
    showZ = z;
    yawning = false;
    setFaceStretch(1);
}

static void openEyes()
{
    closedEyes = false;
    showZ = false;
}

static void fallAsleep(unsigned long ms)
{
    closeEyes(true);
    startFade(0, ms);
    goTo(Sleep::FallingAsleep);
}

void sleepInit()
{
    // TFT_eSPI switched the backlight fully on in init(), PWM takes the pin over from here
    ledcAttach(TFT_BL, PWM_HZ, PWM_BITS);
    level = fadeFrom = fadeTo = 1.0f;
    writeBacklight(level);
}

void updateSleep(bool steveBusy)
{
    // the clock, without blocking, same as the morning
    bool haveClock = timeIsValid();
    struct tm t = {};
    if (haveClock)
    {
        time_t now = time(nullptr);
        localtime_r(&now, &t);
    }
    int minutes = t.tm_hour * 60 + t.tm_min;
    bool night = haveClock && (minutes >= SLEEP_MIN || minutes < WAKE_MIN);
    bool drowsy = haveClock && minutes >= DROWSY_MIN && minutes < SLEEP_MIN;
    if (forced >= 0)
    {
        night = forced == 1;
        drowsy = false;
    }

    // switched on at night: start asleep, no bedtime routine
    if (!started && haveClock)
    {
        started = true;
        if (night)
        {
            closeEyes(false);
            level = fadeFrom = fadeTo = 0;
            writeBacklight(0);
            goTo(Sleep::Asleep);
        }
    }

    updateFade();
    updateYawn();

    switch (state)
    {
    case Sleep::Awake:
        if (night)
        {
            setFaceMode(FACE_FULL);
            setExpression(SLEEPY);
            fallAsleep(FALL_ASLEEP_MS);
        }
        else if (drowsy)
        {
            setFaceMode(FACE_FULL);
            setExpression(SLEEPY);
            scheduleYawn();
            goTo(Sleep::Drowsy);
        }
        break;

    case Sleep::Drowsy:
        if (night)
        {
            fallAsleep(FALL_ASLEEP_MS);
        }
        else if (!drowsy)
        {
            setExpression(NEUTRAL); // the clock was changed, or a test switched it off
            goTo(Sleep::Awake);
        }
        else if (steveBusy)
        {
            scheduleYawn(); // no yawning in the middle of an answer
        }
        else if (wasBusy)
        {
            setExpression(SLEEPY); // the answer's done, back to sleepy
        }
        else if ((long)(millis() - nextYawnAt) >= 0)
        {
            startYawn();
            scheduleYawn();
        }
        break;

    case Sleep::FallingAsleep:
        if (steveBusy)
        {
            openEyes();
            startFade(1, PEEK_FADE_MS);
            goTo(Sleep::Answering);
        }
        else if (!night)
        {
            startFade(1, WAKE_UP_MS);
            goTo(Sleep::Waking);
        }
        else if (fadeDone())
        {
            showZ = false;
            goTo(Sleep::Asleep);
        }
        break;

    case Sleep::Asleep:
        if (steveBusy)
        {
            openEyes();
            startFade(1, PEEK_FADE_MS);
            goTo(Sleep::Answering);
        }
        else if (!night)
        {
            showZ = false;
            startFade(1, WAKE_UP_MS);
            goTo(Sleep::Waking);
        }
        break;

    case Sleep::Peek:
        if (steveBusy)
        {
            openEyes();
            startFade(1, PEEK_FADE_MS);
            goTo(Sleep::Answering);
        }
        else if (timeInState() >= PEEK_MS)
        {
            fallAsleep(SETTLE_MS);
        }
        break;

    case Sleep::Groggy:
        if (timeInState() >= GROGGY_MS)
        {
            // "go away, I'm sleeping": eyes shut, back to dark
            fallAsleep(SETTLE_MS);
        }
        break;

    case Sleep::Answering:
        // stay up until the answer and its mood hold are done, then doze off again
        if (!steveBusy)
        {
            if (night)
            {
                setExpression(SLEEPY);
                fallAsleep(REST_AFTER_ASK_MS);
            }
            else
            {
                goTo(Sleep::Awake);
            }
        }
        break;

    case Sleep::Waking:
        if (night)
        {
            fallAsleep(SETTLE_MS); // a test flipped it back
        }
        else if (fadeDone() && closedEyes)
        {
            // fully bright: open up with a stretch, then he's back to normal
            openEyes();
            setExpression(SLEEPY);
            startYawn();
        }
        else if (!closedEyes && !yawning)
        {
            setExpression(NEUTRAL);
            goTo(Sleep::Awake);
        }
        break;
    }

    wasBusy = steveBusy;
}

bool sleepTap()
{
    switch (state)
    {
    case Sleep::Asleep:
    case Sleep::FallingAsleep:
        // light up dim and show him sleeping
        closeEyes(true);
        startFade(PEEK_LEVEL, PEEK_FADE_MS);
        goTo(Sleep::Peek);
        return true;

    case Sleep::Peek:
        // bothered him: heavy lids, looking at you
        openEyes();
        setExpression(GROGGY);
        goTo(Sleep::Groggy);
        return true;

    case Sleep::Groggy:
    case Sleep::Waking:
        return true; // already reacting, the tap shouldn't reach anything else

    default:
        return false; // awake: taps belong to the morning and the briefing
    }
}

bool sleepScreenDark()
{
    return state == Sleep::Asleep && level == 0;
}

void sleepForce(int mode)
{
    forced = mode;
}

void sleepYawn()
{
    startYawn();
}

void drawSleep()
{
    if (!closedEyes)
    {
        return;
    }

    // hide the open eyes underneath and draw closed ones instead
    frame.fillRect(REGION_X, REGION_Y, REGION_W, REGION_H, TFT_BLACK);
    for (int side = -1; side <= 1; side += 2)
    {
        int cx = CENTER_X + side * EYE_SPACING;
        int px = cx - CLOSED_HALF_W;
        int py = CLOSED_Y;
        for (int i = 1; i <= CLOSED_STEPS; i++)
        {
            // y = dip * (1 - x^2): lowest in the middle, rising to both corners
            float x = -1 + 2.0f * i / CLOSED_STEPS;
            int nx = cx + (int)(x * CLOSED_HALF_W);
            int ny = CLOSED_Y + (int)(CLOSED_DIP * (1 - x * x));
            frame.drawWideLine(px, py, nx, ny, CLOSED_THICK, TFT_WHITE, TFT_BLACK);
            px = nx;
            py = ny;
        }
    }

    if (showZ)
    {
        // one z at a time drifts up and to the right, fading in then out
        float t = (millis() % Z_CYCLE_MS) / (float)Z_CYCLE_MS;
        uint8_t alpha = (uint8_t)(sinf(t * PI) * 255);
        frame.setFreeFont(t < 0.5f ? &FreeSansBold9pt7b : &FreeSansBold12pt7b);
        frame.setTextDatum(MC_DATUM);
        frame.setTextColor(frame.alphaBlend(alpha, Z_COLOUR, TFT_BLACK));
        frame.drawString("z", Z_START_X + (int)(Z_RISE_X * t), Z_START_Y - (int)(Z_RISE_Y * t));
    }
}