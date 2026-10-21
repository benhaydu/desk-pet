#include <Arduino.h>
#include <TFT_eSPI.h>
#include "net.h"
#include "pins.h"
#include "eyes.h"
#include "weather.h"

const int FRAME_INTERVAL_MS = 33;

// not static, eyes.cpp uses this through extern
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite frame = TFT_eSprite(&tft);

unsigned long lastFrameTime = 0;
static unsigned long lastTimePrint = 0;

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
}

void loop()
{
  if (millis() - lastFrameTime >= FRAME_INTERVAL_MS)
  {
    unsigned long frameStart = micros();

    updateEyes();
    frame.fillSprite(TFT_BLACK);
    drawEyes();

    unsigned long drawDone = micros();

    // push only the box around the eyes
    frame.pushSprite(REGION_X, REGION_Y, REGION_X, REGION_Y, REGION_W, REGION_H);

    unsigned long pushDone = micros();

    // timing once every 30 frames (~once a second)
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

    lastFrameTime = millis();
  }

  updateNetwork();

  if (millis() - lastTimePrint >= 10000)
  {
    lastTimePrint = millis();
    if (!timeIsValid())
    {
      Serial.println("time not synced");
    }
    else
    {
      struct tm now;
      getLocalTime(&now, 0);
      char buf[32];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &now);
      Serial.println(buf);
    }
  }

}