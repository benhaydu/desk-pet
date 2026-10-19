#include <Arduino.h>
#include "secrets.h"
#include "pins.h"

const int MAX_BRIGHTNESS = 80;      // tune this to taste
const int STEP_INTERVAL_MS = 10;    // how often brightness changes

unsigned long lastUpdateTime = 0;
int brightness = 0;
int direction = 1;                  // +1 = getting brighter, -1 = getting dimmer

void setup() {
  Serial.begin(115200);
}

void loop() {
  if (millis() - lastUpdateTime >= STEP_INTERVAL_MS) {
    brightness += direction;        // step up or down, depending on the sign

    if (brightness >= MAX_BRIGHTNESS || brightness <= 0) {
      direction = -direction;       // hit an end: turn around
    }

    rgbLedWrite(PET_PIN_RGB_LED, brightness, brightness, brightness);
    lastUpdateTime = millis();
  }
}