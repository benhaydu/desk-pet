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
    char id[16];
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

// what's playing on Spotify, from the server's /now-playing
struct NowPlaying
{
    bool ok;                  // false = the check failed, which isn't the same as "nothing playing"
    bool playing;
    char title[64];           // the server caps these at 60 characters
    char artist[64];
    unsigned long progressMs; // how far into the song, when the server answered
    unsigned long durationMs;
    char artId[32];           // Spotify ids are 22 characters. Changes when the album does
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

// what's playing. Ask, wait for the version to change, then read it
bool nowPlayingRequest();
unsigned long nowPlayingVersion();
void nowPlayingLatest(NowPlaying &out);