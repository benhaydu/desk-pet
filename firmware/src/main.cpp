#include <Arduino.h>
#include <TFT_eSPI.h>
#include "net.h"
#include "pins.h"
#include "eyes.h"
#include "console.h"
#include "ask.h"
#include "headline.h"
#include "scene.h"
#include "touch.h"
#include "panel.h"
#include "player.h"
#include "reading.h"
#include "morning.h"
#include "sleep.h"

// #define DEBUG_TIMING   // uncomment to print frame timing once a second

const int FRAME_INTERVAL_MS = 33;
const int MOOD_HOLD_MS = 8000; // a bit longer than the longest headline (7 s)

// not static, the drawing modules (eyes, headline, scene, panel, reading, sleep) use it through extern
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite frame = TFT_eSprite(&tft);

unsigned long lastFrameTime = 0;

static Rect combine(Rect a, Rect b);

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
  static bool asking = false;      // a question is out, waiting for the answer
  static bool briefWanted = false; // /brief: play the next briefing that arrives

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

    // a scene or paper that just went away still has its last frame on screen, wipe it once
    Rect leftover;
    if (sceneTakeClear(leftover))
    {
      r = combine(r, leftover);
    }
    if (readingTakeClear(leftover))
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
    drawFace();
    drawReading();    // the paper he's holding, in front of his eyes
    drawSleep();      // closed eyes and z's at night, over everything else on his face
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

  // busy answering a question or playing something: the morning puts the paper down,
  // and at night he wakes up for it
  bool steveBusy = asking || holdingMood || headlineActive() || sceneActive() || playerActive();
  updateMorning(steveBusy);
  updateSleep(steveBusy);

  // a tap: wakes him a little at night, skips a card during the briefing,
  // or starts the briefing while he's reading the paper
  // && stops at the first false, so sleepTap() only runs when there was a tap
  if (touchTakePress() && !sleepTap())
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
    if (strcmp(question, "/mini") == 0)
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
      if (getLocalTime(&now))
      {
        char buf[32];
        strftime(buf, sizeof(buf), "%a %I:%M %p", &now);
        Serial.println(buf);
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
      setExpression(THINKING);
      setThinking(true);
    }
  }

  // a new answer arrived: react with the face, show the scene, label it with the headline
  static unsigned long seenVersion = 0;
  if (askVersion() != seenVersion)
  {
    seenVersion = askVersion();
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