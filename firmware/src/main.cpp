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
const int REGION_MARGIN = 20; // extra space around the eyes, for movement later

// The smallest box that contains both eyes, plus the margin
const int REGION_X = CENTER_X - EYE_SPACING - EYE_WIDTH / 2 - REGION_MARGIN; // left edge
const int REGION_Y = CENTER_Y - EYE_HEIGHT / 2 - REGION_MARGIN;              // top edge
const int REGION_W = (EYE_SPACING + EYE_WIDTH / 2 + REGION_MARGIN) * 2;      // width
const int REGION_H = EYE_HEIGHT + REGION_MARGIN * 2;                         // height
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
  tft.fillScreen(TFT_BLACK);
}

void loop()
{
  if (millis() - lastFrameTime >= FRAME_INTERVAL_MS)
  {
    // unsigned long frameStart = micros();

    frame.fillSprite(TFT_BLACK);
    drawEye(CENTER_X - EYE_SPACING, CENTER_Y);
    drawEye(CENTER_X + EYE_SPACING, CENTER_Y);
    // unsigned long drawDone = micros();

    // Push only the box around the eyes, to the same spot on screen
    frame.pushSprite(REGION_X, REGION_Y, REGION_X, REGION_Y, REGION_W, REGION_H);
    // unsigned long pushDone = micros();

    lastFrameTime = millis();
  }
}

void drawEye(int cx, int cy)
{
  frame.fillSmoothRoundRect(cx - EYE_WIDTH / 2, cy - EYE_HEIGHT / 2, EYE_WIDTH, EYE_HEIGHT, EYE_RADIUS, EYE_COLOR);
}