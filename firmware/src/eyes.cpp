#include <TFT_eSPI.h>
#include "eyes.h"

// the sprite is created in main.cpp
extern TFT_eSprite frame;

static const int MIN_EYE_HEIGHT = 6;
static const int BLINK_SPEED = 20;
static const int CLOSED_HOLD_MS = 40;
static const int EYE_RADIUS = 18;
static constexpr uint16_t EYE_COLOR = TFT_PINK;
static const int LID_PAD = 2; // lids overshoot the eye a bit to hide the anti-aliased edge

static const int LOOK_RANGE_X = REGION_MARGIN_X; // can't look past the margin or it gets cut off
static const int LOOK_RANGE_Y = REGION_MARGIN_Y;
static const int GLANCE_MS = 180;
static const int MIN_GLANCE = 25;

static const int EXPR_MS = 250;        // how long a morph takes


// blink
enum BlinkState
{
  OPEN,
  CLOSING,
  CLOSED,
  OPENING
};
static BlinkState blinkState = OPEN;
static int eyeHeight = EYE_HEIGHT;
static unsigned long openedAt = 0;
static unsigned long blinkDelay = 0;
static unsigned long closedSince = 0;

// gaze
enum GazeState
{
  HOLDING,
  MOVING
};
static GazeState gazeState = HOLDING;
static float lookX = 0, lookY = 0; // where the eyes are now, relative to center
static float fromX = 0, fromY = 0; // where the current glance started
static float toX = 0, toY = 0;     // where it's headed
static unsigned long moveStart = 0;
static unsigned long holdStart = 0;
static unsigned long holdDelay = 0;

// expression
static Expression currentExpr = NEUTRAL;
static Expression fromExpr = NEUTRAL;
static Expression toExpr = NEUTRAL;
static unsigned long exprStart = 0;


static void updateBlink();
static void updateGaze();
static void updateExpression();
static void drawEye(int cx, int cy, int h, bool isLeftEye);
static float easeInOut(float t);
static int lerpInt(int a, int b, float t);
static Expression lerpExpr(Expression a, Expression b, float t);


struct NamedExpression
{
    const char* name;
    Expression expr;
};

static const NamedExpression EXPRESSIONS[] = {
    {"neutral", NEUTRAL},
    {"happy", HAPPY},
    {"sad", SAD},
    {"sleepy", SLEEPY},
    {"angry", ANGRY},
    {"suspicious", SUSPICIOUS},
};

static const size_t NUM_EXPRESSIONS = sizeof(EXPRESSIONS) / sizeof(EXPRESSIONS[0]);

void eyesInit()
{
  openedAt = millis();
  blinkDelay = random(2000, 6000);
}

void updateEyes()
{
  updateBlink();
  updateExpression();
  updateGaze();
}

void drawEyes()
{
  drawEye(CENTER_X - EYE_SPACING, CENTER_Y, eyeHeight, true);
  drawEye(CENTER_X + EYE_SPACING, CENTER_Y, eyeHeight, false);
}

void setExpression(Expression next)
{
  // start from wherever the face is right now, even mid-morph
  fromExpr = currentExpr;
  toExpr = next;
  exprStart = millis();
}

Expression expressionFromName(const char* name)
{
    if(name == nullptr){
      return NEUTRAL;
    }
    for(size_t i = 0; i < NUM_EXPRESSIONS; i++){
        if(strcmp(EXPRESSIONS[i].name, name) == 0){
          return EXPRESSIONS[i].expr;
        }
    }
    // got all the way through without a match
    return NEUTRAL;
}


static void updateBlink()
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
}

static void updateGaze()
{
  switch (gazeState)
  {
  case HOLDING:
    if (millis() - holdStart >= holdDelay)
    {
      // sometimes look back to center, as long as we're not already there
      if (random(3) == 0 && lookX * lookX + lookY * lookY > 3 * 3)
      {
        toX = 0;
        toY = 0;
      }
      else
      {
        // keep picking until the target is far enough away to actually notice
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

static void updateExpression()
{
  float t = (millis() - exprStart) / (float)EXPR_MS;
  if (t >= 1)
  {
    t = 1;
  }
  currentExpr = lerpExpr(fromExpr, toExpr, easeInOut(t));
}


static void drawEye(int cx, int cy, int h, bool isLeftEye)
{
  int left_edge_x = cx - EYE_WIDTH / 2 + lookX;
  int top_edge_y = cy - h / 2 + lookY;
  int right_edge_x = left_edge_x + EYE_WIDTH;
  int bottom_edge_y = top_edge_y + h;

  frame.fillSmoothRoundRect(left_edge_x, top_edge_y, EYE_WIDTH, h, EYE_RADIUS, EYE_COLOR);

  int lidLeft = left_edge_x - LID_PAD;
  int lidRight = right_edge_x + LID_PAD;
  int lidTop = top_edge_y - LID_PAD;

  // inner/outer flip depending on which eye this is
  int leftDrop;
  int rightDrop;
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

  // top lid is two triangles split along the same diagonal so there's no gap
  frame.fillTriangle(lidLeft, lidTop,
                     lidRight, lidTop,
                     lidLeft, top_edge_y + leftDrop,
                     TFT_BLACK);
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

static float easeInOut(float t)
{
  return t * t * (3 - 2 * t);
}

static int lerpInt(int a, int b, float t)
{
  return a + (b - a) * t;
}

static Expression lerpExpr(Expression a, Expression b, float t)
{
  return {lerpInt(a.lidInner, b.lidInner, t),
          lerpInt(a.lidOuter, b.lidOuter, t),
          lerpInt(a.lidBottom, b.lidBottom, t)};
}