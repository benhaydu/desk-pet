#pragma once

// talks to Steve's server on a background task, so loop() never waits on the network

struct Answer
{
    char reply[256];
    char expression[16];
    char headline[40];
    char scene[16];
    float tempC;
    float highC;
    float lowC;
};

// sized from the limits in server/briefing.py, plus room for the '\0'
constexpr int MAX_CARDS = 8;
constexpr int MAX_CARD_LINES = 3;

struct Card
{
    bool panel;          // false = face card (headline + scene), true = panel (title + lines)
    char scene[16];
    char expression[16];
    char title[32];
    char lines[MAX_CARD_LINES][28];
    int lineCount;
};

struct Briefing
{
    Card cards[MAX_CARDS];
    int count;           // 0 means the fetch failed
};

void askStart();

// questions
bool askSubmit(const char *question);
unsigned long askVersion();
void askLatest(Answer &out);

// the morning briefing
bool briefingRequest();
unsigned long briefingVersion();
void briefingLatest(Briefing &out);