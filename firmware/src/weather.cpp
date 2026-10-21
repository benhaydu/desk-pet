#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "net.h"
#include "weather.h"

// Victoria, BC
static const char *const WEATHER_URL =
    "https://api.open-meteo.com/v1/forecast?latitude=48.43&longitude=-123.37"
    "&current=temperature_2m,weather_code";

static float lastTemp = 0;
static int lastCode = 0;
static SemaphoreHandle_t weatherLock;
static const unsigned long WEATHER_REFRESH_MS = 15UL * 60 * 1000;
static const unsigned long WEATHER_FAILED_REFRESH_MS = 1UL * 60 * 1000;
static const unsigned long WAIT_FOR_WIFI_MS = 1000;
static unsigned long weatherVersion = 0;
static bool haveWeather = false;


static bool fetchWeather()
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
    float temp = doc["current"]["temperature_2m"];
    int code = doc["current"]["weather_code"];
    xSemaphoreTake(weatherLock, portMAX_DELAY);
    lastTemp = temp;
    lastCode = code;
    haveWeather = true;
    weatherVersion++;
    xSemaphoreGive(weatherLock);
    Serial.printf("weather: %.1f C, code %d\n", temp, code);
    return true;
}

bool weatherValid()
{
    xSemaphoreTake(weatherLock, portMAX_DELAY);
    bool valid = haveWeather;
    xSemaphoreGive(weatherLock);
    return valid;
}
unsigned long weatherVersionNow(){
    xSemaphoreTake(weatherLock, portMAX_DELAY);
    unsigned long v = weatherVersion;
    xSemaphoreGive(weatherLock);
    return v;
}

float weatherTemp()
{
    xSemaphoreTake(weatherLock, portMAX_DELAY);
    float t = lastTemp;          // t is a local, our own private copy
    xSemaphoreGive(weatherLock);
    return t;                    // safe: nobody else can touch t
}

int weatherCode()
{
    xSemaphoreTake(weatherLock, portMAX_DELAY);
    int c = lastCode;
    xSemaphoreGive(weatherLock);
    return c;
}

static void weatherTask(void *param)
{
    for (;;)
    {
        if (isOnline())
        {
            if (fetchWeather())
            {
                vTaskDelay(pdMS_TO_TICKS(WEATHER_REFRESH_MS));
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(WEATHER_FAILED_REFRESH_MS));
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(WAIT_FOR_WIFI_MS));
        }
    }
}

void weatherStart(){
    weatherLock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(weatherTask, "weather", 8192, nullptr, 1, nullptr, 0);
}