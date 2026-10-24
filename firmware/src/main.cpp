#include <Arduino.h>
#include <TFT_eSPI.h>
#include "net.h"
#include "pins.h"
#include "eyes.h"
#include "weather.h"
#include "status.h"
#include "console.h"
#include "ask.h"

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
    frame.fillRect(REGION_X, REGION_Y, REGION_W, REGION_H, TFT_BLACK);
    drawEyes();

#ifdef DEBUG_TIMING
    unsigned long drawDone = micros();
#endif

    // push only the box around the eyes
    frame.pushSprite(REGION_X, REGION_Y, REGION_X, REGION_Y, REGION_W, REGION_H);

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

    // only redraws the strip when its text changes
    updateStatus();
  }

  updateNetwork();

  char question[128];
  if (readLine(question, sizeof(question)))
  {
    if (!askSubmit(question))
    {
      Serial.println("Steve's busy, try again in a sec");
    }
  }
  static unsigned long seenVersion = 0;
  if (askVersion() != seenVersion)
  {
    seenVersion = askVersion();
    char reply[256];
    char expr[16];
    askLatest(reply, sizeof(reply), expr, sizeof(expr));
    setExpression(expressionFromName(expr));
    moodSetAt = millis();
    holdingMood = true;
    Serial.printf("Steve (%s): %s\n", expr, reply);
  }

  if(holdingMood == true && millis() - moodSetAt > MOOD_HOLD_MS){
    setExpression(NEUTRAL);
    holdingMood = false;
  }
}