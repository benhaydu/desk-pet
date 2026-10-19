#include <Arduino.h>
#include "secrets.h"
#include "pins.h"

// put function declarations here:
int myFunction(int, int);

void setup() {
Serial.begin(115200);
}

void loop() {
Serial.println("Hello world");
delay(1000);
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}