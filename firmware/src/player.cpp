#include <Arduino.h>
#include "player.h"
#include "eyes.h"
#include "headline.h"
#include "panel.h"
#include "scene.h"

// a short beat between two face cards, so the next headline reads as new
static const unsigned long GAP_MS = 400;

// panels hold for a base plus a bit per character, so a 3-line card gets longer than a 2-line one
static const unsigned long PANEL_BASE_MS = 3500;
static const unsigned long PANEL_PER_CHAR_MS = 60;
static const unsigned long PANEL_MAX_MS = 9000;

// a happy beat after the last card, so the briefing ends instead of just stopping
static const unsigned long CLOSING_MS = 2500;

enum class Step
{
    Idle,
    WaitFace,  // asked for the big or mini face, waiting for the blink that swaps it
    Hold,      // the card is up
    Leaving,   // the panel is sliding out, or the beat after a face card
    Finishing, // back to the big face after the last card
    Closing,   // the happy sign-off
};

static Briefing briefing; // our own copy, about 1.3 KB
static int current = 0;
static int panelTotal = 0; // how many of the cards are panels, for the progress dots
static Step step = Step::Idle;
static unsigned long stepStart = 0;
static unsigned long holdMs = 0;
static bool skipping = false;
static bool stopping = false;

static void goTo(Step next)
{
    step = next;
    stepStart = millis();
}

static unsigned long timeInStep()
{
    return millis() - stepStart;
}

static const Card &card()
{
    return briefing.cards[current];
}

// which panel this is, counting panels only: the Leafs card is panel 0 even though
// it's card 2, so the first dot lights up on the first panel
static int panelNumber(int cardIndex)
{
    int n = 0;
    for (int i = 0; i < cardIndex; i++)
    {
        if (briefing.cards[i].panel)
        {
            n++;
        }
    }
    return n;
}

static unsigned long panelHold(const Card &c)
{
    size_t chars = strlen(c.title);
    for (int i = 0; i < c.lineCount; i++)
    {
        chars += strlen(c.lines[i]);
    }
    unsigned long ms = PANEL_BASE_MS + PANEL_PER_CHAR_MS * chars;
    return ms > PANEL_MAX_MS ? PANEL_MAX_MS : ms;
}

static void waitForFace()
{
    // the swap happens at the next blink, so ask now and wait for it in WaitFace.
    // Asking for the mode we're already in does nothing, so panel to panel is instant
    setFaceMode(card().panel ? FACE_MINI : FACE_FULL);
    goTo(Step::WaitFace);
}

static void showCard()
{
    const Card &c = card();
    setExpression(expressionFromName(c.expression));
    if (c.panel)
    {
        panelShow(c, panelNumber(current), panelTotal);
        holdMs = panelHold(c);
    }
    else
    {
        sceneShow(c.scene);
        headlineShow(c.title);
    }
    skipping = false;
    goTo(Step::Hold);
}

static void leaveCard()
{
    if (card().panel)
    {
        panelHide();
    }
    else
    {
        headlineHide();
        sceneHide();
    }
    goTo(Step::Leaving);
}

static void finish()
{
    setFaceMode(FACE_FULL);
    goTo(Step::Finishing);
}

static void nextCard()
{
    current++;
    if (stopping || current >= briefing.count)
    {
        finish();
    }
    else
    {
        waitForFace();
    }
}

void playerStart(const Briefing &b)
{
    if (step != Step::Idle || b.count == 0)
    {
        return;
    }
    briefing = b;
    current = 0;
    stopping = false;
    skipping = false;

    panelTotal = 0;
    for (int i = 0; i < briefing.count; i++)
    {
        if (briefing.cards[i].panel)
        {
            panelTotal++;
        }
    }

    // whatever was on screen from the last answer makes way
    headlineHide();
    sceneHide();
    setThinking(false);
    waitForFace();
}

bool playerActive()
{
    return step != Step::Idle;
}

void playerSkip()
{
    // only while a card or the sign-off is up, a tap during a slide or a blink
    // would cut it in half
    if (step == Step::Hold || step == Step::Closing)
    {
        skipping = true;
    }
}

void playerStop()
{
    if (step != Step::Idle)
    {
        stopping = true;
    }
}

void updatePlayer()
{
    switch (step)
    {
    case Step::Idle:
        break;

    case Step::WaitFace:
    {
        if (stopping)
        {
            finish();
            break;
        }
        bool ready = card().panel ? faceIsMini() : faceIsFull();
        if (ready)
        {
            showCard();
        }
        break;
    }

    case Step::Hold:
    {
        // a face card lasts as long as its headline, which already holds longer for longer text
        bool timeUp = card().panel ? timeInStep() >= holdMs : !headlineActive();
        // never leave halfway through the slide in
        bool settled = !card().panel || !panelBusy();
        if (settled && (timeUp || skipping || stopping))
        {
            leaveCard();
        }
        break;
    }

    case Step::Leaving:
    {
        bool done = card().panel ? !panelBusy() : timeInStep() >= GAP_MS;
        if (done)
        {
            nextCard();
        }
        break;
    }

    case Step::Finishing:
        if (!faceIsFull())
        {
            break;
        }
        if (stopping)
        {
            // a question cut it short, so no sign-off: the thinking face takes over
            setExpression(NEUTRAL);
            goTo(Step::Idle);
            break;
        }
        setExpression(HAPPY);
        skipping = false;
        goTo(Step::Closing);
        break;

    case Step::Closing:
        if (timeInStep() >= CLOSING_MS || skipping || stopping)
        {
            setExpression(NEUTRAL);
            goTo(Step::Idle);
        }
        break;
    }
}