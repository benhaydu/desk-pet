#include <Arduino.h>
#include <TFT_eSPI.h>
#include "clocktime.h"
#include "colors.h"
#include "eyes.h"
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

// how long the screen takes to fade to dark, depending on why he's going to sleep
static const unsigned long BEDTIME_FADE_MS = 60000;
static const unsigned long AFTER_ASK_FADE_MS = 10000;
static const unsigned long SETTLE_FADE_MS = 4000; // after a tap
static const unsigned long WAKE_UP_MS = 60000;    // the morning fade up
static const unsigned long TAP_WAKE_MS = 500;     // a tap during that fade hurries it along

// a tap at night
static const float PEEK_LEVEL = 0.2f;
static const unsigned long PEEK_FADE_MS = 800;
static const unsigned long PEEK_MS = 30000;
static const unsigned long GROGGY_MS = 6000;

// woken by a question: brighten with his eyes shut, open them slowly, one slow blink.
// Nothing else touches his face until this is done (sleepFaceFree), so it never fights the thinking
static const unsigned long ROUSE_FADE_MS = 600;
static const unsigned long ROUSE_OPEN_MS = 500;
static const unsigned long ROUSE_BLINK_MS = 600;

// drifting off: a sleepy beat, maybe a yawn, then the eyes close slowly to a slit
static const unsigned long DRIFT_HOLD_MS = 1200;
static const unsigned long DRIFT_CLOSE_MS = 1200;
static const float SLIT = 0.08f; // how open the eyes are when the closed curves take over

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
    DriftingOff,   // eyes open and sleepy, closing slowly
    FallingAsleep, // eyes closed, the screen fading to dark
    Asleep,        // dark, nothing drawn
    Peek,          // tapped: dim, sleeping face, a drifting z
    Groggy,        // tapped again: half awake for a moment
    Rousing,       // woken by a question: brightening, eyes opening, one blink
    Answering,     // awake until the question and its mood hold are done
    Waking,        // 7:30: fading up with his eyes still closed
};

// the steps inside DriftingOff
enum class Drift
{
    Hold,
    Yawn,
    Close,
};

static Sleep state = Sleep::Awake;
static unsigned long stateStart = 0;
static bool started = false;
static int forced = -1;
static bool wasBusy = false; // to notice the moment an answer finishes

static bool closedEyes = false; // drawSleep draws closed eyes where the open ones would be
static bool showZ = false;

// the backlight fade, from one level to another over a set time
static float level = 1.0f;
static float fadeFrom = 1.0f, fadeTo = 1.0f;
static unsigned long fadeStart = 0, fadeMs = 0;

static bool yawning = false;
static unsigned long yawnStart = 0;
static unsigned long nextYawnAt = 0;

// what this drift-off does once the eyes are shut
static Drift driftStep = Drift::Hold;
static bool driftYawn = false;
static unsigned long driftFadeMs = 0;
static unsigned long closeStart = 0;

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
    if (fadeDone())
    {
        return; // nothing moving, don't touch the pin
    }
    float t = fadeMs == 0 ? 1 : (millis() - fadeStart) / (float)fadeMs;
    // land exactly on the target at the end, so fadeDone() can compare with ==
    level = t >= 1 ? fadeTo : fadeFrom + (fadeTo - fadeFrom) * easeInOut(t);
    writeBacklight(level);
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

// one slow blink as a stretch value: close, a short hold shut, then open more slowly
static float blinkStretch(unsigned long ms)
{
    float p = ms / (float)ROUSE_BLINK_MS;
    if (p < 0.33f)
    {
        return 1 - (1 - SLIT) * easeInOut(p / 0.33f);
    }
    if (p < 0.5f)
    {
        return SLIT;
    }
    return SLIT + (1 - SLIT) * easeInOut((p - 0.5f) / 0.5f);
}

static void closeEyes(bool z)
{
    closedEyes = true;
    showZ = z;
    yawning = false;
    setFaceStretch(1); // reset under the cover, so the eyes are normal when they open again
}

static void openEyes()
{
    closedEyes = false;
    showZ = false;
}

// eyes are already shut (a peek running out): just fade the screen
static void fallAsleep(unsigned long fadeMsToDark)
{
    closeEyes(true);
    startFade(0, fadeMsToDark);
    goTo(Sleep::FallingAsleep);
}

// eyes are open: a beat with heavy lids, maybe a yawn, close slowly, then fade
static void driftOff(unsigned long fadeMsToDark, bool yawnFirst, Expression look)
{
    setExpression(look);
    yawning = false;
    setFaceStretch(1);
    driftFadeMs = fadeMsToDark;
    driftYawn = yawnFirst;
    driftStep = Drift::Hold;
    goTo(Sleep::DriftingOff);
}

// woken for a question, from any of the night states
static void rouse()
{
    showZ = false;
    yawning = false;
    startFade(1, ROUSE_FADE_MS);
    goTo(Sleep::Rousing);
    if (!closedEyes)
    {
        // already open (groggy, or mid drift-off): skip the brightening and the opening,
        // ease the lids up to sleepy and go straight to the blink
        setFaceStretch(1);
        setExpression(SLEEPY);
        stateStart -= ROUSE_FADE_MS + ROUSE_OPEN_MS;
    }
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
    struct tm t;
    bool haveClock = localNow(t);
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

    // a question at night wakes him from any of the sleeping states
    bool sleeping = state == Sleep::DriftingOff || state == Sleep::FallingAsleep ||
                    state == Sleep::Asleep || state == Sleep::Peek || state == Sleep::Groggy ||
                    state == Sleep::Waking;
    if (steveBusy && sleeping)
    {
        rouse();
    }

    switch (state)
    {
    case Sleep::Awake:
        if (night)
        {
            setFaceMode(FACE_FULL);
            driftOff(BEDTIME_FADE_MS, true, SLEEPY);
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
            driftOff(BEDTIME_FADE_MS, true, SLEEPY);
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

    case Sleep::DriftingOff:
        if (!night)
        {
            setFaceStretch(1);
            setExpression(NEUTRAL);
            goTo(Sleep::Awake);
            break;
        }
        switch (driftStep)
        {
        case Drift::Hold:
            if (timeInState() >= DRIFT_HOLD_MS)
            {
                if (driftYawn)
                {
                    startYawn();
                    driftStep = Drift::Yawn;
                }
                else
                {
                    closeStart = millis();
                    driftStep = Drift::Close;
                }
            }
            break;
        case Drift::Yawn:
            if (!yawning)
            {
                closeStart = millis();
                driftStep = Drift::Close;
            }
            break;
        case Drift::Close:
        {
            float c = (millis() - closeStart) / (float)DRIFT_CLOSE_MS;
            if (c >= 1)
            {
                // down to a slit: swap to the closed curves and start the fade
                fallAsleep(driftFadeMs);
            }
            else
            {
                setFaceStretch(1 - (1 - SLIT) * easeInOut(c));
            }
            break;
        }
        }
        break;

    case Sleep::FallingAsleep:
        if (!night)
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
        if (!night)
        {
            startFade(1, WAKE_UP_MS);
            goTo(Sleep::Waking);
        }
        break;

    case Sleep::Peek:
        if (timeInState() >= PEEK_MS)
        {
            fallAsleep(SETTLE_FADE_MS);
        }
        break;

    case Sleep::Groggy:
        if (timeInState() >= GROGGY_MS)
        {
            driftOff(SETTLE_FADE_MS, false, GROGGY); // no yawn, he just wants you to go away
        }
        break;

    case Sleep::Rousing:
    {
        unsigned long e = timeInState();
        if (e < ROUSE_FADE_MS)
        {
            break; // brightening, eyes still shut so you see him asleep first
        }
        if (closedEyes)
        {
            // swap the closed curves for a slit, the same size they closed to,
            // so opening is the drift-off played backwards
            openEyes();
            setExpression(SLEEPY);
            setFaceStretch(SLIT);
        }
        unsigned long o = e - ROUSE_FADE_MS;
        if (o < ROUSE_OPEN_MS)
        {
            setFaceStretch(SLIT + (1 - SLIT) * easeInOut(o / (float)ROUSE_OPEN_MS));
        }
        else if (o < ROUSE_OPEN_MS + ROUSE_BLINK_MS)
        {
            setFaceStretch(blinkStretch(o - ROUSE_OPEN_MS));
        }
        else
        {
            // awake: hand his face back, the thinking starts from these sleepy lids
            setFaceStretch(1);
            goTo(Sleep::Answering);
        }
        break;
    }

    case Sleep::Answering:
        // stay up until the answer and its mood hold are done, then drift back off
        if (!steveBusy)
        {
            if (night)
            {
                driftOff(AFTER_ASK_FADE_MS, true, SLEEPY);
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
            fallAsleep(SETTLE_FADE_MS); // a test flipped it back
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
    case Sleep::DriftingOff:
        // bothered him: heavy lids, looking at you
        openEyes();
        yawning = false;
        setFaceStretch(1);
        setExpression(GROGGY);
        goTo(Sleep::Groggy);
        return true;

    case Sleep::Waking:
        // a tap during the slow morning fade: wake up now instead of in a minute.
        // The Waking case opens his eyes as soon as this shorter fade lands
        startFade(1, TAP_WAKE_MS);
        return true;

    case Sleep::Groggy:
    case Sleep::Rousing:
        return true; // already reacting, the tap shouldn't reach anything else

    default:
        return false; // awake: taps belong to the morning and the briefing
    }
}

bool sleepCoversFace()
{
    return closedEyes;
}

bool sleepFaceFree()
{
    // closed eyes or mid wake-up: anything main shows now would be hidden or fight the animation
    return !closedEyes && state != Sleep::Rousing;
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

    // main skipped drawFace() and already cleared the face area, so just the closed eyes
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