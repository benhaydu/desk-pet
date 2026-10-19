#include <Arduino.h>
#include "secrets.h"
#include "pins.h"

bool ledOn = false;
unsigned long last_toggle_time = 0;

void setup() {
  Serial.begin(115200);
}

void loop() {
if (millis() - last_toggle_time >= 500){
  if(ledOn){
    rgbLedWrite(PET_PIN_RGB_LED, 0, 0, 0);
      ledOn = false;
      Serial.println("LED OF");
  } else {
    rgbLedWrite(PET_PIN_RGB_LED, 255, 255, 255);
      ledOn = true;
      Serial.println("LED ON");
    }
    last_toggle_time = millis();
  }
}
