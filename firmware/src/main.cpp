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
const int REGION_MARGIN_X = 45; // extra space around the eyes, for movement later
const int REGION_MARGIN_Y = 30;
// The smallest box that contains both eyes, plus the margin
const int REGION_X = CENTER_X - EYE_SPACING - EYE_WIDTH / 2 - REGION_MARGIN_X; // left edge
const int REGION_Y = CENTER_Y - EYE_HEIGHT / 2 - REGION_MARGIN_Y;              // top edge
const int REGION_W = (EYE_SPACING + EYE_WIDTH / 2 + REGION_MARGIN_X) * 2;      // width
const int REGION_H = EYE_HEIGHT + REGION_MARGIN_Y * 2;                         // height
constexpr uint16_t EYE_COLOR = TFT_PINK;

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
const int LOOK_RANGE_X = REGION_MARGIN_X; // can't look further than the margin!
const int LOOK_RANGE_Y = REGION_MARGIN_Y;
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

// expressions
struct Expression
{
  int lidInner;  // how far the top lid drops at the nose side (px)
  int lidOuter;  // how far it drops at the outside edge (px)
  int lidBottom; // how far the bottom lid rises (px)
};

const Expression NEUTRAL = {0, 0, 0};
const Expression ANGRY = {35, 0, 0}; // values in the same order as the struct
const Expression SAD = {0, 35, 0};
const Expression SLEEPY = {15, 15, 0};
const Expression HAPPY = {0, 0, 35};
const Expression SUSPICIOUS = {35, 35, 35};
Expression currentExpr = NEUTRAL;

const Expression ALL_EXPRS[] = { NEUTRAL, ANGRY, SAD, SLEEPY, HAPPY, SUSPICIOUS };
const int NUM_EXPRS = 6;
int exprIndex = 0;

Expression fromExpr = NEUTRAL;
Expression toExpr = NEUTRAL;
unsigned long exprStart = 0;
const int EXPR_MS = 250;   // how long a morph takes
unsigned long lastExprChange;
const int EXPR_CYCLE_MS = 3000;

const int LID_PAD = 2;

void drawEye(int cx, int cy, int h, bool isLeft);
void updateBlink();
void updateGaze();
void  updateExpression();
float easeInOut(float t);
int lerpInt(int a, int b, float t);
Expression lerpExpr(Expression a, Expression b, float t);
void setExpression(Expression next);
void cycleExpressions();

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
    unsigned long frameStart = micros(); // NEW
    updateBlink();
    cycleExpressions();
    updateExpression();
    updateGaze();
    frame.fillSprite(TFT_BLACK);
    drawEye(CENTER_X - EYE_SPACING, CENTER_Y, eyeHeight, true);
    drawEye(CENTER_X + EYE_SPACING, CENTER_Y, eyeHeight, false);

    unsigned long drawDone = micros(); // NEW

    // Push only the box around the eyes, to the same spot on screen
    frame.pushSprite(REGION_X, REGION_Y, REGION_X, REGION_Y, REGION_W, REGION_H);

    unsigned long pushDone = micros(); // NEW

    // Print once every 30 frames (~once a second) so Serial doesn't flood   NEW
    static int frameCount = 0;                                     // NEW
    frameCount++;                                                  // NEW
    if (frameCount >= 30)                                          // NEW
    {                                                              // NEW
      frameCount = 0;                                              // NEW
      Serial.printf("draw: %lu us  push: %lu us  total: %lu us\n", // NEW
                    drawDone - frameStart,                         // NEW
                    pushDone - drawDone,                           // NEW
                    pushDone - frameStart);                        // NEW
    } // NEW

    lastFrameTime = millis();
  }
}
void cycleExpressions(){
  if (millis() - lastExprChange >= EXPR_CYCLE_MS){
    exprIndex = (exprIndex + 1) % NUM_EXPRS;
    setExpression(ALL_EXPRS[exprIndex]);
    lastExprChange = millis();
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

void updateExpression(){
  float t = (millis() - exprStart) / (float)EXPR_MS;
  if (t >= 1)
    {
      t = 1;
    }
   currentExpr = lerpExpr(fromExpr, toExpr, easeInOut(t)); 
}



void drawEye(int cx, int cy, int h, bool isLeftEye)
{
  int leftDrop;
  int rightDrop;

  int left_edge_x = cx - EYE_WIDTH / 2 + lookX;
  int top_edge_y = cy - h / 2 + lookY;

  int right_edge_x = left_edge_x + EYE_WIDTH;
  int bottom_edge_y = top_edge_y + h;
  frame.fillSmoothRoundRect(left_edge_x, top_edge_y, EYE_WIDTH, h, EYE_RADIUS, EYE_COLOR);

  int lidLeft = left_edge_x - LID_PAD;
  int lidRight = right_edge_x + LID_PAD;
  int lidTop = top_edge_y - LID_PAD;

  if (isLeftEye)
  {
    leftDrop = currentExpr.lidOuter;
    rightDrop = currentExpr.lidInner;
  }
  else
  {

    leftDrop = currentExpr.lidInner;
    rightDrop = currentExpr.lidOuter;
  }
  frame.fillTriangle(lidLeft, lidTop,
                     lidRight, lidTop,
                     lidLeft, top_edge_y + leftDrop,
                     TFT_BLACK);

  // Triangle 2: TR, BR, BL
  frame.fillTriangle(lidRight, lidTop,
                     lidRight, top_edge_y + rightDrop,
                     lidLeft, top_edge_y + leftDrop,
                     TFT_BLACK);

  if (currentExpr.lidBottom > 0)
  {
    int rise = currentExpr.lidBottom;
    frame.fillRect(lidLeft, bottom_edge_y - rise,
                   lidRight - lidLeft, rise + LID_PAD,
                   TFT_BLACK);
  }
}

int lerpInt(int a, int b, float t){
  return a + (b - a) * t;
}

Expression lerpExpr(Expression a, Expression b, float t){
  return {lerpInt(a.lidInner, b.lidInner, t), lerpInt(a.lidOuter, b.lidOuter, t), lerpInt(a.lidBottom, b.lidBottom, t)};

}

void setExpression(Expression next){
  fromExpr = currentExpr;
  toExpr = next;
  exprStart = millis();
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