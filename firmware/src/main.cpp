#include <Arduino.h>
#include <TFT_eSPI.h>
#include "net.h"
#include "pins.h"
#include "eyes.h"
#include "console.h"
#include "ask.h"
#include "clocktime.h"
#include "headline.h"
#include "scene.h"
#include "touch.h"
#include "panel.h"
#include "player.h"
#include "reading.h"
#include "morning.h"
#include "sleep.h"
#include "timer.h"
#include "timerview.h"

// #define DEBUG_TIMING   // uncomment to print frame timing once a second

const int FRAME_INTERVAL_MS = 33;
const int MOOD_HOLD_MS = 8000;           // a bit longer than the longest headline (7 s)
const unsigned long MIN_THINK_MS = 1200; // a fast answer still gets a moment of thinking first

const unsigned long PROMPT_TIMEOUT_MS = 30000; // give up on "how many minutes?" after this
const long MIN_TIMER_MINUTES = 1;
const long MAX_TIMER_MINUTES = 180;

// not static, the drawing modules (eyes, headline, scene, panel, reading, sleep, timerview, vine) use it through extern
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite frame = TFT_eSprite(&tft);

unsigned long lastFrameTime = 0;

static Rect combine(Rect a, Rect b);
static long parseMinutes(const char *text);

void setup()
{
  Serial.begin(115200);
  touchInit();
  pinMode(PET_PIN_BUTTON, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  if (frame.createSprite(tft.width(), tft.height()) == nullptr)
  {
    Serial.println("Sprite allocation failed: not enough memory");
  }
  tft.fillScreen(TFT_BLACK);
  sleepInit(); // after tft.init(), which switches the backlight on

  eyesInit();
  networkInit();
  askStart();
}

void loop()
{
  static unsigned long moodSetAt = 0;
  static bool holdingMood = false;
  static bool asking = false;        // a question is out, until its answer is on screen
  static bool briefWanted = false;   // /brief: play the next briefing that arrives
  static bool wantThinking = false;  // start thinking as soon as his face is free
  static bool answerWaiting = false; // an answer arrived, show it once he's thought for a moment
  static unsigned long thinkingSince = 0;
  static bool askingMinutes = false; // waiting for the number after /timer or /focus
  static TimerKind askingFor = TimerKind::Plain;
  static unsigned long askedAt = 0;

  // nothing to draw while the screen is dark, so the chip gets to rest too
  if (!sleepScreenDark() && millis() - lastFrameTime >= FRAME_INTERVAL_MS)
  {
    lastFrameTime = millis();

#ifdef DEBUG_TIMING
    unsigned long frameStart = micros();
#endif

    updateEyes();
    updateScene();
    updateReading();
    updateTimerView();

    // the face's box, grown to cover the scene while one is showing
    Rect r = faceRegion();
    if (sceneActive())
    {
      r = combine(r, sceneRegion());
    }

    // and the newspaper, where it is now and where it was last frame
    Rect paper = readingRegion();
    if (readingActive() && paper.w > 0)
    {
      r = combine(r, paper);
    }

    // and the timer's ring or vine. Once Steve has settled into its layout, its own region
    // covers him, and it's smaller than his full-size box, so it replaces that
    Rect timerBox = timerViewRegion();
    if (timerViewOwnsFace())
    {
      r = timerBox;
    }
    else if (timerViewActive() && timerBox.w > 0)
    {
      r = combine(r, timerBox);
    }

    // a scene, paper or ring that just went away still has its last frame on screen, wipe it once
    Rect leftover;
    if (sceneTakeClear(leftover))
    {
      r = combine(r, leftover);
    }
    if (readingTakeClear(leftover))
    {
      r = combine(r, leftover);
    }
    if (timerViewTakeClear(leftover))
    {
      r = combine(r, leftover);
    }

    // right after a mode switch the old face is still on screen, so wipe everything once
    if (faceTakeFullRedraw())
    {
      r = {0, 0, 320, 240};
    }

    frame.fillRect(r.x, r.y, r.w, r.h, TFT_BLACK);
    drawSceneBack(); // behind Steve
    if (!sleepCoversFace())
    {
      drawFace(); // no point drawing open eyes that the closed ones would hide
    }
    drawTimerView(r); // the ring around him or the vine beside him
    drawReading();    // the paper he's holding, in front of his eyes
    drawSleep();      // closed eyes and z's at night
    drawSceneFront(); // in front of Steve

#ifdef DEBUG_TIMING
    unsigned long drawDone = micros();
#endif

    // only push the part of the screen that's in use
    frame.pushSprite(r.x, r.y, r.x, r.y, r.w, r.h);

    // after the face push, so a full-screen clear can never wipe fresh text
    updateHeadline();
    drawHeadline();
    updatePanel();
    drawPanel();

#ifdef DEBUG_TIMING
    unsigned long pushDone = micros();

    static int frameCount = 0;
    frameCount++;
    if (frameCount >= 30)
    {
      frameCount = 0;
      Serial.printf("draw: %lu us  push: %lu us  total: %lu us\n",
                    drawDone - frameStart,
                    pushDone - drawDone,
                    pushDone - frameStart);
    }
#endif
  }

  updateNetwork();
  updateTouch();
  updatePlayer();
  updateTimer();

  // a countdown just ran out: the view shows its ending
  if (timerTakeFinished())
  {
    Serial.printf("time's up (%s)\n", timerKind() == TimerKind::Focus ? "focus" : "timer");
    timerViewFinished(timerKind());
  }

  // nobody answered "how many minutes?", so drop it. This has to be out here, not in the
  // readLine block: that block only runs when something is typed
  if (askingMinutes && millis() - askedAt >= PROMPT_TIMEOUT_MS)
  {
    askingMinutes = false;
    Serial.println("never mind, no timer");
  }

  // answering a question or playing the briefing: his whole face is in use
  bool answering = asking || holdingMood || headlineActive() || sceneActive() || playerActive();

  // busy with that or a timer: the morning keeps the paper down, and at night he stays up
  // (or wakes up) for it
  bool steveBusy = answering || timerViewBusy();
  updateMorning(steveBusy);
  updateSleep(steveBusy);

  // the timer steps aside whenever Steve needs his whole face, and comes back after.
  // Not for the timer itself, or it would hide itself
  timerViewSetAside(answering || readingActive() || !sleepFaceFree());

  // at night his face is busy waking up first, so the thinking waits for it
  if (wantThinking && sleepFaceFree())
  {
    wantThinking = false;
    setExpression(THINKING);
    setThinking(true);
    thinkingSince = millis();
  }

  // a tap: wakes him a little at night, puts away a timer's ending (or starts the break),
  // skips a card during the briefing, or starts the briefing while he's reading the paper.
  // && stops at the first false, so each one only runs if the ones before didn't use the tap
  if (touchTakePress() && !sleepTap() && !timerViewTap())
  {
    if (playerActive())
    {
      playerSkip();
    }
    else if (!morningTap())
    {
      Serial.println("tap");
    }
  }

  // typed lines: /commands for testing, anything else goes to Steve
  char question[128];
  if (readLine(question, sizeof(question)))
  {
    if (askingMinutes)
    {
      // while asking, the line is the answer: not a command and not a question for Steve
      long minutes = parseMinutes(question);
      if (strcmp(question, "cancel") == 0)
      {
        askingMinutes = false;
        Serial.println("ok, no timer");
      }
      else if (minutes < 0)
      {
        askedAt = millis(); // they're answering, so give them another 30 s
        Serial.println("Please enter a whole number from 1 to 180");
      }
      else
      {
        askingMinutes = false;
        timerStart(askingFor, minutes);
        Serial.printf("%s: %ld min\n", askingFor == TimerKind::Focus ? "focus" : "timer", minutes);
      }
    }
    else if (strcmp(question, "/mini") == 0)
    {
      setFaceMode(FACE_MINI);
    }
    else if (strcmp(question, "/full") == 0)
    {
      setFaceMode(FACE_FULL);
    }
    else if (strcmp(question, "/head") == 0)
    {
      headlineShow("Hey Ben!");
    }
    else if (strcmp(question, "/headlong") == 0)
    {
      headlineShow("Locks stop two threads at once");
    }
    else if (strcmp(question, "/time") == 0)
    {
      // the board's own clock, to compare with the server's
      struct tm now;
      if (localNow(now))
      {
        char buf[32];
        strftime(buf, sizeof(buf), "%a %I:%M %p", &now);
        Serial.println(buf);
      }
      else
      {
        Serial.println("clock not set yet");
      }
    }
    else if (strcmp(question, "/brief") == 0)
    {
      // test only: fetch a fresh briefing and play it when it arrives
      if (briefingRequest())
      {
        briefWanted = true;
      }
      else
      {
        Serial.println("Steve's busy, try again in a sec");
      }
    }
    else if (strcmp(question, "/play") == 0)
    {
      // test only: replay the last briefing without asking the server again
      Briefing b;
      briefingLatest(b);
      if (b.count == 0)
      {
        Serial.println("no briefing yet, try /brief first");
      }
      else
      {
        holdingMood = false;
        playerStart(b);
      }
    }
    else if (strcmp(question, "/read") == 0)
    {
      // test only: the reading look on its own, /read again puts the paper down
      if (readingActive())
      {
        readingHide();
      }
      else
      {
        readingShow();
      }
    }
    else if (strcmp(question, "/morning") == 0)
    {
      // test only: the whole weekday morning right now, fetch, paper, tap, briefing.
      // It counts as today's, so the real one won't run again today
      morningForce();
    }
    else if (strcmp(question, "/sleep") == 0)
    {
      // test only: act like it's night, /auto goes back to the clock
      sleepForce(1);
    }
    else if (strcmp(question, "/wake") == 0)
    {
      // test only: act like it's daytime
      sleepForce(0);
    }
    else if (strcmp(question, "/auto") == 0)
    {
      sleepForce(-1);
    }
    else if (strcmp(question, "/yawn") == 0)
    {
      sleepYawn();
    }
    else if (strcmp(question, "/timer") == 0 || strcmp(question, "/focus") == 0)
    {
      askingFor = strcmp(question, "/focus") == 0 ? TimerKind::Focus : TimerKind::Plain;
      askingMinutes = true;
      askedAt = millis();
      if (askingFor == TimerKind::Focus)
      {
        Serial.println("How many minutes of focus? (1-180, or cancel)");
      }
      else
      {
        Serial.println("How many minutes? (1-180, or cancel)");
      }
    }
    else if (strcmp(question, "/cancel") == 0)
    {
      if (timerRunning())
      {
        timerCancel();
        Serial.println("cancelled");
      }
      else if (timerViewDismiss())
      {
        Serial.println("put away"); // "Time's up!" or the flowers, with nothing counting
      }
      else
      {
        Serial.println("no timer");
      }
    }
    else if (strcmp(question, "/left") == 0)
    {
      if (timerRunning())
      {
        // round up, so it shows 0:01 in the last second rather than 0:00 while still running
        unsigned long s = (timerRemainingMs() + 999) / 1000;
        Serial.printf("%lu:%02lu left\n", s / 60, s % 60);
      }
      else
      {
        Serial.println("no timer");
      }
    }
    else if (question[0] == '/')
    {
      // anything else starting with / is a scene test: /rain, /sun, /storm...
      // question + 1 skips the slash, an unknown name just hides the scene
      sceneShow(question + 1);
    }
    else if (!askSubmit(question))
    {
      Serial.println("Steve's busy, try again in a sec");
    }
    else
    {
      // a new question wraps up the briefing and clears the old answer
      playerStop();
      headlineHide();
      sceneHide();
      holdingMood = false;
      asking = true;
      answerWaiting = false;
      wantThinking = true; // the face reacts once it's free (the wantThinking block)
    }
  }

  // a new answer arrived: note it, it's shown below once he's ready
  static unsigned long seenVersion = 0;
  if (askVersion() != seenVersion)
  {
    seenVersion = askVersion();
    answerWaiting = true;
  }

  // react with the face, show the scene, label it with the headline. Only once he's been
  // thinking for a moment, so a fast answer (or one that arrives while he's still waking up)
  // doesn't flash past
  if (answerWaiting && !wantThinking && millis() - thinkingSince >= MIN_THINK_MS)
  {
    answerWaiting = false;
    Answer a;
    askLatest(a);

    asking = false;
    setThinking(false);
    setExpression(expressionFromName(a.expression));
    sceneShow(a.scene);

    // an empty headline (an older server) just means no text, the face still answers
    if (a.headline[0] != '\0')
    {
      headlineShow(a.headline);
    }

    moodSetAt = millis();
    holdingMood = true;

    Serial.printf("Steve (%s): %s\n", a.expression, a.reply);
    Serial.printf("  headline: %s  scene: %s  temp: %.1f\n", a.headline, a.scene, a.tempC);
  }

  // a new briefing arrived: always print it, but only play it here for /brief.
  // The morning watches for its own fetches and plays them itself
  static unsigned long seenBriefing = 0;
  if (briefingVersion() != seenBriefing)
  {
    seenBriefing = briefingVersion();
    Briefing b;
    briefingLatest(b);

    if (b.count == 0)
    {
      Serial.println("briefing came back empty, see the error above");
    }
    for (int i = 0; i < b.count; i++)
    {
      const Card &c = b.cards[i];
      Serial.printf("card %d: %s %s  scene=%s  expr=%s  \"%s\"\n",
                    i, c.id, c.panel ? "panel" : "face", c.scene, c.expression, c.title);
      for (int j = 0; j < c.lineCount; j++)
      {
        Serial.printf("    %s\n", c.lines[j]);
      }
    }

    if (briefWanted)
    {
      briefWanted = false;
      holdingMood = false;
      playerStart(b); // does nothing if it's empty
    }
  }

  // relax back to neutral once, after the hold runs out. The briefing sets its own faces
  if (holdingMood && !playerActive() && millis() - moodSetAt > MOOD_HOLD_MS)
  {
    setExpression(NEUTRAL);
    holdingMood = false;
  }

  // dark screen, nothing to animate: let the chip idle between passes instead of spinning.
  // 10 ms is still twice as often as the touch pad is read, so taps aren't missed
  if (sleepScreenDark())
  {
    delay(10);
  }
}

// the smallest box that covers both a and b
static Rect combine(Rect a, Rect b)
{
  int left = min(a.x, b.x);
  int top = min(a.y, b.y);
  int right = max(a.x + a.w, b.x + b.w);
  int bottom = max(a.y + a.h, b.y + b.h);
  return {left, top, right - left, bottom - top};
}

// the whole line as a number of minutes, or -1 if it isn't one.
// "10abc", "abc", "", "0" and "500" are all -1
static long parseMinutes(const char *text)
{
  char *end;
  long n = strtol(text, &end, 10);
  // end == text: no digits at all. *end != '\0': something after the digits
  if (end == text || *end != '\0' || n < MIN_TIMER_MINUTES || n > MAX_TIMER_MINUTES)
  {
    return -1;
  }
  return n;
}