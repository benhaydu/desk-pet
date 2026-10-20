#include <Arduino.h>
#include <TFT_eSPI.h>
#include "secrets.h"
#include "pins.h"

const int EYE_WIDTH = 70;
const int EYE_HEIGHT = 90;
const int EYE_RADIUS = 18;
const int EYE_SPACING = 65;
const int CENTER_X = 160;
const int CENTER_Y = 120;
constexpr uint16_t EYE_COLOR = TFT_CYAN;

const int FRAME_INTERVAL_MS = 33;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite frame = TFT_eSprite(&tft);

unsigned long lastFrameTime = 0;

void drawEye(int cx, int cy);

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
}

void loop()
{
  if (millis() - lastFrameTime >= FRAME_INTERVAL_MS)
  {
    frame.fillSprite(TFT_BLACK);
    drawEye(CENTER_X - EYE_SPACING, CENTER_Y);
    drawEye(CENTER_X + EYE_SPACING, CENTER_Y);
    frame.pushSprite(0, 0);
    lastFrameTime = millis();
  }
}

void drawEye(int cx, int cy)
{
  frame.fillSmoothRoundRect(cx - EYE_WIDTH / 2, cy - EYE_HEIGHT / 2, EYE_WIDTH, EYE_HEIGHT, EYE_RADIUS, EYE_COLOR);
}