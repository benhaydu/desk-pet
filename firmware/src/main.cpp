#include <Arduino.h>
#include <TFT_eSPI.h>
#include "secrets.h"
#include "pins.h"

const int MAX_BRIGHTNESS = 80;      // tune this to taste
const int STEP_INTERVAL_MS = 10;    // how often brightness changes
const int DEBOUNCE_MS = 50;

TFT_eSPI tft = TFT_eSPI();

int mode = 0;

unsigned long lastPressTime = 0;
unsigned long lastUpdateTime = 0;
int brightness = 0;
int direction = 1;                  // +1 = getting brighter, -1 = getting dimmer
int lastButtonState = HIGH; // high = not pressed

void setup() {
  Serial.begin(115200);
  pinMode(PET_PIN_BUTTON, INPUT_PULLUP);
  tft.init();
  tft.setRotation(1);
   tft.fillScreen(TFT_BLACK);
  tft.drawString("desk-pet", 10, 10, 4);
}

void loop() {
  if (millis() - lastUpdateTime >= STEP_INTERVAL_MS) {
    brightness += direction;        // step up or down, depending on the sign

    if (brightness >= MAX_BRIGHTNESS || brightness <= 0) {
      direction = -direction;       // hit an end: turn around
    }

    switch (mode) {
  case 0:   // white
    rgbLedWrite(PET_PIN_RGB_LED, brightness, brightness, brightness);
    break;
  case 1:   // cyan
    rgbLedWrite(PET_PIN_RGB_LED, 0, brightness, brightness);
    break;
  case 2:   // magenta
    rgbLedWrite(PET_PIN_RGB_LED, brightness, 0, brightness);
    break;
}
    lastUpdateTime = millis();
  }

  int currentButtonState = digitalRead(PET_PIN_BUTTON);

  if(lastButtonState == HIGH && currentButtonState == LOW && millis() - lastPressTime >= DEBOUNCE_MS){
    mode = (mode + 1) % 3;
    lastPressTime = millis();
    Serial.println(mode);
  }

  lastButtonState = currentButtonState;
}