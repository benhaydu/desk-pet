#include "ask.h"
#include <Arduino.h>

static QueueHandle_t askQueue;

struct Question
{
    char text[128];
};
static void askTask(void *param);

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
        }
    }
}