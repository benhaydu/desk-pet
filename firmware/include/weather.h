#pragma once

bool fetchWeather();   // blocks until done, returns false if anything failed
float weatherTemp();   // last temperature we got, in C
int weatherCode();     // last WMO weather code