#include <Arduino.h>
#include <TFT_eSPI.h>
#include "colors.h"
#include "timer.h"
#include "timerview.h"
#include "vine.h"

extern TFT_eSprite frame;

static constexpr uint16_t TRACK = rgb565(55, 55, 55); // the part of the ring already used up
static constexpr uint16_t PLAIN_BLUE = rgb565(120, 180, 240);
static constexpr uint16_t FOCUS_GREEN = rgb565(110, 200, 95);
static constexpr uint16_t HINT_GREY = rgb565(150, 150, 150);

// the ring: as big as the screen allows, so Steve barely has to shrink to fit inside
static const int RING_X = 160;
static const int RING_Y = 120;
static const int RING_R = 116;
static const int RING_THICK = 6;
static const int RING_TEXT_Y = 172;
static const float RING_FACE_SCALE = 0.68f;
static const int RING_FACE_OFFSET_Y = -12;

// drawSmoothArc measures angles clockwise from 6 o'clock, so 12 o'clock is 180
static const int TOP = 180;

// the whole ring, with a couple of pixels for its anti-aliased edge
static const Rect RING_BOX = {RING_X - RING_R - 2, RING_Y - RING_R - 2, 2 * RING_R + 4, 2 * RING_R + 4};

// the middle of the ring: everywhere the small Steve can reach (glances, blinks, the
// time's-up hop) plus the text. Most frames only this changes
static const Rect MIDDLE_BOX = {58, 48, 204, 136};

// time's up: wide eyes, a little hop every so often, and the words pulsing
static const float ALERT_STRETCH = 1.1f;
static const unsigned long HOP_EVERY_MS = 1400;
static const unsigned long HOP_MS = 320;
static const int HOP_PX = 7;
static const unsigned long PULSE_MS = 1200;
static const unsigned long ALERT_MAX_MS = 30UL * 60 * 1000; // so a missed alert can't keep him up all night

// beside the vine: a bit smaller and moved left, leaving the right side for the pot
static const float VINE_FACE_SCALE = 0.75f;
static const int VINE_FACE_OFFSET_Y = -10;
static const int VINE_SHIFT_X = -58;                   // his middle ends up at x 102
static const int VINE_TEXT_X = CENTER_X + VINE_SHIFT_X; // the time sits under him
static const int VINE_TEXT_Y = 186;
static const int VINE_HINT_Y = 208;

// his eyes stay on the vine's tip. Any further right than 26 and his right eye
// would cross into the vine's box, which isn't pushed every frame
static const float VINE_LOOK_X = 26;
static const float LOOK_Y_MIN = -28, LOOK_Y_MAX = 22;

// Steve and the text, everywhere he can reach in this layout, and the plant. They don't
// overlap, so most frames only push the left one: the plant only changes once every few seconds
static const Rect VINE_FACE_BOX = {0, 46, 200, 174};
static const Rect VINE_BOX = {200, 4, 120, 196};
static const Rect VINE_ALL = {0, 4, 320, 216};

static const unsigned long SETTLE_MS = 400; // a bit longer than his 300 ms resize

// the vine's ending, and tidying it away
static const unsigned long BLOOM_MS = 1600;
static const unsigned long BREAK_OFFER_MS = 30000; // a tap within this long starts the break
static const unsigned long BREAK_MINUTES = 5;
static const unsigned long RETRACT_MIN_MS = 300;   // a short vine pulls in quickly,
static const unsigned long RETRACT_FULL_MS = 800;  // a full grown one takes this long
static const unsigned long SLIDE_MS = 350;
static const int SLIDE_PX = 100; // far enough that the pot is off the right edge

// an ending that arrives while he's busy waits for him, but not forever
static const unsigned long PENDING_MS = 10UL * 60 * 1000;

enum class Stage
{
    Idle,
    Ring,      // a plain timer counting down
    RingEnd,   // time's up, until a tap
    Vine,      // a focus session, the vine growing
    VineEnd,   // flowers out, a tap starts the break
    VineTidy,  // the vine pulling back into the pot
    VineSlide, // then the pot sliding off
};

enum class Look
{
    None,
    Ring,
    Vine,
};

static Stage stage = Stage::Idle;
static unsigned long stageStart = 0;
static bool setAside = false;

// which layout Steve is in right now, which can lag behind the stage (set aside)
static Look shown = Look::None;
static unsigned long shownAt = 0;
static bool clearPending = false;
static Rect clearBox = {0, 0, 0, 0};

// endings
static unsigned long finishedAt = 0;
static bool endStarted = false; // its animation has begun, it waits for the screen if he's busy
static unsigned long endStart = 0;
static bool endFace = false;    // the ending's expression and stretch are on him right now
static bool onBreak = false;    // the ring counting down is the break after a focus session
static bool breakWanted = false;

// tidying the vine away, from however grown it was
static float tidyGrowth = 0;
static float tidyBloom = 0;
static unsigned long tidyMs = 0;

// what this frame draws, worked out in updateTimerView
static int sweep = 0;
static int pushedSweep = -1;
static bool ringChanged = true;
static float growth = 0;
static float bloom = 0;
static int slide = 0;
static int pushedPx = -1, pushedBloom = -1, pushedSlide = -1;
static bool vineChanged = true;

static bool nudgeMine = false; // the hop's nudge. Only hand it back once, other things use it too
static bool gazeMine = false;

static float clamp01(float v)
{
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

static float easeInOut(float t)
{
    return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
}

static Rect unite(Rect a, Rect b)
{
    if (a.w <= 0 || a.h <= 0)
    {
        return b;
    }
    if (b.w <= 0 || b.h <= 0)
    {
        return a;
    }
    int left = min(a.x, b.x);
    int top = min(a.y, b.y);
    int right = max(a.x + a.w, b.x + b.w);
    int bottom = max(a.y + a.h, b.y + b.h);
    return {left, top, right - left, bottom - top};
}

static bool overlaps(Rect a, Rect b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void goTo(Stage next)
{
    stage = next;
    stageStart = millis();
}

static Look lookFor(Stage s)
{
    switch (s)
    {
    case Stage::Ring:
    case Stage::RingEnd:
        return Look::Ring;
    case Stage::Vine:
    case Stage::VineEnd:
    case Stage::VineTidy:
    case Stage::VineSlide:
        return Look::Vine;
    default:
        return Look::None;
    }
}

static bool settled()
{
    return millis() - shownAt >= SETTLE_MS;
}

static void lookAt(float x, float y)
{
    setGazeTarget(x, y);
    gazeMine = true;
}

static void stopLooking()
{
    if (gazeMine)
    {
        clearGazeTarget();
        gazeMine = false;
    }
}

// take the ending's face back off him. Only when the ending itself is over: being set
// aside leaves the expression alone, because the answer sets its own
static void endEnding()
{
    if (endFace)
    {
        setExpression(NEUTRAL);
        setFaceStretch(1.0f);
        endFace = false;
    }
    endStarted = false;
}

static void goIdle()
{
    endEnding();
    onBreak = false;
    goTo(Stage::Idle);
}

static void startTidy(float fromGrowth, float fromBloom)
{
    if (shown != Look::Vine)
    {
        goIdle(); // nobody's watching, skip the animation
        return;
    }
    tidyGrowth = fromGrowth;
    tidyBloom = fromBloom;
    tidyMs = RETRACT_MIN_MS + (unsigned long)((RETRACT_FULL_MS - RETRACT_MIN_MS) * fromGrowth);
    goTo(Stage::VineTidy);
}

// stage changes that come from the timer itself: one starting, or one being cancelled.
// One running out comes in through timerViewFinished() instead, in the same loop pass it
// happens, so by the next frame the stage has already moved on to its ending
static void followTimer()
{
    bool running = timerRunning();
    bool focus = timerKind() == TimerKind::Focus;

    switch (stage)
    {
    case Stage::Idle:
    case Stage::RingEnd:
    case Stage::VineEnd:
        // a new timer replaces whatever ending was up
        if (running)
        {
            endEnding();
            onBreak = !focus && breakWanted && timerDurationMs() == BREAK_MINUTES * 60000UL;
            breakWanted = false;
            goTo(focus ? Stage::Vine : Stage::Ring);
        }
        else
        {
            breakWanted = false; // the break was cancelled before its ring came up
        }
        break;

    case Stage::Ring:
        if (!running)
        {
            goIdle(); // cancelled
        }
        else if (focus)
        {
            goTo(Stage::Vine); // a focus session replaced it
        }
        break;

    case Stage::Vine:
        if (!running)
        {
            startTidy(growth, 0); // cancelled: pull it back in, no flowers
        }
        else if (!focus)
        {
            goTo(Stage::Ring); // a plain timer replaced it
        }
        break;

    case Stage::VineTidy:
    case Stage::VineSlide:
        break; // a timer started now (the break) waits for the pot to leave
    }
}

static void expireEndings()
{
    unsigned long now = millis();
    bool ending = stage == Stage::RingEnd || stage == Stage::VineEnd;
    if (ending && !endStarted && now - finishedAt >= PENDING_MS)
    {
        goIdle(); // he was busy the whole time, it's old news now
    }
    else if (stage == Stage::RingEnd && endStarted && now - endStart >= ALERT_MAX_MS)
    {
        goIdle();
    }
    else if (stage == Stage::VineEnd && endStarted && now - endStart >= BREAK_OFFER_MS)
    {
        Serial.println("no break then");
        startTidy(1, 1);
    }
}

static void changeLook(Look want)
{
    // the old look's last frame is still on screen, main wipes it once
    if (shown == Look::Ring)
    {
        clearBox = unite(clearPending ? clearBox : Rect{0, 0, 0, 0}, RING_BOX);
        clearPending = true;
    }
    else if (shown == Look::Vine)
    {
        clearBox = unite(clearPending ? clearBox : Rect{0, 0, 0, 0}, VINE_ALL);
        clearPending = true;
    }

    switch (want)
    {
    case Look::None:
        setFaceScale(1.0f, 0);
        setFaceShift(0);
        stopLooking();
        if (endFace)
        {
            setFaceStretch(1.0f); // the expression stays, whatever comes next sets its own
            endFace = false;
        }
        break;
    case Look::Ring:
        setFaceScale(RING_FACE_SCALE, RING_FACE_OFFSET_Y);
        setFaceShift(0);
        stopLooking();
        break;
    case Look::Vine:
        setFaceScale(VINE_FACE_SCALE, VINE_FACE_OFFSET_Y);
        setFaceShift(VINE_SHIFT_X);
        break;
    }

    shown = want;
    shownAt = millis();
    pushedSweep = -1; // nothing of the new look is on screen yet
    pushedPx = pushedBloom = pushedSlide = -1;
}

// the time's-up hop. Once it isn't hopping, the nudge is handed back
static void applyNudge()
{
    int hopY = 0;
    if (shown == Look::Ring && stage == Stage::RingEnd && endStarted)
    {
        unsigned long into = (millis() - endStart) % HOP_EVERY_MS;
        if (into < HOP_MS)
        {
            hopY = -(int)(HOP_PX * sinf(PI * into / (float)HOP_MS) + 0.5f);
        }
    }
    if (hopY != 0)
    {
        setFaceNudge(0, hopY);
        nudgeMine = true;
    }
    else if (nudgeMine)
    {
        setFaceNudge(0, 0);
        nudgeMine = false;
    }
}

static void updateRing()
{
    if (stage == Stage::RingEnd)
    {
        sweep = 360;
        lookAt(0, 0); // straight at you
    }
    else
    {
        unsigned long duration = timerDurationMs();
        sweep = duration == 0 ? 0 : (int)(360.0f * timerRemainingMs() / duration + 0.5f);
        stopLooking();
    }

    // push the whole ring while Steve is still shrinking (he's outside the middle box),
    // and whenever the ring has moved on a degree
    ringChanged = !settled() || sweep != pushedSweep;
    pushedSweep = sweep;
}

static void updateVine()
{
    unsigned long now = millis();
    switch (stage)
    {
    case Stage::Vine:
    {
        unsigned long duration = timerDurationMs();
        growth = duration == 0 ? 0 : 1.0f - timerRemainingMs() / (float)duration;
        bloom = 0;
        slide = 0;
        break;
    }
    case Stage::VineEnd:
        growth = 1;
        bloom = clamp01((now - endStart) / (float)BLOOM_MS);
        slide = 0;
        break;
    case Stage::VineTidy:
    {
        // the flowers close first, then the vine pulls in, overlapping a little
        float t = clamp01((now - stageStart) / (float)tidyMs);
        bloom = tidyBloom * (1 - clamp01(t / 0.35f));
        growth = tidyGrowth * (1 - easeInOut(clamp01((t - 0.25f) / 0.75f)));
        slide = 0;
        if (t >= 1)
        {
            goTo(Stage::VineSlide);
        }
        break;
    }
    case Stage::VineSlide:
    {
        // speeding up as it goes, like it's being pulled away
        float t = clamp01((now - stageStart) / (float)SLIDE_MS);
        growth = 0;
        bloom = 0;
        slide = (int)(SLIDE_PX * t * t);
        if (t >= 1)
        {
            goIdle(); // this frame still draws it at the edge, the next one wipes it
        }
        break;
    }
    default:
        break;
    }

    // his eyes follow the tip: up as it climbs, back down as it pulls in
    int tipX, tipY;
    vineTip(growth, tipX, tipY);
    float lookY = (tipY - (CENTER_Y + VINE_FACE_OFFSET_Y)) / 2.5f;
    lookY = lookY < LOOK_Y_MIN ? LOOK_Y_MIN : (lookY > LOOK_Y_MAX ? LOOK_Y_MAX : lookY);
    lookAt(VINE_LOOK_X, lookY);

    // the plant only needs pushing when it's changed by a whole pixel, or a flower has
    int px = vineGrowthPx(growth);
    int bloomStep = (int)(bloom * 100);
    vineChanged = !settled() || px != pushedPx || bloomStep != pushedBloom || slide != pushedSlide;
    pushedPx = px;
    pushedBloom = bloomStep;
    pushedSlide = slide;
}

void updateTimerView()
{
    followTimer();
    expireEndings();

    // compare the look that should be up with the one that is, and change only when they differ
    Look want = setAside ? Look::None : lookFor(stage);
    if (want != shown)
    {
        changeLook(want);
    }

    if (shown == Look::None)
    {
        // nobody can see a tidy-up, so finish it now
        if (stage == Stage::VineTidy || stage == Stage::VineSlide)
        {
            goIdle();
        }
        applyNudge(); // he may still be gliding back to the middle
        return;
    }

    // an ending that's just come on screen: start it, or put its face back after an answer
    if ((stage == Stage::RingEnd || stage == Stage::VineEnd) && !endFace)
    {
        if (!endStarted)
        {
            endStarted = true;
            endStart = millis();
        }
        if (stage == Stage::VineEnd)
        {
            setExpression(HAPPY);
        }
        else
        {
            setExpression(NEUTRAL);
            setFaceStretch(ALERT_STRETCH);
        }
        endFace = true;
    }

    if (shown == Look::Ring)
    {
        updateRing();
    }
    else
    {
        updateVine();
    }
    applyNudge();
}

static void drawTime(int x, int y)
{
    // rounded up, so it never shows 0:00 while still running
    unsigned long s = (timerRemainingMs() + 999) / 1000;
    char text[16];
    snprintf(text, sizeof(text), "%lu:%02lu", s / 60, s % 60);
    frame.setFreeFont(&FreeSansBold12pt7b);
    frame.setTextDatum(MC_DATUM);
    frame.setTextColor(TFT_WHITE);
    frame.drawString(text, x, y);
}

static void drawRing()
{
    // drawn every frame even when only the middle is pushed: the frame's clear wipes the
    // parts of the ring that cross the middle box, and the sprite has to stay complete
    frame.drawSmoothArc(RING_X, RING_Y, RING_R, RING_R - RING_THICK, 0, 360, TRACK, TFT_BLACK);
    if (sweep >= 360)
    {
        frame.drawSmoothArc(RING_X, RING_Y, RING_R, RING_R - RING_THICK, 0, 360, PLAIN_BLUE, TFT_BLACK);
    }
    else if (sweep > 0)
    {
        // from 12 o'clock clockwise. Angles have to stay within 0 to 360, so an arc that
        // passes 6 o'clock is drawn in two pieces
        int end = TOP + sweep;
        if (end <= 360)
        {
            frame.drawSmoothArc(RING_X, RING_Y, RING_R, RING_R - RING_THICK, TOP, end, PLAIN_BLUE, TRACK);
        }
        else
        {
            frame.drawSmoothArc(RING_X, RING_Y, RING_R, RING_R - RING_THICK, TOP, 360, PLAIN_BLUE, TRACK);
            frame.drawSmoothArc(RING_X, RING_Y, RING_R, RING_R - RING_THICK, 0, end - 360, PLAIN_BLUE, TRACK);
        }
    }

    if (stage == Stage::RingEnd)
    {
        // brightness pulsing between about half and full
        float wave = 0.5f + 0.5f * cosf(2 * PI * ((millis() - endStart) % PULSE_MS) / PULSE_MS);
        uint16_t colour = frame.alphaBlend((uint8_t)(130 + 125 * wave), TFT_WHITE, TFT_BLACK);
        frame.setFreeFont(&FreeSansBold12pt7b);
        frame.setTextDatum(MC_DATUM);
        frame.setTextColor(colour);
        frame.drawString(onBreak ? "Break's over!" : "Time's up!", RING_X, RING_TEXT_Y);
    }
    else
    {
        drawTime(RING_X, RING_TEXT_Y);
    }
}

static void drawVineLook(Rect pushed)
{
    // the plant is only drawn when its box is going out. Once Steve has settled he can't
    // reach it, so it can be cleared here too: main may only have cleared part of it
    if (overlaps(pushed, VINE_BOX))
    {
        if (settled())
        {
            frame.fillRect(VINE_BOX.x, VINE_BOX.y, VINE_BOX.w, VINE_BOX.h, TFT_BLACK);
        }
        drawVine(growth, bloom, slide);
    }

    frame.setTextDatum(MC_DATUM);
    if (stage == Stage::Vine)
    {
        drawTime(VINE_TEXT_X, VINE_TEXT_Y);
        frame.setFreeFont(&FreeSansBold9pt7b);
        frame.setTextColor(FOCUS_GREEN);
        frame.drawString("focus", VINE_TEXT_X, VINE_HINT_Y);
    }
    else if (stage == Stage::VineEnd)
    {
        frame.setFreeFont(&FreeSansBold12pt7b);
        frame.setTextColor(TFT_WHITE);
        frame.drawString("Break time!", VINE_TEXT_X, VINE_TEXT_Y);
        frame.setFreeFont(&FreeSansBold9pt7b);
        frame.setTextColor(HINT_GREY);
        frame.drawString("tap for 5 min", VINE_TEXT_X, VINE_HINT_Y);
    }
    // nothing under him while it's being tidied away
}

void drawTimerView(Rect pushed)
{
    if (shown == Look::Ring)
    {
        drawRing();
    }
    else if (shown == Look::Vine)
    {
        drawVineLook(pushed);
    }
}

bool timerViewActive()
{
    return shown != Look::None;
}

bool timerViewOwnsFace()
{
    return shown != Look::None && settled();
}

Rect timerViewRegion()
{
    if (shown == Look::Ring)
    {
        // the ring only moves a degree at a time, so the full box is only pushed when it has
        return ringChanged ? RING_BOX : MIDDLE_BOX;
    }
    if (shown == Look::Vine)
    {
        return vineChanged ? VINE_ALL : VINE_FACE_BOX;
    }
    return {0, 0, 0, 0};
}

bool timerViewTakeClear(Rect &out)
{
    if (!clearPending)
    {
        return false;
    }
    clearPending = false;
    out = clearBox;
    return true;
}

void timerViewSetAside(bool aside)
{
    setAside = aside;
}

void timerViewFinished(TimerKind kind)
{
    finishedAt = millis();
    endStarted = false;
    goTo(kind == TimerKind::Focus ? Stage::VineEnd : Stage::RingEnd);
}

bool timerViewTap()
{
    if (shown == Look::None)
    {
        return false; // not on screen, the tap is for whatever is
    }
    switch (stage)
    {
    case Stage::RingEnd:
        Serial.println("timer put away");
        goIdle();
        break;
    case Stage::VineEnd:
        Serial.printf("break: %lu min\n", BREAK_MINUTES);
        breakWanted = true;
        timerStart(TimerKind::Plain, BREAK_MINUTES); // counts from now, the ring shows once the pot's gone
        startTidy(1, 1);
        break;
    default:
        break; // counting down: nothing to do, but don't let it start something hidden behind the timer
    }
    return true;
}

bool timerViewDismiss()
{
    if (stage == Stage::RingEnd)
    {
        goIdle();
        return true;
    }
    if (stage == Stage::VineEnd)
    {
        startTidy(1, 1);
        return true;
    }
    return false;
}

bool timerViewBusy()
{
    return stage != Stage::Idle;
}