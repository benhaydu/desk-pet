#include <Arduino.h>
#include <TFT_eSPI.h>
#include "reply.h"

// the sprite lives in main.cpp
extern TFT_eSprite frame;

// where the text goes: below mini Steve, down to near the bottom of the screen
static const int TEXT_X = 12;
static const int TEXT_Y = 38;
static const int TEXT_W = 296;
static const int TEXT_H = 192;
static const int LINE_H = 22;
static const GFXfont *REPLY_FONT = &FreeSansBold9pt7b;

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

enum ReplyState
{
    REPLY_IDLE,      // nothing showing
    REPLY_PAUSE_TOP, // showing the start, giving you time to begin reading
    REPLY_SCROLLING, // moving up slowly
    REPLY_PAUSE_END, // reached the bottom, time to finish reading
    REPLY_DONE       // finished, waiting for main to hide it
};

static ReplyState replyState = REPLY_IDLE;
static unsigned long phaseStart = 0;
static int scrollY = 0;
static int maxScroll = 0;  // how far it can scroll (0 = it all fits)
static bool dirty = false; // something changed, so redraw and push

static const int PAUSE_TOP_MS = 3000;     // was 1500: time to start reading before anything moves
static const int SCROLL_PX_PER_SEC = 12;  // was 20: about one line every 2 seconds
static const int PAUSE_END_MS = 3000;     // was 2500: time to finish the last line

static const int SHORT_BASE_MS = 2000;
static const int SHORT_PER_CHAR_MS = 60;
static size_t textLen = 0;

void replyShow(const char *text)
{
    frame.setFreeFont(REPLY_FONT);

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

    int totalHeight = lineCount * LINE_H;
    maxScroll = totalHeight - TEXT_H;
    if (maxScroll < 0)
    {
        maxScroll = 0;
    }
    scrollY = 0;
    phaseStart = millis();
    textLen = strlen(text);
    replyState = REPLY_PAUSE_TOP;
    dirty = true;
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
    if (frame.textWidth(word) > TEXT_W)
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

    if (frame.textWidth(candidate) <= TEXT_W)
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

        if (frame.textWidth(candidate) > TEXT_W)
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

void updateReply()
{
    unsigned long time_elapsed = millis() - phaseStart;

    switch (replyState)
    {
    case REPLY_PAUSE_TOP:
        if (maxScroll == 0)
        {
            if (time_elapsed >= SHORT_BASE_MS + textLen * SHORT_PER_CHAR_MS)
            {
                replyState = REPLY_DONE;
            }
        }
        else
        {
            if (time_elapsed >= PAUSE_TOP_MS)
            {
                phaseStart = millis();
                replyState = REPLY_SCROLLING;
            }
        }
        break;

    case REPLY_SCROLLING:
    {
        int newY = time_elapsed * SCROLL_PX_PER_SEC / 1000;
        if (newY >= maxScroll)
        {
            newY = maxScroll;
            phaseStart = millis();
            replyState = REPLY_PAUSE_END;
        }
        if (newY != scrollY)
        {
            scrollY = newY;
            dirty = true;
        }
        break;
    }
    case REPLY_PAUSE_END:
        if (time_elapsed >= PAUSE_END_MS)
        {
            replyState = REPLY_DONE;
        }

        break;
    default:
        break;
    }
}

void drawReply()
{
    if (!dirty)
    {
        return;
    }
    frame.setViewport(TEXT_X, TEXT_Y, TEXT_W, TEXT_H, false);
    frame.fillRect(TEXT_X, TEXT_Y, TEXT_W, TEXT_H, TFT_BLACK);
    frame.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    frame.setTextDatum(TL_DATUM);
    frame.setFreeFont(REPLY_FONT);
    frame.setTextDatum(TL_DATUM);

    for (int i = 0; i < lineCount; i++)
    {
        int y = TEXT_Y + i * LINE_H - scrollY;
        if (y + LINE_H <= TEXT_Y)
        {
            continue;
        }
        else if (y >= TEXT_Y + TEXT_H)
        {
            continue;
        }
        frame.drawString(lines[i], TEXT_X, y);
    }

    frame.resetViewport();
    frame.pushSprite(TEXT_X, TEXT_Y, TEXT_X, TEXT_Y, TEXT_W, TEXT_H);
    dirty = false;
}
bool replyActive()
{
    // showing means anything between appearing and being finished
    return replyState != REPLY_IDLE && replyState != REPLY_DONE;
}

void replyHide()
{
    // blank the text area on screen, not just in the sprite
    frame.fillRect(TEXT_X, TEXT_Y, TEXT_W, TEXT_H, TFT_BLACK);
    frame.pushSprite(TEXT_X, TEXT_Y, TEXT_X, TEXT_Y, TEXT_W, TEXT_H);
    replyState = REPLY_IDLE;
    dirty = false;
}