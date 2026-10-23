#include "ask.h"
#include "net.h"
#include "secrets.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <HTTPClient.h>

static QueueHandle_t askQueue;

struct Question
{
    char text[128];
};
static void askTask(void *param);
static void sendQuestion(const char *text);

void askStart()
{
    askQueue = xQueueCreate(2, sizeof(Question));
    xTaskCreatePinnedToCore(
        askTask, // the function the task runs
        "ask",   // a name, only used for debugging
        8192,    // stack size in bytes
        nullptr, // the void* param handed to askTask (we don't need one)
        1,       // priority (1 is normal, same as the weather task)
        nullptr, // somewhere to store a handle to the task (we don't need it)
        0);      // which core: 0, so it stays off the animation core
}

bool askSubmit(const char *question)
{
    Question myQuestion;
    snprintf(myQuestion.text, sizeof(myQuestion.text), "%s", question);
    return xQueueSend(askQueue, &myQuestion, 0) == pdTRUE;
}

static void askTask(void *param)
{
    for (;;)
    {
        Question q;
        if (xQueueReceive(askQueue, &q, portMAX_DELAY) == pdTRUE)
        {
            Serial.printf("ask task got: %s (core %d)\n ", q.text, xPortGetCoreID());
            sendQuestion(q.text);
        }
    }
}

static void sendQuestion(const char* text)
{
    if (!isOnline())
    {
        Serial.println("offline, can't ask");
        return;
    }

    // from here on we know we're online, so no nesting needed
    JsonDocument req;
    req["text"] = text;
    String body;
    serializeJson(req, body);

    // next: WiFiClient, HTTPClient, begin, headers, timeout, POST...
    WiFiClient client;
    HTTPClient http;
    http.begin(client, SERVER_URL);

    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000);
    unsigned long startPost = millis();
    int status = http.POST(body);
    unsigned long timeToPost = millis() - startPost;
    if(status > 0){
        Serial.printf("Status %d and reply: %s\n",status, http.getString().c_str());
    }else{
        Serial.printf("request failed, status %d\n",status);
    }
    Serial.printf("time to post: %lu\n", timeToPost);
    http.end();
}   