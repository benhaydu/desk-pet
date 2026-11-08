#include <Arduino.h>
#include <TFT_eSPI.h>
#include "net.h"
#include "pins.h"
#include "eyes.h"
#include "weather.h"
#include "console.h"
#include "ask.h"
#include "headline.h"
#include "scene.h"
#include "touch.h"

// #define DEBUG_TIMING   // uncomment to print frame timing once a second

const int FRAME_INTERVAL_MS = 33;
const int MOOD_HOLD_MS = 8000; // a bit longer than the longest headline (7 s)

// not static, eyes.cpp, headline.cpp and scene.cpp use this through extern
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

  eyesInit();
  networkInit();
  weatherStart();
  askStart();
}

void loop()
{
  static unsigned long moodSetAt = 0;
  static bool holdingMood = false;

  if (millis() - lastFrameTime >= FRAME_INTERVAL_MS)
  {
    lastFrameTime = millis();

#ifdef DEBUG_TIMING
    unsigned long frameStart = micros();
#endif

    updateEyes();
    updateScene();

    // the face's box, grown to cover the scene while one is showing
    Rect r = faceRegion();
    if (sceneActive())
    {
      r = combine(r, sceneRegion());
    }

    // a scene that just ended still has its last frame on screen, wipe it once
    Rect leftover;
    if (sceneTakeClear(leftover))
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
    drawSceneFront(); // in front of Steve

#ifdef DEBUG_TIMING
    unsigned long drawDone = micros();
#endif

    // only push the part of the screen that's in use
    frame.pushSprite(r.x, r.y, r.x, r.y, r.w, r.h);

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

    // after the face push, so a full-screen clear can never wipe a fresh headline
    updateHeadline();
    drawHeadline();
  }

  updateNetwork();
  updateTouch();

    updateNetwork();
  updateTouch();

  // temporary: check each tap prints exactly once
  if (touchTakePress())
  {
    Serial.println("tap");
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
      // a new question clears the old answer and any reaction still being held
      headlineHide();
      sceneHide();
      holdingMood = false;
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

  // relax back to neutral once, after the hold runs out
  if (holdingMood && millis() - moodSetAt > MOOD_HOLD_MS)
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