#include <Arduino.h>
#include <TFT_eSPI.h>
#include "ask.h"
#include "colors.h"
#include "eyes.h"
#include "music.h"
#include "timer.h"
#include "timerview.h"

extern TFT_eSprite frame;

static constexpr uint16_t GREY = rgb565(150, 150, 150);
static constexpr uint16_t TRACK = rgb565(60, 60, 60);
static constexpr uint16_t PLACEHOLDER = rgb565(40, 40, 40);
static constexpr uint16_t NOTE = rgb565(90, 90, 90);
static constexpr uint16_t HEADBAND = rgb565(80, 80, 92);
static constexpr uint16_t CUPS = rgb565(105, 105, 120);
static constexpr uint16_t NO_ACCENT = rgb565(190, 190, 190); // for black and white covers

// the layout. Steve's box never overlaps the other three, so most frames only push his
static const int ART_X = 12, ART_Y = 10;
static const Rect ART_BOX = {ART_X, ART_Y, ART_SIZE, ART_SIZE};
static const Rect STEVE_BOX = {150, 4, 170, 130};
static const Rect TEXT_BOX = {0, 138, 320, 58};
static const Rect BAR_BOX = {0, 196, 320, 40};
static const Rect ALL = {0, 0, 320, 240};

static const int TEXT_X = 12;
static const int TEXT_W = 296;
static const int BAR_X = 12, BAR_Y = 202, BAR_W = 296, BAR_H = 7;
static const int TIMES_Y = 223;

// Steve while listening: small, over on the right, centred at x 236, y 78
static const float FACE_SCALE = 0.48f;
static const int FACE_SHIFT = 76;
static const int FACE_OFFSET_Y = -42;

// the headphones, in unscaled sizes, so they grow and shrink with him
static const float BAND_R = 130;
static const float BAND_THICK = 13;
static const float CUP_W = 26, CUP_H = 56, CUP_DROP = 4;

// bobbing along to the song's tempo, which the server looks up. Fast songs get a bob every
// other beat (half time), slow ones a smooth nod rather than a bounce
static const float FASTEST_BPM = 130;  // above this, bob at half the tempo
static const float NOD_BELOW_BPM = 90; // below this, a smooth nod instead of a bounce
static const int BOB_PX = 4;
static const int NOD_PX = 3;
// no tempo: a slow rise and fall like breathing, which fits any song without pretending to be on the beat
static const unsigned long SWAY_MS = 3000;
static const int SWAY_PX = 2;
static const float SWAY_X = 6;      // his eyes drift slowly side to side, kept inside the cups
static const float LOOK_DOWN = 4;

static const unsigned long SETTLE_MS = 400;        // a bit longer than his 300 ms resize
static const unsigned long HIDE_AFTER_MS = 12000;  // the gap between songs reads as "not playing", don't flash away for that
static const unsigned long RETURN_DELAY_MS = 500;  // after an answer or a timer, let him get back to normal first

// the song. Paused results come back blank, so this keeps the last one that was playing
static bool haveSong = false;
static NowPlaying song = {};
static unsigned long songAt = 0;       // when that result arrived, to move the progress along
static unsigned long lastPlayingAt = 0;
static char artAsked[32] = "";         // the album whose cover was last requested

static uint16_t *artPixels = nullptr;
static bool artOk = false;             // artPixels holds the current album's cover
static uint16_t accent = NO_ACCENT;

static bool setAside = false;
static bool wasBlocked = false;
static unsigned long unblockedAt = 0;

static bool shown = false;
static unsigned long shownAt = 0;
static bool leaving = false;           // just hidden, Steve still sliding back to the middle
static unsigned long hiddenAt = 0;
static bool clearPending = false;

static bool artDirty = false, textDirty = false, barDirty = false;
static int pushedBarPx = -1;
static long pushedSecond = -1;
static Rect extra = {0, 0, 0, 0};
static bool extraPending = false;

static bool nudgeMine = false;
static bool gazeMine = false;

static int bobOffset();

static Rect unite(Rect a, Rect b)
{
    if (a.w <= 0 || a.h <= 0)
    {
        return b;
    }
    if (b.w <= 0 || b.h <= 0)
    {
        return a;
    }
    int left = min(a.x, b.x);
    int top = min(a.y, b.y);
    int right = max(a.x + a.w, b.x + b.w);
    int bottom = max(a.y + a.h, b.y + b.h);
    return {left, top, right - left, bottom - top};
}

static bool overlaps(Rect a, Rect b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static bool contains(Rect outer, Rect inner)
{
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}

static bool settled()
{
    return millis() - shownAt >= SETTLE_MS;
}

// how far into the song right now: the last answer plus the time since, while it's playing
static unsigned long progressNow()
{
    if (!song.playing)
    {
        return song.progressMs;
    }
    unsigned long p = song.progressMs + (millis() - songAt);
    return p < song.durationMs ? p : song.durationMs;
}

// the cover's most common strong colour, brightened up, for the artist and the progress bar.
// Hues go in 12 buckets, each pixel counting more the more colourful and bright it is
static uint16_t pickAccent(const uint16_t *px)
{
    float weight[12] = {0};
    for (int i = 0; i < ART_SIZE * ART_SIZE; i += 7) // every 7th pixel is plenty
    {
        uint16_t c = px[i];
        float r = ((c >> 11) & 31) / 31.0f;
        float g = ((c >> 5) & 63) / 63.0f;
        float b = (c & 31) / 31.0f;
        float hi = max(r, max(g, b));
        float lo = min(r, min(g, b));
        float sat = hi > 0 ? (hi - lo) / hi : 0;
        if (sat < 0.25f || hi < 0.25f)
        {
            continue; // greys, blacks and whites have no real colour to offer
        }
        // hue from 0 to 6, the usual way: which channel is biggest decides the sixth
        float d = hi - lo;
        float hue = hi == r ? fmodf((g - b) / d + 6, 6) : (hi == g ? (b - r) / d + 2 : (r - g) / d + 4);
        weight[(int)(hue * 2) % 12] += sat * hi;
    }

    int best = 0;
    for (int i = 1; i < 12; i++)
    {
        if (weight[i] > weight[best])
        {
            best = i;
        }
    }
    if (weight[best] < 20)
    {
        return NO_ACCENT; // a black and white cover, or nearly
    }

    // that bucket's middle hue, fairly saturated and full brightness, so it shows on black
    float h = (best + 0.5f) / 2; // 0 to 6
    float s = 0.65f;
    float x = 1 - fabsf(fmodf(h, 2) - 1);
    float r = 0, g = 0, b = 0;
    switch ((int)h)
    {
    case 0: r = 1; g = x; break;
    case 1: r = x; g = 1; break;
    case 2: g = 1; b = x; break;
    case 3: g = x; b = 1; break;
    case 4: r = x; b = 1; break;
    default: r = 1; b = x; break;
    }
    // pull each channel toward white by (1 - s)
    r = 1 - s + s * r;
    g = 1 - s + s * g;
    b = 1 - s + s * b;
    return rgb565(r * 255, g * 255, b * 255);
}

void musicInit()
{
    artPixels = (uint16_t *)ps_malloc(ART_BYTES);
}

void musicResult(const NowPlaying &n)
{
    if (!n.ok)
    {
        return; // a failed check says nothing about the music, keep what we had
    }

    if (!n.playing)
    {
        // paused or stopped: freeze the bar where it got to. The screen stays up a few seconds
        // in case it's only the gap between songs
        if (haveSong && song.playing)
        {
            song.progressMs = progressNow();
            song.playing = false;
            barDirty = true;
        }
        return;
    }

    if (!haveSong || strcmp(n.title, song.title) != 0 || strcmp(n.artist, song.artist) != 0)
    {
        textDirty = true;
        barDirty = true;
    }
    if (strcmp(n.artId, song.artId) != 0)
    {
        artOk = false; // the old cover is for another album, show the placeholder until this one arrives
        accent = NO_ACCENT;
        artDirty = true;
        textDirty = true;
        barDirty = true;
    }

    song = n;
    songAt = millis();
    haveSong = true;
    lastPlayingAt = millis();

    // a new album: ask for its cover. If the queue is full, artAsked stays the same and the
    // next result tries again
    if (artPixels != nullptr && n.artId[0] != '\0' && strcmp(n.artId, artAsked) != 0 && artRequest(n.artId))
    {
        snprintf(artAsked, sizeof(artAsked), "%s", n.artId);
    }
}

void musicArtReady()
{
    // check which album it's for before copying: a slow download for an older song mustn't
    // replace the cover of the one playing now
    char id[32];
    bool ok = artLatest(id, sizeof(id), nullptr);
    if (strcmp(id, song.artId) != 0)
    {
        return;
    }
    artOk = ok && artPixels != nullptr && artLatest(id, sizeof(id), artPixels);
    if (artOk)
    {
        accent = pickAccent(artPixels);
    }
    else
    {
        Serial.println("album art failed, showing a placeholder");
    }
    artDirty = true;
    textDirty = true; // the artist and the bar are in the cover's colour
    barDirty = true;
}

void musicSetAside(bool aside)
{
    setAside = aside;
}

static void lookAt(float x, float y)
{
    setGazeTarget(x, y);
    gazeMine = true;
}

static void stopLooking()
{
    if (gazeMine)
    {
        clearGazeTarget();
        gazeMine = false;
    }
}

static void show()
{
    setFaceScale(FACE_SCALE, FACE_OFFSET_Y);
    setFaceShift(FACE_SHIFT);
    setExpression(HAPPY);
    shown = true;
    leaving = false;
    shownAt = millis();
    artDirty = textDirty = barDirty = true; // nothing of it is on screen yet
}

static void hide(bool blocked)
{
    setFaceScale(1.0f, 0);
    setFaceShift(0);
    // put his normal face back only if the music stopped. If an answer or a timer is
    // taking over, it sets its own
    if (!blocked)
    {
        setExpression(NEUTRAL);
    }
    stopLooking();
    if (nudgeMine)
    {
        setFaceNudge(0, 0);
        nudgeMine = false;
    }
    shown = false;
    leaving = true;
    hiddenAt = millis();
    clearPending = true;
}

void updateMusic()
{
    unsigned long now = millis();

    // a timer is checked here rather than by main, so the music steps aside in the same frame
    // the timer view takes over, before it, and never undoes the timer's layout
    bool blocked = setAside || timerRunning() || timerViewBusy();
    if (wasBlocked && !blocked)
    {
        unblockedAt = now;
    }
    wasBlocked = blocked;

    bool want = haveSong && now - lastPlayingAt < HIDE_AFTER_MS && !blocked && now - unblockedAt >= RETURN_DELAY_MS;
    if (want && !shown)
    {
        show();
    }
    else if (!want && shown)
    {
        hide(blocked);
    }

    if (leaving && now - hiddenAt >= SETTLE_MS)
    {
        leaving = false;
    }
    if (!shown)
    {
        return;
    }

    // his eyes drift slowly side to side, a little down, toward the song
    float t = now / 1000.0f;
    lookAt(SWAY_X * sinf(t * 1.3f), LOOK_DOWN);

    // moving along while the song plays, still while it's paused
    int bob = 0;
    if (song.playing)
    {
        bob = bobOffset();
    }
    if (bob != 0 || nudgeMine)
    {
        setFaceNudge(0, bob);
        nudgeMine = bob != 0;
    }

    // the bar only needs redrawing when its fill moves a pixel or the time ticks over a second
    unsigned long p = progressNow();
    int barPx = song.durationMs == 0 ? 0 : (int)((uint64_t)BAR_W * p / song.durationMs);
    long second = p / 1000;
    if (barPx != pushedBarPx || second != pushedSecond)
    {
        barDirty = true;
        pushedBarPx = barPx;
        pushedSecond = second;
    }
}

// how far up his head is this frame
static int bobOffset()
{
    if (song.bpm <= 0)
    {
        float t = (millis() % SWAY_MS) / (float)SWAY_MS;
        return -(int)(SWAY_PX * (1 - cosf(2 * PI * t)) / 2 + 0.5f);
    }

    float bpm = song.bpm;
    while (bpm > FASTEST_BPM)
    {
        bpm /= 2; // half time: bobbing at 170 beats a minute just looks frantic
    }
    float beatMs = 60000.0f / bpm;

    // timed from the song's own position, so it starts fresh with each song and stays put
    // through a pause. Where the first beat falls isn't known, so it's the right speed but
    // not guaranteed to land on the beat
    float t = fmodf((float)progressNow(), beatMs) / beatMs;
    if (bpm < NOD_BELOW_BPM)
    {
        return -(int)(NOD_PX * (1 - cosf(2 * PI * t)) / 2 + 0.5f); // smooth down and up
    }
    return -(int)(BOB_PX * fabsf(sinf(PI * t)) + 0.5f); // a little bounce off the bottom
}

static void drawPlaceholder()
{
    frame.fillSmoothRoundRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, 8, PLACEHOLDER, TFT_BLACK);
    // a pair of joined notes in the middle
    int cx = ART_X + ART_SIZE / 2, cy = ART_Y + ART_SIZE / 2;
    frame.fillSmoothCircle(cx - 14, cy + 18, 8, NOTE, PLACEHOLDER);
    frame.fillSmoothCircle(cx + 16, cy + 12, 8, NOTE, PLACEHOLDER);
    frame.fillRect(cx - 8, cy - 22, 4, 40, NOTE);
    frame.fillRect(cx + 22, cy - 28, 4, 40, NOTE);
    frame.fillTriangle(cx - 8, cy - 22, cx + 26, cy - 28, cx + 26, cy - 20, NOTE);
    frame.fillTriangle(cx - 8, cy - 22, cx + 26, cy - 20, cx - 8, cy - 14, NOTE);
}

static void drawArt()
{
    if (!artOk)
    {
        drawPlaceholder();
        return;
    }
    // pushImage sends each value's bytes swapped, the sprite has to be told to swap them back
    frame.setSwapBytes(true);
    frame.pushImage(ART_X, ART_Y, ART_SIZE, ART_SIZE, artPixels);
    frame.setSwapBytes(false);
}

// text that fits maxW in the current font, cut with "..." if it has to be
static void fitLine(char *out, size_t size, const char *text, int maxW)
{
    snprintf(out, size, "%s", text);
    if (frame.textWidth(out) <= maxW)
    {
        return;
    }
    for (int len = strlen(text) - 1; len > 0; len--)
    {
        int keep = len;
        while (keep > 0 && text[keep - 1] == ' ')
        {
            keep--; // so it never reads "word ..."
        }
        snprintf(out, size, "%.*s...", keep, text);
        if (frame.textWidth(out) <= maxW)
        {
            return;
        }
    }
    snprintf(out, size, "...");
}

static void drawText()
{
    char line1[72], line2[72] = "", artist[72];
    int titleY, line2Y = 0, artistY;

    frame.setTextDatum(ML_DATUM);

    // the title: big if it fits, then smaller, then two smaller lines, then cut
    frame.setFreeFont(&FreeSansBold12pt7b);
    if (frame.textWidth(song.title) <= TEXT_W)
    {
        snprintf(line1, sizeof(line1), "%s", song.title);
        titleY = 152;
        artistY = 178;
    }
    else
    {
        frame.setFreeFont(&FreeSansBold9pt7b);
        if (frame.textWidth(song.title) <= TEXT_W)
        {
            snprintf(line1, sizeof(line1), "%s", song.title);
            titleY = 154;
            artistY = 177;
        }
        else
        {
            // break at the last space where the first line still fits
            int split = -1;
            for (int i = strlen(song.title) - 1; i > 0; i--)
            {
                if (song.title[i] != ' ')
                {
                    continue;
                }
                snprintf(line1, sizeof(line1), "%.*s", i, song.title);
                if (frame.textWidth(line1) <= TEXT_W)
                {
                    split = i;
                    break;
                }
            }
            if (split < 0)
            {
                fitLine(line1, sizeof(line1), song.title, TEXT_W); // one long word, nothing better than cutting it
            }
            else
            {
                fitLine(line2, sizeof(line2), song.title + split + 1, TEXT_W);
            }
            titleY = 147;
            line2Y = 166;
            artistY = 186;
        }
    }

    frame.setTextColor(TFT_WHITE);
    frame.drawString(line1, TEXT_X, titleY);
    if (line2[0] != '\0')
    {
        frame.drawString(line2, TEXT_X, line2Y);
    }

    frame.setFreeFont(&FreeSansBold9pt7b);
    fitLine(artist, sizeof(artist), song.artist, TEXT_W);
    frame.setTextColor(accent); // the cover's colour, light grey until it arrives
    frame.drawString(artist, TEXT_X, artistY);
}

static void drawBar()
{
    unsigned long p = progressNow();
    frame.fillSmoothRoundRect(BAR_X, BAR_Y, BAR_W, BAR_H, 3, TRACK, TFT_BLACK);
    int fill = song.durationMs == 0 ? 0 : (int)((uint64_t)BAR_W * p / song.durationMs);
    if (fill >= BAR_H) // any shorter and the rounded ends would overlap
    {
        frame.fillSmoothRoundRect(BAR_X, BAR_Y, fill, BAR_H, 3, accent, TRACK);
    }

    char text[16];
    frame.setFreeFont(&FreeSansBold9pt7b);
    frame.setTextColor(GREY);
    unsigned long s = p / 1000;
    snprintf(text, sizeof(text), "%lu:%02lu", s / 60, s % 60);
    frame.setTextDatum(ML_DATUM);
    frame.drawString(text, BAR_X, TIMES_Y);
    s = song.durationMs / 1000;
    snprintf(text, sizeof(text), "%lu:%02lu", s / 60, s % 60);
    frame.setTextDatum(MR_DATUM);
    frame.drawString(text, BAR_X + BAR_W, TIMES_Y);
}

static void drawHeadphones()
{
    // drawn wherever he is right now, so they slide and shrink with him and bob along too
    float ax, ay, s;
    faceAnchor(ax, ay, s);
    int r = BAND_R * s;
    int thick = max(3, (int)(BAND_THICK * s));
    // over the top: drawSmoothArc counts clockwise from 6 o'clock, so 95 to 265 is the top half
    frame.drawSmoothArc(ax, ay, r, r - thick, 95, 265, HEADBAND, TFT_BLACK, true);
    for (int side = -1; side <= 1; side += 2)
    {
        float cx = ax + side * (r - thick / 2.0f);
        frame.fillSmoothRoundRect(cx - CUP_W * s / 2, ay - CUP_H * s / 2 + CUP_DROP * s, CUP_W * s, CUP_H * s,
                                  9 * s, CUPS, TFT_BLACK);
    }
}

// draws one of the still parts if it changed or main cleared it, and remembers to push it
static void drawPart(Rect box, bool &dirty, void (*draw)(), Rect pushed)
{
    if (!dirty && !overlaps(pushed, box))
    {
        return;
    }
    frame.fillRect(box.x, box.y, box.w, box.h, TFT_BLACK);
    draw();
    dirty = false;
    if (!contains(pushed, box))
    {
        extra = unite(extraPending ? extra : Rect{0, 0, 0, 0}, box);
        extraPending = true;
    }
}

void drawMusic(Rect pushed)
{
    if (!shown)
    {
        return;
    }
    drawHeadphones();

    // while he's moving into place the card waits, so it never lands on top of him mid-slide.
    // It all appears together on the first frame after
    if (!settled())
    {
        return;
    }
    drawPart(ART_BOX, artDirty, drawArt, pushed);
    drawPart(TEXT_BOX, textDirty, drawText, pushed);
    drawPart(BAR_BOX, barDirty, drawBar, pushed);
}

bool musicActive()
{
    return shown || leaving;
}

bool musicOwnsFace()
{
    return shown && settled();
}

Rect musicRegion()
{
    // the whole screen while he moves in or out (he crosses everything), then just his box
    if (!shown || !settled())
    {
        return ALL;
    }
    return STEVE_BOX;
}

bool musicTakeExtra(Rect &out)
{
    if (!extraPending)
    {
        return false;
    }
    extraPending = false;
    out = extra;
    return true;
}

bool musicTakeClear(Rect &out)
{
    if (!clearPending)
    {
        return false;
    }
    clearPending = false;
    out = ALL;
    return true;
}