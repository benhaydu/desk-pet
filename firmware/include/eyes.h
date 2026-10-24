#pragma once
#include <Arduino.h>

// Eye size and placement. These live here because main needs the region for the push.
const int EYE_WIDTH = 70;
const int EYE_HEIGHT = 90;
const int EYE_SPACING = 65;
const int CENTER_X = 160;
const int CENTER_Y = 120;

// extra room around the eyes so they can glance around
const int REGION_MARGIN_X = 45;
const int REGION_MARGIN_Y = 30;

// smallest box that fits both eyes plus the margin, only this part gets pushed
const int REGION_X = CENTER_X - EYE_SPACING - EYE_WIDTH / 2 - REGION_MARGIN_X;
const int REGION_Y = CENTER_Y - EYE_HEIGHT / 2 - REGION_MARGIN_Y;
const int REGION_W = (EYE_SPACING + EYE_WIDTH / 2 + REGION_MARGIN_X) * 2;
const int REGION_H = EYE_HEIGHT + REGION_MARGIN_Y * 2;

struct Expression
{
  int lidInner;  // top lid drop on the nose side (px)
  int lidOuter;  // top lid drop on the outside edge (px)
  int lidBottom; // how far the bottom lid comes up (px)
};

const Expression NEUTRAL    = {0, 0, 0};
const Expression ANGRY      = {35, 0, 0};
const Expression SAD        = {0, 35, 0};
const Expression SLEEPY     = {15, 15, 0};
const Expression HAPPY      = {0, 0, 35};
const Expression SUSPICIOUS = {35, 35, 35};

Expression expressionFromName(const char* name);

void eyesInit();                     // call once in setup()
void updateEyes();                   // once per frame, before drawing
void drawEyes();                     // draws both eyes into the sprite
void setExpression(Expression next); // morphs from the current face to this one