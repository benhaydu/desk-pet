#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "weather.h"

// Victoria, BC
static const char* const WEATHER_URL =
    "https://api.open-meteo.com/v1/forecast?latitude=48.43&longitude=-123.37"
    "&current=temperature_2m,weather_code";

static float lastTemp = 0;
static int lastCode = 0;

bool fetchWeather()
{
  WiFiClientSecure client;
  client.setInsecure(); // still encrypted, just not checking the cert. fine for public weather data

  HTTPClient http;
  http.begin(client, WEATHER_URL);
  int status = http.GET();

  if (status != 200)
  {
    // negative means we never got a reply at all (DNS, TLS, timeout...)
    Serial.printf("weather request failed, status %d\n", status);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end(); // have the text now, don't need the connection

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err)
  {
    Serial.print("weather JSON parse failed: ");
    Serial.println(err.c_str());
    return false;
  }

  // missing keys quietly turn into 0, so check it's actually there
  if (!doc["current"]["temperature_2m"].is<float>())
  {
    Serial.println("no temperature in weather reply");
    return false;
  }

  lastTemp = doc["current"]["temperature_2m"];
  lastCode = doc["current"]["weather_code"];
  Serial.printf("weather: %.1f C, code %d\n", lastTemp, lastCode);
  return true;
}

float weatherTemp()
{
  return lastTemp;
}

int weatherCode()
{
  return lastCode;
}