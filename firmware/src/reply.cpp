#include <Arduino.h>
#include <TFT_eSPI.h>
#include "reply.h"

// the sprite lives in main.cpp
extern TFT_eSprite frame;

// where the text goes: below mini Steve, down to near the bottom of the screen
static const int TEXT_X = 12;
static const int TEXT_Y = 56;
static const int TEXT_W = 296;
static const int TEXT_H = 174;
static const int LINE_H = 20;
static const int TEXT_FONT = 2;

static const int MAX_LINES = 20; // a 256-char reply wraps to ~8, so this is plenty
static const int LINE_LEN = 100;

// the finished lines
static char lines[MAX_LINES][LINE_LEN];
static int lineCount = 0;

// the line currently being built, before it's finished
static char current[LINE_LEN];

static void finishLine();
static void addWord(const char *word);
static void addLongWord(const char *word);

void replyShow(const char *text)
{
    // start fresh
    lineCount = 0;
    current[0] = '\0';

    // walk through the text, cutting out one word at each space (and at the end)
    int start = 0;
    for (int i = 0;; i++)
    {
        if (text[i] == ' ' || text[i] == '\0')
        {
            if (i > start)
            {
                char word[LINE_LEN];
                snprintf(word, sizeof(word), "%.*s", i - start, text + start);
                addWord(word);
            }

            start = i + 1;

            if (text[i] == '\0')
            {
                break;
            }
        }
    }
    finishLine();

    // test print, delete once drawing works
    for (int i = 0; i < lineCount; i++)
    {
        Serial.printf("%2d [%s] %d px\n", i, lines[i], frame.textWidth(lines[i], TEXT_FONT));
    }
}

// copies current into the next slot in lines and empties current
static void finishLine()
{
    if (current[0] == '\0')
    {
        return;
    }
    if (lineCount < MAX_LINES)
    {
        snprintf(lines[lineCount], sizeof(lines[lineCount]), "%s", current);
        lineCount++;
    }
    current[0] = '\0'; // start the next line empty
}

static void addWord(const char *word)
{
    // a word that can't fit even on a line by itself gets split up instead
    if (frame.textWidth(word, TEXT_FONT) > TEXT_W)
    {
        addLongWord(word);
        return;
    }

    // try the line with this word added, without touching current yet
    char candidate[LINE_LEN];
    if (current[0] == '\0')
    {
        snprintf(candidate, sizeof(candidate), "%s", word);
    }
    else
    {
        snprintf(candidate, sizeof(candidate), "%s %s", current, word);
    }

    if (frame.textWidth(candidate, TEXT_FONT) <= TEXT_W)
    {
        // it fits, keep it
        snprintf(current, sizeof(current), "%s", candidate);
    }
    else
    {
        // it doesn't, so the old line is done and this word starts the next one
        finishLine();
        snprintf(current, sizeof(current), "%s", word);
    }
}

// rare: a single "word" wider than the whole line, like a leftover link
static void addLongWord(const char *word)
{
    // a long word always starts on a fresh line
    finishLine();

    for (int i = 0; word[i] != '\0'; i++)
    {
        // try the line with one more character
        char candidate[LINE_LEN];
        snprintf(candidate, sizeof(candidate), "%s%c", current, word[i]);

        if (frame.textWidth(candidate, TEXT_FONT) > TEXT_W)
        {
            // that character would go past the edge, so finish this line
            // and start the next one with just that character
            finishLine();
            snprintf(candidate, sizeof(candidate), "%c", word[i]);
        }

        snprintf(current, sizeof(current), "%s", candidate);
    }

    // whatever's left of the word stays in current, so the next normal
    // word can carry on from it on the same line
}