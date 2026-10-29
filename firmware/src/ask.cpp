#include "ask.h"
#include "net.h"
#include "secrets.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

struct Question
{
    char text[128];
};

static QueueHandle_t askQueue;

// latest answer, shared between the ask task (core 0) and loop (core 1)
static char lastReply[256];
static char lastExpression[16];
static unsigned long answerVersion = 0;
static SemaphoreHandle_t answerLock;

static void askTask(void *param);
static void sendQuestion(const char *text);
static void storeAnswer(const char *reply, const char *expr);

void askStart()
{
    // queue and lock have to exist before the task starts using them
    askQueue = xQueueCreate(2, sizeof(Question));
    answerLock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(askTask, "ask", 8192, nullptr, 1, nullptr, 0);
}

bool askSubmit(const char *question)
{
    Question q;
    snprintf(q.text, sizeof(q.text), "%s", question);
    // 0 = don't wait if the queue is full, loop() must never block
    return xQueueSend(askQueue, &q, 0) == pdTRUE;
}

unsigned long askVersion()
{
    xSemaphoreTake(answerLock, portMAX_DELAY);
    unsigned long v = answerVersion;
    xSemaphoreGive(answerLock);
    return v;
}

void askLatest(char *reply, size_t replySize, char *expr, size_t exprSize)
{
    // copy into the caller's buffers while we hold the lock,
    // so the task can't overwrite them halfway through
    xSemaphoreTake(answerLock, portMAX_DELAY);
    snprintf(reply, replySize, "%s", lastReply);
    snprintf(expr, exprSize, "%s", lastExpression);
    xSemaphoreGive(answerLock);
}

static void askTask(void *param)
{
    for (;;)
    {
        Question q;
        // sleeps here until loop() sends something
        if (xQueueReceive(askQueue, &q, portMAX_DELAY) == pdTRUE)
        {
            Serial.printf("ask task got: %s (core %d)\n", q.text, xPortGetCoreID());
            sendQuestion(q.text);
        }
    }
}

// the only place that writes the shared answer
static void storeAnswer(const char *reply, const char *expr)
{
    xSemaphoreTake(answerLock, portMAX_DELAY);
    snprintf(lastReply, sizeof(lastReply), "%s", reply);
    snprintf(lastExpression, sizeof(lastExpression), "%s", expr);
    answerVersion++;
    xSemaphoreGive(answerLock);
}

static void sendQuestion(const char *text)
{
    if (!isOnline())
    {
        Serial.println("offline, can't ask");
        storeAnswer("I'm offline right now.", "sad");
        return;
    }

    // let ArduinoJson build the body so quotes in the question get escaped
    JsonDocument req;
    req["text"] = text;
    String body;
    serializeJson(req, body);

    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();
    http.begin(client, SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Steve-Key", STEVE_KEY);
    http.setTimeout(60000); // Render's free tier can take up to a minute to wake up

    unsigned long postStart = millis();
    int status = http.POST(body);
    unsigned long postTime = millis() - postStart;

    if (status == 401)
    {
        Serial.println("ask rejected: STEVE_KEY doesn't match the server");
        http.end();
        storeAnswer("My key doesn't fit the door.", "suspicious");
        return;
    }

    if (status != 200)
    {
       // negative = never got a response (Render down, no Wi-Fi, timed out)
        Serial.printf("ask failed, status %d after %lu ms\n", status, postTime);
        http.end();
        storeAnswer("I can't reach my brain right now.", "sad");
        return;
    }

    String resp = http.getString();
    http.end();
    Serial.printf("ask took %lu ms\n", postTime);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    if (err)
    {
        Serial.printf("ask reply parse failed: %s\n", err.c_str());
        storeAnswer("Hmm, my thoughts got scrambled. Try again?", "suspicious");
        return;
    }

    if (!doc["reply"].is<const char *>() || !doc["expression"].is<const char *>())
    {
        Serial.println("ask reply missing fields");
        storeAnswer("Hmm, my thoughts got scrambled. Try again?", "suspicious");
        return;
    }

    // these point into doc, which is fine because storeAnswer copies them right away
    const char *reply = doc["reply"];
    const char *expression = doc["expression"];
    storeAnswer(reply, expression);
}