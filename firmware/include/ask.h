#pragma once
#include <Arduino.h>

// everything the board gets back for one question
struct Answer
{
    char reply[256];
    char expression[16];
    char scene[16]; // "none" when there's nothing to draw
    float tempC;    // NAN when there was no weather lookup
    float highC;
    float lowC;
};

void askStart();                       // call once in setup()
bool askSubmit(const char *question);  // false if Steve's already busy
unsigned long askVersion();            // goes up by one every time a new answer lands
void askLatest(Answer &out);           // copies the newest answer into out