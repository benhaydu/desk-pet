#pragma once
#include <Arduino.h>

// non-blocking line reader for the serial monitor
// returns true once a full line has been typed, and copies it into out
bool readLine(char *out, size_t outSize);