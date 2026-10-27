#pragma once

void replyShow(const char *text); // wrap the text and start showing it
void replyHide();                 // clear the text area
bool replyActive();               // still showing (or scrolling)?
void updateReply();               // every frame: works out the scroll position
void drawReply();                 // every frame: redraws and pushes only if something changed