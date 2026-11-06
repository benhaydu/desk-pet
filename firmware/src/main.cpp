#include <Arduino.h>
#include <TFT_eSPI.h>
#include "net.h"
#include "pins.h"
#include "eyes.h"
#include "weather.h"
#include "console.h"
#include "ask.h"
#include "headline.h"

// #define DEBUG_TIMING   // uncomment to print frame timing once a second

const int FRAME_INTERVAL_MS = 33;
const int MOOD_HOLD_MS = 8000; // a bit longer than the longest headline (7 s)

// not static, eyes.cpp and headline.cpp use this through extern
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite frame = TFT_eSprite(&tft);

unsigned long lastFrameTime = 0;

void setup()
{
  Serial.begin(115200);
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

    Rect r = faceRegion();

    // right after a mode switch the old face is still on screen, so wipe everything once
    if (faceTakeFullRedraw())
    {
      r = {0, 0, 320, 240};
    }

    frame.fillRect(r.x, r.y, r.w, r.h, TFT_BLACK);
    drawFace();

#ifdef DEBUG_TIMING
    unsigned long drawDone = micros();
#endif

    // only push the part of the screen the face is using
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
      headlineShow("Hey Ben!"); // short, should get the biggest font
    }
    else if (strcmp(question, "/headlong") == 0)
    {
      headlineShow("Locks stop two threads at once"); // 30 chars, should drop a size or two
    }
    else if (!askSubmit(question))
    {
      Serial.println("Steve's busy, try again in a sec");
    }
    else
    {
      // a new question clears the old answer and any reaction still being held
      headlineHide();
      holdingMood = false;
      setExpression(THINKING);
      setThinking(true);
    }
  }

  // a new answer arrived: react with the face, label it with the headline
  static unsigned long seenVersion = 0;
  if (askVersion() != seenVersion)
  {
    seenVersion = askVersion();
    Answer a;
    askLatest(a);

    setThinking(false);
    setExpression(expressionFromName(a.expression));

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