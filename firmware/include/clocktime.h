#pragma once
#include <time.h>

// the local time, read from the system clock at most once a second.
// Returns false until NTP has set the clock, and out is then all zeros
bool localNow(struct tm &out);