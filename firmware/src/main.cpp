#include <Arduino.h>
#include <TFT_eSPI.h>
#include "secrets.h"
#include "pins.h"

const int EYE_WIDTH = 70;
const int EYE_HEIGHT = 90;
const int MIN_EYE_HEIGHT = 6;
const int BLINK_SPEED = 30;

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
const int CLOSED_HOLD_MS = 40;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite frame = TFT_eSprite(&tft);

unsigned long lastFrameTime = 0;
unsigned long openedAt = 0;
unsigned long blinkDelay = 0;
unsigned long closedSince = 0;
int eyeHeight = EYE_HEIGHT;

enum BlinkState
{
  OPEN,
  CLOSING,
  CLOSED,
  OPENING
};
BlinkState blinkState = OPEN;

void drawEye(int cx, int cy, int h);

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
  openedAt = millis();
  blinkDelay =  random(2000, 6000);
}

void loop()
{
  if (millis() - lastFrameTime >= FRAME_INTERVAL_MS)
  {
    switch (blinkState)
    {
    case OPEN:
      if (millis() - openedAt >= blinkDelay)
      {
        blinkState = CLOSING;
      }
      break;

    case CLOSING:
      eyeHeight -= BLINK_SPEED;
      if (eyeHeight <= MIN_EYE_HEIGHT)
      {
        eyeHeight = MIN_EYE_HEIGHT;
        blinkState = CLOSED;
        closedSince = millis();
      }
      break;

    case CLOSED:
      if (millis() - closedSince >= CLOSED_HOLD_MS)
      {
        blinkState = OPENING;
      }
      break;

    case OPENING:
      eyeHeight += BLINK_SPEED;
      if (eyeHeight >= EYE_HEIGHT)
      {
        eyeHeight = EYE_HEIGHT;
        openedAt = millis();
        blinkDelay =  random(2000, 6000);
        blinkState = OPEN;
      }
      break;
    }
    // unsigned long frameStart = micros();

    frame.fillSprite(TFT_BLACK);
    drawEye(CENTER_X - EYE_SPACING, CENTER_Y, eyeHeight);
    drawEye(CENTER_X + EYE_SPACING, CENTER_Y, eyeHeight);
    // unsigned long drawDone = micros();

    // Push only the box around the eyes, to the same spot on screen
    frame.pushSprite(REGION_X, REGION_Y, REGION_X, REGION_Y, REGION_W, REGION_H);
    // unsigned long pushDone = micros();

    lastFrameTime = millis();
  }
}

void drawEye(int cx, int cy, int h)
{
  frame.fillSmoothRoundRect(cx - EYE_WIDTH / 2, cy - h / 2, EYE_WIDTH, h, EYE_RADIUS, EYE_COLOR);
}