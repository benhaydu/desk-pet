#include <TFT_eSPI.h>
#include "eyes.h"

// the sprite is created in main.cpp
extern TFT_eSprite frame;

static const int MIN_EYE_HEIGHT = 6;
static const int BLINK_SPEED = 20;
static const int CLOSED_HOLD_MS = 40;
static const int EYE_RADIUS = 18;
static constexpr uint16_t EYE_COLOR = TFT_CYAN;
static const int LID_PAD = 2; // lids overshoot the eye a bit to hide the anti-aliased edge

static const int LOOK_RANGE_X = REGION_MARGIN_X; // can't look past the margin or it gets cut off
static const int LOOK_RANGE_Y = REGION_MARGIN_Y;
static const int GLANCE_MS = 180;
static const int MIN_GLANCE = 25;

static const int EXPR_MS = 250; // how long a morph takes

// thinking dots, sitting just under the eyes but still inside the pushed region
static const int DOT_Y = CENTER_Y + 62;
static const int DOT_SPACING = 20;
static const int DOT_RADIUS = 6;
static const unsigned long DOT_PULSE_MS = 1200;

static const int MINI_CX = 270;
static const int MINI_CY = 30;
static const int MINI_WIDTH = 24;
static const int MINI_HEIGHT = 30;
static const int MINI_SPACING = 22;
static const int MINI_RADIUS = 6;
static const float MINI_SCALE = MINI_HEIGHT / (float)EYE_HEIGHT;

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

static bool thinking = false;

static const int MINI_MARGIN = 4;

struct NamedExpression
{
  const char *name;
  Expression expr;
};

static const NamedExpression EXPRESSIONS[] = {
    {"neutral", NEUTRAL},
    {"happy", HAPPY},
    {"sad", SAD},
    {"sleepy", SLEEPY},
    {"angry", ANGRY},
    {"suspicious", SUSPICIOUS},
    {"thinking", SQUINT},
};

static bool switchPending = false;
static FaceMode pendingMode = FACE_FULL;

static const size_t NUM_EXPRESSIONS = sizeof(EXPRESSIONS) / sizeof(EXPRESSIONS[0]);

static FaceMode faceMode = FACE_FULL;
static bool needsFullRedraw = false;
static void updateBlink();
static void updateGaze();
static void updateExpression();
static void drawEye(int cx, int cy,        // where the eye's centre is (offset already added)
                    int width, int height, // this frame's size (height already includes the blink)
                    int radius,
                    float lidScale, // 1.0 for big Steve, MINI_SCALE for mini
                    bool isLeftEye);
static void drawThinkingDots();
static float easeInOut(float t);
static int lerpInt(int a, int b, float t);
static void drawEyes();
static void drawMiniEyes();
static Expression lerpExpr(Expression a, Expression b, float t);

void setFaceMode(FaceMode mode){
  if(faceMode == mode){
    switchPending = false;
    return;
  }
    pendingMode = mode;
    switchPending = true;
      if(blinkState == OPEN){
      blinkState = CLOSING;
    
  }
}
void drawFace(){
  if(faceMode == FACE_FULL){
    drawEyes();
  }else{
    drawMiniEyes();
  }
}

Rect faceRegion()
{
  if (faceMode == FACE_FULL)
  {
    return {REGION_X, REGION_Y, REGION_W, REGION_H};
  }

  // same shape as the big REGION_ formulas, just with the mini sizes
  int x = MINI_CX - MINI_SPACING - MINI_WIDTH / 2 - MINI_MARGIN;
  int y = MINI_CY - MINI_HEIGHT / 2 - MINI_MARGIN;
  int w = (MINI_SPACING + MINI_WIDTH / 2 + MINI_MARGIN) * 2;
  int h = MINI_HEIGHT + MINI_MARGIN * 2;
  return {x, y, w, h};
}

bool faceTakeFullRedraw(){
  bool local = needsFullRedraw;
  needsFullRedraw = false;
  return local;
}

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

static void drawEyes()
{
  drawEye(CENTER_X - EYE_SPACING + lookX, CENTER_Y + lookY,
        EYE_WIDTH, eyeHeight, EYE_RADIUS, 1.0f, true);
  drawEye(CENTER_X + EYE_SPACING + lookX, CENTER_Y + lookY,
        EYE_WIDTH, eyeHeight, EYE_RADIUS, 1.0f, false);
  // after both eyes so nothing draws over them
  if (thinking)
  {
    drawThinkingDots();
  }
}
static void drawMiniEyes()
{
  // same blink as big Steve, just as a fraction of the mini height
  int h = MINI_HEIGHT * (eyeHeight / (float)EYE_HEIGHT);

  // no lookX/lookY here, mini Steve stays put
  drawEye(MINI_CX - MINI_SPACING, MINI_CY,
          MINI_WIDTH, h, MINI_RADIUS, MINI_SCALE, true);
  drawEye(MINI_CX + MINI_SPACING, MINI_CY,
          MINI_WIDTH, h, MINI_RADIUS, MINI_SCALE, false);
}

void setExpression(Expression next)
{
  // start from wherever the face is right now, even mid-morph
  fromExpr = currentExpr;
  toExpr = next;
  exprStart = millis();
}

void setThinking(bool on)
{
  thinking = on;
}

Expression expressionFromName(const char *name)
{
  if (name == nullptr)
  {
    return NEUTRAL;
  }
  for (size_t i = 0; i < NUM_EXPRESSIONS; i++)
  {
    if (strcmp(EXPRESSIONS[i].name, name) == 0)
    {
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
      if(switchPending){
        faceMode = pendingMode;
        switchPending = false;
        needsFullRedraw = true;
      }
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

static void drawEye(int cx, int cy,        // where the eye's centre is (offset already added)
                    int width, int height, // this frame's size (height already includes the blink)
                    int radius,
                    float lidScale, // 1.0 for big Steve, MINI_SCALE for mini
                    bool isLeftEye)
{
  int left_edge_x = cx - width / 2;
  int top_edge_y = cy - height / 2;
  int right_edge_x = left_edge_x + width;
  int bottom_edge_y = top_edge_y + height;

  frame.fillSmoothRoundRect(left_edge_x, top_edge_y,width, height, radius, EYE_COLOR);

  int lidLeft = left_edge_x - LID_PAD;
  int lidRight = right_edge_x + LID_PAD;
  int lidTop = top_edge_y - LID_PAD;

  // inner/outer flip depending on which eye this is
  int leftDrop;
  int rightDrop;
  if (isLeftEye)
  {
    leftDrop = currentExpr.lidOuter * lidScale;
    rightDrop = currentExpr.lidInner * lidScale;
  }
  else
  {
    leftDrop = currentExpr.lidInner * lidScale;
    rightDrop = currentExpr.lidOuter * lidScale;
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
    int rise = currentExpr.lidBottom * lidScale;
    frame.fillRect(lidLeft, bottom_edge_y - rise,
                   lidRight - lidLeft, rise + LID_PAD,
                   TFT_BLACK);
  }
}

static void drawThinkingDots()
{
  for (int i = 0; i < 3; i++)
  {
    // each dot runs a little behind the one before it, which makes the wave
    float phase = (millis() / (float)DOT_PULSE_MS) - i * 0.15f;
    float brightness = 0.5f + 0.5f * sinf(phase * 2 * PI); // 0..1

    // never fully off, so the dots don't blink out completely
    uint8_t alpha = 60 + brightness * 195;
    uint16_t color = frame.alphaBlend(alpha, EYE_COLOR, TFT_BLACK);

    int x = CENTER_X + (i - 1) * DOT_SPACING; // -20, 0, +20
    frame.fillSmoothCircle(x, DOT_Y, DOT_RADIUS, color, TFT_BLACK);
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