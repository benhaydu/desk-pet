#include <Arduino.h>
#include <time.h>
#include "morning.h"
#include "ask.h"
#include "eyes.h"
#include "net.h"
#include "player.h"
#include "reading.h"

// times as minutes since midnight, in Ben's time zone
static const int PREFETCH_MIN = 8 * 60 + 50; // also wakes Render up for the 9:00 start
static const int START_MIN = 9 * 60;
static const int CUTOFF_MIN = 12 * 60;

// a briefing older than this gets fetched again when he taps, so a late tap gets today's news
static const time_t STALE_S = 60 * 60;

// three tries: 8:50, a minute later (Render is awake by then), then five minutes after that
static const int MAX_TRIES = 3;
static const unsigned long RETRY_MS[MAX_TRIES - 1] = {60000, 300000};

enum class Morning
{
    Idle,       // nothing today yet, or today's already done
    Fetching,   // asked for the briefing, waiting for it to arrive
    RetryWait,  // the last try failed, waiting to try again
    Ready,      // got it before 9:00, waiting for 9:00
    Waiting,    // reading the paper, waiting for a tap
    Refreshing, // tapped late, fetching a fresher one
    Starting,   // the paper is dropping, then the briefing plays
    Playing,
};

static Morning state = Morning::Idle;
static unsigned long stateStart = 0;

static Briefing briefing; // our own copy, so a /brief test can't swap it out
static time_t fetchedAt = 0;
static int tries = 0;
static bool queued = false;            // the fetch is actually in the ask queue
static unsigned long seenVersion = 0;  // the briefing version before we asked
static int doneDay = -1;               // which day the last morning finished, so it runs once a day
static bool forced = false;            // /morning: ignore the clock for this run
static bool tapped = false;

static void goTo(Morning next)
{
    state = next;
    stateStart = millis();
}

static unsigned long timeInState()
{
    return millis() - stateStart;
}

static void startFetch()
{
    // anything newer than this version is our answer
    seenVersion = briefingVersion();
    // the queue holds two, so this only fails if questions are piled up. Try again next loop
    queued = briefingRequest();
}

// true once the fetch has come back, with the result in out
static bool fetchArrived(Briefing &out)
{
    if (!queued)
    {
        startFetch();
        return false;
    }
    if (briefingVersion() == seenVersion)
    {
        return false;
    }
    seenVersion = briefingVersion();
    briefingLatest(out);
    return true;
}

static void finishDay(int today)
{
    doneDay = today;
    forced = false;
    goTo(Morning::Idle);
}

void updateMorning(bool steveBusy)
{
    // the clock, without blocking: getLocalTime() can wait up to 5 s if the time isn't set
    bool haveClock = timeIsValid();
    struct tm t = {};
    if (haveClock)
    {
        time_t now = time(nullptr);
        localtime_r(&now, &t);
    }
    int minutes = t.tm_hour * 60 + t.tm_min;
    int today = t.tm_year * 1000 + t.tm_yday; // one number per day, different every year
    bool weekday = t.tm_wday >= 1 && t.tm_wday <= 5;

    bool inMorning = forced || (haveClock && weekday && minutes >= PREFETCH_MIN && minutes < CUTOFF_MIN);
    bool startTime = forced || (haveClock && minutes >= START_MIN);
    bool tooLate = !forced && haveClock && minutes >= CUTOFF_MIN;

    switch (state)
    {
    case Morning::Idle:
        // a board switched on at 9:40 lands here too, so it still gets its briefing
        if (inMorning && (forced || today != doneDay))
        {
            tries = 0;
            Serial.println("morning: fetching the briefing");
            startFetch();
            goTo(Morning::Fetching);
        }
        break;

    case Morning::Fetching:
    {
        Briefing b;
        if (!fetchArrived(b))
        {
            break;
        }
        tries++;
        if (b.count > 0)
        {
            briefing = b;
            fetchedAt = time(nullptr);
            Serial.printf("morning: got %d cards\n", b.count);
            goTo(startTime ? Morning::Waiting : Morning::Ready);
        }
        else if (tries < MAX_TRIES)
        {
            Serial.printf("morning: try %d failed, trying again in %lu s\n", tries, RETRY_MS[tries - 1] / 1000);
            goTo(Morning::RetryWait);
        }
        else
        {
            Serial.println("morning: couldn't get the briefing, skipping today");
            finishDay(today);
        }
        break;
    }

    case Morning::RetryWait:
        if (tooLate)
        {
            finishDay(today);
        }
        else if (timeInState() >= RETRY_MS[tries - 1])
        {
            startFetch();
            goTo(Morning::Fetching);
        }
        break;

    case Morning::Ready:
        if (tooLate)
        {
            finishDay(today);
        }
        else if (startTime)
        {
            goTo(Morning::Waiting);
        }
        break;

    case Morning::Waiting:
        if (tooLate)
        {
            // no tap all morning: put the paper down quietly and try again tomorrow
            Serial.println("morning: no tap by noon, see you tomorrow");
            readingHide();
            finishDay(today);
            break;
        }
        if (tapped)
        {
            tapped = false;
            readingHide();
            if (time(nullptr) - fetchedAt > STALE_S)
            {
                Serial.println("morning: that one's old, getting a fresh one");
                setExpression(THINKING);
                setThinking(true);
                startFetch();
                goTo(Morning::Refreshing);
            }
            else
            {
                goTo(Morning::Starting);
            }
            break;
        }
        // put the paper down while he answers a question, pick it back up after
        if (steveBusy)
        {
            readingHide();
        }
        else
        {
            readingShow();
        }
        break;

    case Morning::Refreshing:
    {
        Briefing b;
        if (!fetchArrived(b))
        {
            break;
        }
        // a failed refresh still plays the morning one: older news beats no news
        if (b.count > 0)
        {
            briefing = b;
            fetchedAt = time(nullptr);
        }
        setThinking(false);
        goTo(Morning::Starting);
        break;
    }

    case Morning::Starting:
        // let the paper finish dropping, so it's off the screen before the first card
        if (!readingActive())
        {
            playerStart(briefing);
            goTo(Morning::Playing);
        }
        break;

    case Morning::Playing:
        if (!playerActive())
        {
            finishDay(today);
        }
        break;
    }
}

bool morningTap()
{
    if (state == Morning::Waiting)
    {
        tapped = true;
        return true;
    }
    return false;
}

bool morningWaiting()
{
    return state == Morning::Waiting;
}

void morningForce()
{
    if (state == Morning::Idle)
    {
        forced = true;
    }
}