#pragma once
#include <Arduino.h>

void askStart();                       // once in setup()
bool askSubmit(const char *question);  // false if Steve's still busy

unsigned long askVersion();            // goes up every time a new answer lands
void askLatest(char *reply, size_t replySize,
               char *expr, size_t exprSize); // copies the latest answer out