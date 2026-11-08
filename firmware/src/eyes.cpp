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

// thinking: short eyes sweeping side to side, like he's scanning for the answer
static const int SCAN_RANGE_X = 40;   // how far left and right the sweep goes
static const int SCAN_Y = -12;        // a little above centre
static const float SCAN_SPEED = 2.2f; // radians per second, one sweep about every 3 s
static const float THINK_HEIGHT_SCALE = 0.5f;
static const unsigned long THINK_FADE_MS = 250;

static const int EXPR_MS = 250;                 // how long a morph takes
static const unsigned long SCALE_FADE_MS = 300; // how long a resize takes
static const float GAZE_FOLLOW_S = 0.12f;       // how quickly the eyes catch up to a scene's target

static const int MINI_CX = 292;
static const int MINI_CY = 20;
static const int MINI_WIDTH = 14;
static const int MINI_HEIGHT = 18;
static const int MINI_SPACING = 12;
static const int MINI_RADIUS = 4;
static const int MINI_MARGIN = 3;
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

// a scene can take over the gaze, the eyes ease toward its target
static bool gazeOverride = false;
static float overrideX = 0, overrideY = 0;
static unsigned long lastOverrideUpdate = 0;

// expression
static Expression currentExpr = NEUTRAL;
static Expression fromExpr = NEUTRAL;
static Expression toExpr = NEUTRAL;
static unsigned long exprStart = 0;

// thinking
static bool thinking = false;
static float thinkAmount = 0; // 0 = normal, 1 = fully thinking
static float fadeFrom = 0;    // where thinkAmount started its current fade
static unsigned long fadeStart = 0;
static unsigned long thinkStart = 0;

// face size, for scenes that need him smaller (the umbrella)
static float faceScale = 1.0f, faceOffsetY = 0;
static float scaleFrom = 1.0f, scaleTo = 1.0f;
static float offsetFrom = 0, offsetTo = 0;
static unsigned long scaleStart = 0;

// instant tweaks a scene sets every frame (the storm's startle and shake)
static int nudgeX = 0, nudgeY = 0;
static float faceStretch = 1.0f;

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
    {"thinking", THINKING},
};

static const size_t NUM_EXPRESSIONS = sizeof(EXPRESSIONS) / sizeof(EXPRESSIONS[0]);

// face mode, the switch waits for a blink so the swap is hidden
static FaceMode faceMode = FACE_FULL;
static bool switchPending = false;
static FaceMode pendingMode = FACE_FULL;
static bool needsFullRedraw = false;

static void updateBlink();
static void updateThinkAmount();
static void updateFaceScale();
static void updateGaze();
static void updateExpression();
static void drawEye(int cx, int cy,        // where the eye's centre is (offset already added)
                    int width, int height, // this frame's size (height already includes the blink)
                    int radius,
                    float lidScale, // 1.0 for big Steve, smaller when the face is shrunk
                    bool isLeftEye);
static float easeInOut(float t);
static int lerpInt(int a, int b, float t);
static void drawEyes();
static void drawMiniEyes();
static Expression lerpExpr(Expression a, Expression b, float t);

bool faceIsMini()
{
  return faceMode == FACE_MINI && !switchPending;
}

void setFaceMode(FaceMode mode)
{
  // asking for the mode we're already in cancels any switch that was waiting
  if (faceMode == mode)
  {
    switchPending = false;
    return;
  }
  pendingMode = mode;
  switchPending = true;
  if (blinkState == OPEN)
  {
    blinkState = CLOSING;
  }
}

void drawFace()
{
  if (faceMode == FACE_FULL)
  {
    drawEyes();
  }
  else
  {
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

bool faceTakeFullRedraw()
{
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
  updateThinkAmount(); // before the gaze, which uses it
  updateFaceScale();
  updateGaze();
}

static void drawEyes()
{
  // thinking squashes the eyes down, on top of whatever the blink is doing
  float thinkSquash = 1.0f - thinkAmount * (1.0f - THINK_HEIGHT_SCALE);

  // a scene can shrink the whole face, so size, spacing, gaze and lids all scale together
  int w = EYE_WIDTH * faceScale;
  int h = eyeHeight * thinkSquash * faceScale * faceStretch;
  int spacing = EYE_SPACING * faceScale;
  int radius = EYE_RADIUS * faceScale;
  int cx = CENTER_X + lookX * faceScale + nudgeX;
  int cy = CENTER_Y + faceOffsetY + lookY * faceScale + nudgeY;

  drawEye(cx - spacing, cy, w, h, radius, faceScale, true);
  drawEye(cx + spacing, cy, w, h, radius, faceScale, false);
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
  if (on == thinking)
  {
    return; // already there, don't restart the fade or the sweep
  }
  thinking = on;
  fadeFrom = thinkAmount;
  fadeStart = millis();

  if (on)
  {
    // remember where he was looking, the sweep fades in from here
    fromX = lookX;
    fromY = lookY;
    thinkStart = millis();
  }
  else
  {
    // glide back to centre, then the normal glances take over
    fromX = lookX;
    fromY = lookY;
    toX = 0;
    toY = 0;
    moveStart = millis();
    gazeState = MOVING;
  }
}

void setFaceScale(float scale, int offsetY)
{
  // start from wherever he is right now, even mid-resize
  scaleFrom = faceScale;
  offsetFrom = faceOffsetY;
  scaleTo = scale;
  offsetTo = offsetY;
  scaleStart = millis();
}

void setFaceNudge(int dx, int dy)
{
  nudgeX = dx;
  nudgeY = dy;
}

void setFaceStretch(float s)
{
  faceStretch = s;
}

void setGazeTarget(float x, float y)
{
  if (!gazeOverride)
  {
    gazeOverride = true;
    lastOverrideUpdate = millis();
  }
  // keep the target inside the range the region can show
  overrideX = constrain(x, -LOOK_RANGE_X, LOOK_RANGE_X);
  overrideY = constrain(y, -LOOK_RANGE_Y, LOOK_RANGE_Y);
}

void clearGazeTarget()
{
  if (!gazeOverride)
  {
    return; // safe to call every frame
  }
  gazeOverride = false;
  // glide back to centre, then the normal glances take over
  fromX = lookX;
  fromY = lookY;
  toX = 0;
  toY = 0;
  moveStart = millis();
  gazeState = MOVING;
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
      // eyes are shut, so this is the moment to swap faces without it showing
      if (switchPending)
      {
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
      if (switchPending)
      {
        blinkState = CLOSING; // a switch came in mid-blink, so blink again right away
      }
    }
    break;
  }
}

static void updateThinkAmount()
{
  float t = (millis() - fadeStart) / (float)THINK_FADE_MS;
  if (t > 1)
  {
    t = 1;
  }
  // starts from fadeFrom, so flipping thinking mid-fade carries on smoothly
  float target = thinking ? 1.0f : 0.0f;
  thinkAmount = fadeFrom + (target - fadeFrom) * easeInOut(t);
}

static void updateFaceScale()
{
  float t = (millis() - scaleStart) / (float)SCALE_FADE_MS;
  if (t > 1)
  {
    t = 1;
  }
  float e = easeInOut(t);
  faceScale = scaleFrom + (scaleTo - scaleFrom) * e;
  faceOffsetY = offsetFrom + (offsetTo - offsetFrom) * e;
}

static void updateGaze()
{
  if (thinking)
  {
    float t = (millis() - thinkStart) / 1000.0f;
    float scanX = SCAN_RANGE_X * sinf(t * SCAN_SPEED);
    // blend from where he was looking into the sweep while thinkAmount fades in
    lookX = fromX + (scanX - fromX) * thinkAmount;
    lookY = fromY + (SCAN_Y - fromY) * thinkAmount;
    return;
  }

  if (gazeOverride)
  {
    // close a fixed fraction of the gap per second, so a moving target is followed smoothly
    unsigned long now = millis();
    float dt = (now - lastOverrideUpdate) / 1000.0f;
    lastOverrideUpdate = now;
    if (dt > 0.1f)
    {
      dt = 0.1f; // after a long pause don't jump the whole way in one frame
    }
    float k = 1.0f - expf(-dt / GAZE_FOLLOW_S);
    lookX += (overrideX - lookX) * k;
    lookY += (overrideY - lookY) * k;
    return;
  }

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
                    float lidScale, // 1.0 for big Steve, smaller when the face is shrunk
                    bool isLeftEye)
{
  int left_edge_x = cx - width / 2;
  int top_edge_y = cy - height / 2;
  int right_edge_x = left_edge_x + width;
  int bottom_edge_y = top_edge_y + height;

  frame.fillSmoothRoundRect(left_edge_x, top_edge_y, width, height, radius, EYE_COLOR);

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