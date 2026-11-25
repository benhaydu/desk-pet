#include "ask.h"
#include "net.h"
#include "secrets.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// what the task should do with a queue item. Only this file needs to know,
// everyone else just calls askSubmit, briefingRequest or nowPlayingRequest
enum class RequestKind
{
    Ask,
    Briefing,
    NowPlaying
};

struct Question
{
    RequestKind kind;
    char text[128]; // only used by questions
};

static QueueHandle_t askQueue;

// one lock for all the results below, each is only held long enough to copy
static SemaphoreHandle_t answerLock;

// latest answer, shared between the ask task (core 0) and loop (core 1)
static Answer lastAnswer;
static unsigned long answerVersion = 0;

// latest briefing, same idea
static Briefing lastBriefing;
static unsigned long briefingCount = 0;

// what's playing on Spotify, polled every few seconds
static NowPlaying lastNowPlaying;
static unsigned long nowPlayingCount = 0;

static void askTask(void *param);
static void sendQuestion(const char *text);
static void fetchBriefing();
static void storeAnswer(const Answer &a);
static void storeFallback(const char *headline, const char *reply, const char *expr);
static void storeBriefing(const Briefing &b);
static void fetchNowPlaying();
static void storeNowPlaying(const NowPlaying &n);

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
    q.kind = RequestKind::Ask;
    snprintf(q.text, sizeof(q.text), "%s", question);
    // 0 = don't wait if the queue is full, loop() must never block
    return xQueueSend(askQueue, &q, 0) == pdTRUE;
}

bool briefingRequest()
{
    Question q;
    q.kind = RequestKind::Briefing;
    q.text[0] = '\0';
    return xQueueSend(askQueue, &q, 0) == pdTRUE;
}

unsigned long askVersion()
{
    xSemaphoreTake(answerLock, portMAX_DELAY);
    unsigned long v = answerVersion;
    xSemaphoreGive(answerLock);
    return v;
}

void askLatest(Answer &out)
{
    // copy while holding the lock so the task can't overwrite it halfway through
    xSemaphoreTake(answerLock, portMAX_DELAY);
    out = lastAnswer;
    xSemaphoreGive(answerLock);
}

unsigned long briefingVersion()
{
    xSemaphoreTake(answerLock, portMAX_DELAY);
    unsigned long v = briefingCount;
    xSemaphoreGive(answerLock);
    return v;
}

void briefingLatest(Briefing &out)
{
    xSemaphoreTake(answerLock, portMAX_DELAY);
    out = lastBriefing;
    xSemaphoreGive(answerLock);
}

bool nowPlayingRequest()
{
    Question q;
    q.kind = RequestKind::NowPlaying;
    q.text[0] = '\0';
    return xQueueSend(askQueue, &q, 0) == pdTRUE;
}

unsigned long nowPlayingVersion()
{
    xSemaphoreTake(answerLock, portMAX_DELAY);
    unsigned long v = nowPlayingCount;
    xSemaphoreGive(answerLock);
    return v;
}

void nowPlayingLatest(NowPlaying &out)
{
    xSemaphoreTake(answerLock, portMAX_DELAY);
    out = lastNowPlaying;
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
            if (q.kind == RequestKind::Briefing)
            {
                Serial.printf("ask task got: briefing (core %d)\n", xPortGetCoreID());
                fetchBriefing();
            }
            else if (q.kind == RequestKind::Ask)
            {
                Serial.printf("ask task got: %s (core %d)\n", q.text, xPortGetCoreID());
                sendQuestion(q.text);
            }
            else if (q.kind == RequestKind::NowPlaying)
            {
                // no print here: it runs every 5 seconds and would bury everything else
                fetchNowPlaying();
            }
        }
    }
}

// the only place that writes the shared answer
static void storeAnswer(const Answer &a)
{
    xSemaphoreTake(answerLock, portMAX_DELAY);
    lastAnswer = a;
    answerVersion++;
    xSemaphoreGive(answerLock);
}

// for the error cases: a canned line, no scene, no weather
static void storeFallback(const char *headline, const char *reply, const char *expr)
{
    Answer a;
    snprintf(a.headline, sizeof(a.headline), "%s", headline);
    snprintf(a.reply, sizeof(a.reply), "%s", reply);
    snprintf(a.expression, sizeof(a.expression), "%s", expr);
    snprintf(a.scene, sizeof(a.scene), "none");
    a.tempC = a.highC = a.lowC = NAN;
    storeAnswer(a);
}

// the only place that writes the shared briefing. A failed fetch still gets stored
// (with count 0) so whoever asked finds out it's over instead of waiting forever
static void storeBriefing(const Briefing &b)
{
    xSemaphoreTake(answerLock, portMAX_DELAY);
    lastBriefing = b;
    briefingCount++;
    xSemaphoreGive(answerLock);
}

// the only place that writes the shared now playing. Failures get stored too (with ok false),
// so loop() always hears back and knows it can ask again
static void storeNowPlaying(const NowPlaying &n)
{
    xSemaphoreTake(answerLock, portMAX_DELAY);
    lastNowPlaying = n;
    nowPlayingCount++;
    xSemaphoreGive(answerLock);
}

static void sendQuestion(const char *text)
{
    if (!isOnline())
    {
        Serial.println("offline, can't ask");
        storeFallback("Brain offline", "I'm offline right now. No wifi connection", "sad");
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
    http.begin(client, String(SERVER_URL) + "/ask");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Steve-Key", STEVE_KEY);
    http.setTimeout(65000); // Render's free tier can take up to a minute to wake up

    unsigned long postStart = millis();
    int status = http.POST(body);
    unsigned long postTime = millis() - postStart;

    if (status == 401)
    {
        Serial.println("ask rejected: STEVE_KEY doesn't match the server");
        http.end();
        storeFallback("Wrong key", "My key doesn't fit the door.", "suspicious");
        return;
    }

    if (status != 200)
    {
        // negative = never got a response (Render down, no Wi-Fi, timed out)
        Serial.printf("ask failed, status %d after %lu ms\n", status, postTime);
        http.end();
        storeFallback("Brain offline", "I can't reach my brain right now.", "sad");
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
        storeFallback("Scrambled", "Hmm, my thoughts got scrambled. Try again?", "suspicious");
        return;
    }

    if (!doc["reply"].is<const char *>() || !doc["expression"].is<const char *>())
    {
        Serial.println("ask reply missing fields");
        storeFallback("Scrambled", "Hmm, my thoughts got scrambled. Try again?", "suspicious");
        return;
    }

    Answer a;
    snprintf(a.reply, sizeof(a.reply), "%s", (const char *)doc["reply"]);
    snprintf(a.expression, sizeof(a.expression), "%s", (const char *)doc["expression"]);
    // scene and data are optional, so fall back if the server didn't send them
    snprintf(a.headline, sizeof(a.headline), "%s", doc["headline"] | "");
    snprintf(a.scene, sizeof(a.scene), "%s", doc["scene"] | "none");
    a.tempC = doc["data"]["temp_c"] | NAN;
    a.highC = doc["data"]["high_c"] | NAN;
    a.lowC = doc["data"]["low_c"] | NAN;
    storeAnswer(a);
}

static void fetchNowPlaying()
{
    // = {} zeroes everything, so ok is false and every failure below can store n as it is
    NowPlaying n = {};

    if (!isOnline())
    {
        storeNowPlaying(n); // quietly: it'd print every 5 seconds while Wi-Fi is down
        return;
    }

    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();
    http.begin(client, String(SERVER_URL) + "/now-playing");
    http.addHeader("X-Steve-Key", STEVE_KEY);
    // short: a poll that waits a minute is out of date by the time it answers.
    // If Render is asleep this one fails, and a later one finds it awake
    http.setTimeout(10000);

    unsigned long getStart = millis();
    int status = http.GET();
    unsigned long getTime = millis() - getStart;

    if (status != 200)
    {
        if (status == 401)
        {
            Serial.println("now playing rejected: STEVE_KEY doesn't match the server");
        }
        else
        {
            // 503 means Spotify needs logging in again (tools/spotify_login.py)
            Serial.printf("now playing failed, status %d after %lu ms\n", status, getTime);
        }
        http.end();
        storeNowPlaying(n);
        return;
    }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    if (err)
    {
        Serial.printf("now playing parse failed: %s\n", err.c_str());
        storeNowPlaying(n);
        return;
    }

    // | gives a default when a field is missing, so an older server can't crash this
    n.playing = doc["playing"] | false;
    snprintf(n.title, sizeof(n.title), "%s", doc["title"] | "");
    snprintf(n.artist, sizeof(n.artist), "%s", doc["artist"] | "");
    snprintf(n.artId, sizeof(n.artId), "%s", doc["art_id"] | "");
    n.progressMs = doc["progress_ms"] | 0UL;
    n.durationMs = doc["duration_ms"] | 0UL;

    n.ok = true; // only now: everything above worked
    storeNowPlaying(n);
}

static void fetchBriefing()
{
    // = {} zeroes every field, so count starts at 0 and every failure below
    // can just store b as it is
    Briefing b = {};

    if (!isOnline())
    {
        Serial.println("offline, can't fetch the briefing");
        storeBriefing(b);
        return;
    }

    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();
    http.begin(client, String(SERVER_URL) + "/briefing");
    http.addHeader("X-Steve-Key", STEVE_KEY);
    // fetched in the background before 9, so it can wait out a cold start. 65 s is about the
    // most setTimeout takes: it's a uint16_t, and anything over 65535 wraps around to a short wait
    http.setTimeout(65000);

    unsigned long getStart = millis();
    int status = http.GET();
    unsigned long getTime = millis() - getStart;

    if (status != 200)
    {
        if (status == 401)
        {
            Serial.println("briefing rejected: STEVE_KEY doesn't match the server");
        }
        else
        {
            Serial.printf("briefing failed, status %d after %lu ms\n", status, getTime);
        }
        http.end();
        storeBriefing(b);
        return;
    }

    String resp = http.getString();
    http.end();
    Serial.printf("briefing took %lu ms\n", getTime);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    if (err)
    {
        Serial.printf("briefing parse failed: %s\n", err.c_str());
        storeBriefing(b);
        return;
    }

    // a missing "cards" gives an empty array, so the loop just doesn't run
    for (JsonObject c : doc["cards"].as<JsonArray>())
    {
        // the server sends 7, but never write past the end of the array
        if (b.count == MAX_CARDS)
        {
            break;
        }

        const char *kind = c["kind"] | "";
        const char *title = c["title"] | "";

        // skip kinds this firmware doesn't know, so a newer server can't show junk
        bool panel = strcmp(kind, "panel") == 0;
        if (!panel && strcmp(kind, "face") != 0)
        {
            continue;
        }
        if (title[0] == '\0')
        {
            continue; // nothing to show
        }

        // fill the next free slot. If the card gets skipped below, count doesn't
        // move and the next card just overwrites this slot
        Card &card = b.cards[b.count];
        card.panel = panel;
        snprintf(card.id, sizeof(card.id), "%s", c["id"] | "");
        snprintf(card.scene, sizeof(card.scene), "%s", c["scene"] | "none");
        snprintf(card.expression, sizeof(card.expression), "%s", c["expression"] | "neutral");
        snprintf(card.title, sizeof(card.title), "%s", title);

        card.lineCount = 0;
        for (JsonVariant line : c["lines"].as<JsonArray>())
        {
            if (card.lineCount == MAX_CARD_LINES)
            {
                break;
            }
            const char *text = line | "";
            if (text[0] == '\0')
            {
                continue;
            }
            snprintf(card.lines[card.lineCount], sizeof(card.lines[0]), "%s", text);
            card.lineCount++;
        }

        // a panel with no lines would just be a title over an empty box
        if (card.panel && card.lineCount == 0)
        {
            continue;
        }

        b.count++;
    }

    Serial.printf("briefing has %d cards\n", b.count);
    storeBriefing(b);
}