#pragma once
#include "eyes.h" // Rect

void sceneShow(const char *name); // "rain", "sun", ... from the server, "none" or unknown hides it
void updateScene();               // once per frame, hides the scene when its time is up
void drawSceneBack();             // once per frame, before drawFace(): things behind Steve
void drawSceneFront();            // once per frame, after drawFace(): things in front of him
Rect sceneRegion();               // where the current scene draws, so main can push it
bool sceneTakeClear(Rect &out);   // true once after a scene ends, with the area to wipe
bool sceneActive();
void sceneHide();