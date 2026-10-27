#include <Arduino.h>
#include <TFT_eSPI.h>
#include "net.h"
#include "pins.h"
#include "eyes.h"
#include "weather.h"
#include "status.h"
#include "console.h"
#include "ask.h"
#include "reply.h"

// #define DEBUG_TIMING   // uncomment to print frame timing once a second

const int FRAME_INTERVAL_MS = 33;
const int MOOD_HOLD_MS = 8000;

// not static, eyes.cpp and status.cpp use this through extern
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
    else if (strcmp(question, "/wrap") == 0)
    {
      replyShow("A mutex is like a key to a single bathroom: whoever holds it gets in, "
                "and everyone else waits outside until it is handed back. That stops two "
                "tasks from changing the same data at once. "
                "Thisisaverylongwordthatcannotpossiblyfitononelineofthescreenatall and then normal text again.");
    }
    else if (!askSubmit(question))
    {
      Serial.println("Steve's busy, try again in a sec");
    }
    else
    {
      setExpression(SQUINT);
      setThinking(true);
    }
  }

  // a new answer arrived: react, then hold the mood for a bit
  static unsigned long seenVersion = 0;
  if (askVersion() != seenVersion)
  {
    seenVersion = askVersion();
    char reply[256];
    char expr[16];
    askLatest(reply, sizeof(reply), expr, sizeof(expr));
    setThinking(false);
    setExpression(expressionFromName(expr));
    moodSetAt = millis();
    holdingMood = true;
    Serial.printf("Steve (%s): %s\n", expr, reply);
  }

  // relax back to neutral once, after the hold runs out
  if (holdingMood && millis() - moodSetAt > MOOD_HOLD_MS)
  {
    setExpression(NEUTRAL);
    holdingMood = false;
  }
}