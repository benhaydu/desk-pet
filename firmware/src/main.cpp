#include <Arduino.h>
#include <TFT_eSPI.h>
#include "secrets.h"
#include "pins.h"
#include "eyes.h"

const int FRAME_INTERVAL_MS = 33;

// Not static: eyes.cpp reaches this via "extern"
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

    // Push only the box around the eyes, to the same spot on screen
    frame.pushSprite(REGION_X, REGION_Y, REGION_X, REGION_Y, REGION_W, REGION_H);

    unsigned long pushDone = micros();

    // Print timing once every 30 frames (~once a second)
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
}