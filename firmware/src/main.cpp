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
constexpr uint16_t EYE_COLOR = TFT_GREENYELLOW;

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

// for eye animations
const int LOOK_RANGE_X = REGION_MARGIN; // can't look further than the margin!
const int LOOK_RANGE_Y = REGION_MARGIN;
const int GLANCE_MS = 180;
const int MIN_GLANCE = 25;
enum GazeState
{
  HOLDING,
  MOVING
};
GazeState gazeState = HOLDING;

float lookX = 0, lookY = 0; // current offset from center
float fromX = 0, fromY = 0; // where this move started
float toX = 0, toY = 0;     // where it's going
unsigned long moveStart = 0;
unsigned long holdStart = 0;
unsigned long holdDelay = 0;

void drawEye(int cx, int cy, int h);
void updateBlink();
void updateGaze();
float easeInOut(float t);

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
  blinkDelay = random(2000, 6000);
}

void loop()
{
  if (millis() - lastFrameTime >= FRAME_INTERVAL_MS)
  {
    updateBlink();
    updateGaze();
    frame.fillSprite(TFT_BLACK);
    drawEye(CENTER_X - EYE_SPACING, CENTER_Y, eyeHeight);
    drawEye(CENTER_X + EYE_SPACING, CENTER_Y, eyeHeight);
    // Push only the box around the eyes, to the same spot on screen
    frame.pushSprite(REGION_X, REGION_Y, REGION_X, REGION_Y, REGION_W, REGION_H);

    lastFrameTime = millis();
  }
}

void updateGaze()
{

  switch (gazeState)
  {
  case HOLDING:
    if (millis() - holdStart >= holdDelay)
    {
      if (random(3) == 0 && lookX * lookX + lookY * lookY > 3 * 3)
      {
        toX = 0;
        toY = 0;
      }
      else
      {
        float dx;
        float dy;
        do
        {
          toX = random(-LOOK_RANGE_X, LOOK_RANGE_X + 1);
          toY = random(-LOOK_RANGE_Y, LOOK_RANGE_Y + 1);
          dx = toX - lookX;
          dy = toY - lookY;
        } while (dx * dx + dy * dy < MIN_GLANCE * MIN_GLANCE);
      }
      fromX = lookX;
      fromY = lookY;
      moveStart = millis();
      gazeState = MOVING;
    }
    break;
  case MOVING:
  {
    float t = (millis() - moveStart) / (float)GLANCE_MS;
    if (t >= 1)
    {
      t = 1;
      holdStart = millis();
      holdDelay = random(1000, 4000);
      gazeState = HOLDING;
    }

    float e = easeInOut(t);
    lookX = fromX + (toX - fromX) * e;
    lookY = fromY + (toY - fromY) * e;
    break;
  }
  }
}
float easeInOut(float t)
{
  return t * t * (3 - 2 * t);
}

void drawEye(int cx, int cy, int h)
{
  frame.fillSmoothRoundRect(cx - EYE_WIDTH / 2 + lookX, cy - h / 2 + lookY, EYE_WIDTH, h, EYE_RADIUS, EYE_COLOR);
}

void updateBlink()
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
      blinkDelay = random(2000, 6000);
      blinkState = OPEN;
    }
    break;
  }
  // unsigned long frameStart = micros();
}