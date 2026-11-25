#pragma once
#include <stddef.h>
#include <stdint.h>

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
    bool paused;              // a song is up but paused, so a tap can start it again
    char title[64];           // the server caps these at 60 characters
    char artist[64];
    unsigned long progressMs; // how far into the song, when the server answered
    unsigned long durationMs;
    char artId[32];           // Spotify ids are 22 characters. Changes when the album does
    float bpm;                // the song's tempo, 0 if nobody knows it
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

// controlling Spotify: "play", "pause" or "next". Fire and forget, the next now playing
// check shows the result
bool playerRequest(const char *action);
unsigned long playerVersion();                    // goes up when a command has been sent, worked or not
bool playerLatest(char *reason, size_t reasonSize); // true if it worked. reason: "no_device", "refused", ...

// album covers from the server's /art: square, raw RGB565, the same kind of values as the team icons
constexpr int ART_SIZE = 120;
constexpr size_t ART_BYTES = ART_SIZE * ART_SIZE * sizeof(uint16_t);

bool artRequest(const char *artId); // download this album's cover in the background
unsigned long artVersion();         // goes up when a download finishes, even a failed one
// copies the last download out: its album id, and the pixels if it worked (pixels must hold
// ART_SIZE * ART_SIZE). False if that download failed
bool artLatest(char *artId, size_t idSize, uint16_t *pixels);