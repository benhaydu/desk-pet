#include <Arduino.h>
#include <TFT_eSPI.h>
#include "colors.h"
#include "panel.h"

// your own team images, if you've made them. tools/make_team_icons.py writes this file from
// PNGs in firmware/icons/, and both are gitignored, so anyone else building the repo
// just gets the drawn icons
#if __has_include("team_icons.h")
#include "team_icons.h"
#define HAVE_TEAM_ICONS
#endif

extern TFT_eSprite frame;
// one colour per topic, all bright enough to read on black
static constexpr uint16_t LEAFS_BLUE = rgb565(74, 127, 232);
static constexpr uint16_t SAINTS_GOLD = rgb565(211, 188, 141);
static constexpr uint16_t CANADA_RED = rgb565(232, 69, 60);
static constexpr uint16_t TECH_CYAN = rgb565(46, 196, 214);
static constexpr uint16_t WORLD_VIOLET = rgb565(150, 130, 240);
static constexpr uint16_t PLAIN = rgb565(220, 220, 220); // a card id this firmware doesn't know
static constexpr uint16_t DOT_GREY = rgb565(60, 60, 60);
static constexpr uint16_t PUCK_GREY = rgb565(200, 200, 200);

// layout in screen pixels. Mini Steve's box starts at x 270, so the title row stops short
static const int SCREEN_W = 320;
static const int TITLE_W = 264;
static const int TITLE_H = 44;
static const int ICON_SLOT = 40; // images fill it, drawn icons are 32 and sit in the middle
static const int ICON_X = 8;
static const int ICON_Y = 2;
static const int TITLE_X = 56;

// the lines and underline, the only rows besides the title that move during a slide
static const int BODY_Y = 70;
static const int BODY_H = 130;
static const int LINES_MID = 128; // the block of lines is centred on this row
static const int LINE_GAP = 40;
static const int LINE_MAX_W = SCREEN_W - 16;
static const int UNDERLINE_Y = 194;
static const int UNDERLINE_W = 100;

static const int DOTS_BAND_Y = 214;
static const int DOTS_BAND_H = 16;
static const int DOTS_Y = 222;
static const int DOT_GAP = 12;

static const unsigned long SLIDE_IN_MS = 300;
static const unsigned long SLIDE_OUT_MS = 220;

#ifdef HAVE_TEAM_ICONS
static_assert(TEAM_ICON_SIZE == ICON_SLOT, "rerun make_team_icons.py with SIZE matching ICON_SLOT");
#endif

// the drawn icons, each fits a 32x32 box with its top-left at (x, y)

static void iconPuck(int x, int y, uint16_t c)
{
    frame.drawWideLine(x + 26, y + 2, x + 12, y + 24, 4, c, TFT_BLACK); // shaft
    frame.drawWideLine(x + 12, y + 24, x + 3, y + 26, 5, c, TFT_BLACK); // blade
    frame.fillEllipse(x + 24, y + 27, 6, 3, PUCK_GREY);
}

static void iconFootball(int x, int y, uint16_t c)
{
    frame.fillEllipse(x + 16, y + 16, 15, 9, c);
    frame.fillRect(x + 9, y + 15, 14, 2, TFT_BLACK); // seam
    for (int lx = x + 11; lx <= x + 20; lx += 3)
    {
        frame.fillRect(lx, y + 12, 2, 9, TFT_BLACK); // laces
    }
}

// the outline of a maple leaf, clockwise from the top point
static const int8_t LEAF[][2] = {
    {16, 0}, {19, 7}, {23, 5}, {22, 13}, {29, 9}, {27, 15}, {32, 17}, {24, 22}, {25, 26},
    {18, 24}, {17, 31}, {15, 31}, {14, 24}, {7, 26}, {8, 22}, {0, 17}, {5, 15}, {3, 9},
    {10, 13}, {9, 5}, {13, 7}};

static void iconMaple(int x, int y, uint16_t c)
{
    // filled as a fan of triangles from the middle, like slices of a pie. That works
    // because every point of the outline can be reached in a straight line from (16, 17)
    const int n = sizeof(LEAF) / sizeof(LEAF[0]);
    for (int i = 0; i < n; i++)
    {
        int j = (i + 1) % n; // the last point joins back up with the first
        frame.fillTriangle(x + 16, y + 17,
                           x + LEAF[i][0], y + LEAF[i][1],
                           x + LEAF[j][0], y + LEAF[j][1], c);
    }
}

static void iconChip(int x, int y, uint16_t c)
{
    frame.fillRoundRect(x + 7, y + 7, 19, 19, 3, c);
    frame.fillRect(x + 12, y + 12, 9, 9, TFT_BLACK);
    for (int i = 0; i < 3; i++)
    {
        int o = 10 + i * 6;
        frame.fillRect(x + o, y + 1, 2, 5, c);  // top pins
        frame.fillRect(x + o, y + 26, 2, 5, c); // bottom
        frame.fillRect(x + 1, y + o, 5, 2, c);  // left
        frame.fillRect(x + 26, y + o, 5, 2, c); // right
    }
}

static void iconGlobe(int x, int y, uint16_t c)
{
    frame.fillSmoothCircle(x + 16, y + 16, 15, c, TFT_BLACK);
    // meridian two pixels thick, then the equator and two latitudes, all cut out in black
    frame.drawEllipse(x + 16, y + 16, 7, 15, TFT_BLACK);
    frame.drawEllipse(x + 16, y + 16, 6, 15, TFT_BLACK);
    frame.fillRect(x + 1, y + 15, 31, 2, TFT_BLACK);
    frame.fillRect(x + 4, y + 8, 25, 2, TFT_BLACK);
    frame.fillRect(x + 4, y + 22, 25, 2, TFT_BLACK);
}

// what each card id looks like. A new topic is one more line here
struct Topic
{
    const char *id;
    uint16_t accent;
    void (*drawIcon)(int x, int y, uint16_t c);
};

static const Topic TOPICS[] = {
    {"leafs", LEAFS_BLUE, iconPuck},
    {"saints", SAINTS_GOLD, iconFootball},
    {"news_canada", CANADA_RED, iconMaple},
    {"news_tech", TECH_CYAN, iconChip},
    {"news_world", WORLD_VIOLET, iconGlobe},
};

enum class Slide
{
    Hidden,
    In,
    Shown,
    Out
};

static Card card; // our own copy, the caller's can go away
static uint16_t accent = PLAIN;
static void (*drawIcon)(int x, int y, uint16_t c) = nullptr;
static const GFXfont *titleFont = &FreeSansBold12pt7b;
static const GFXfont *lineFont[MAX_CARD_LINES];
static int dotIndex = 0;
static int dotTotal = 0;

static Slide slide = Slide::Hidden;
static unsigned long slideStart = 0;
static int offsetX = 0;       // how far the content is shifted sideways right now
static bool dirty = false;    // title and body need drawing
static bool dotsDirty = false;
static bool clearAll = false; // the slide-out just finished, wipe everything once

static float easeOut(float t)
{
    float u = 1 - t;
    return 1 - u * u * u; // fast start, gentle landing
}

static float easeIn(float t)
{
    return t * t * t; // gentle start, quick exit
}

static const GFXfont *fitFont(const char *text, int maxWidth)
{
    // big if it fits, otherwise the smaller size. Measured in pixels, not characters,
    // because "W" is much wider than "i"
    frame.setFreeFont(&FreeSansBold12pt7b);
    if (frame.textWidth(text) <= maxWidth)
    {
        return &FreeSansBold12pt7b;
    }
    return &FreeSansBold9pt7b;
}

static bool drawImageIcon(const char *id, int x, int y)
{
#ifdef HAVE_TEAM_ICONS
    for (int i = 0; i < TEAM_ICON_COUNT; i++)
    {
        if (strcmp(TEAM_ICONS[i].id, id) != 0)
        {
            continue;
        }
        const uint16_t *pixels = TEAM_ICONS[i].pixels;
        for (int row = 0; row < TEAM_ICON_SIZE; row++)
        {
            for (int col = 0; col < TEAM_ICON_SIZE; col++)
            {
                uint16_t c = pixels[row * TEAM_ICON_SIZE + col];
                // black is the background the converter filled in, the screen's already black
                if (c != 0)
                {
                    frame.drawPixel(x + col, y + row, c);
                }
            }
        }
        return true;
    }
#else
    (void)id; // only used when the images exist, this keeps the compiler from warning
    (void)x;
    (void)y;
#endif
    return false;
}

static void drawContent(int dx)
{
    // the title row, clipped so a slide can never draw over mini Steve
    frame.setViewport(0, 0, TITLE_W, TITLE_H, false);
    frame.fillRect(0, 0, TITLE_W, TITLE_H, TFT_BLACK);
    if (!drawImageIcon(card.id, ICON_X + dx, ICON_Y) && drawIcon != nullptr)
    {
        int inset = (ICON_SLOT - 32) / 2;
        drawIcon(ICON_X + inset + dx, ICON_Y + inset, accent);
    }
    frame.setFreeFont(titleFont);
    frame.setTextDatum(ML_DATUM); // x is the left edge, y is the middle of the text
    frame.setTextColor(accent);
    frame.drawString(card.title, TITLE_X + dx, TITLE_H / 2);
    frame.resetViewport();

    // the lines, centred as a block, then the underline
    frame.setViewport(0, BODY_Y, SCREEN_W, BODY_H, false);
    frame.fillRect(0, BODY_Y, SCREEN_W, BODY_H, TFT_BLACK);
    frame.setTextDatum(MC_DATUM);
    frame.setTextColor(TFT_WHITE);
    // 3 lines sit at -40, 0, +40 around the middle, 2 lines at -20 and +20
    float first = LINES_MID - (card.lineCount - 1) * LINE_GAP / 2.0f;
    for (int i = 0; i < card.lineCount; i++)
    {
        frame.setFreeFont(lineFont[i]);
        frame.drawString(card.lines[i], SCREEN_W / 2 + dx, (int)(first + i * LINE_GAP));
    }
    frame.fillRect(SCREEN_W / 2 - UNDERLINE_W / 2 + dx, UNDERLINE_Y, UNDERLINE_W, 2, accent);
    frame.resetViewport();

    frame.pushSprite(0, 0, 0, 0, TITLE_W, TITLE_H);
    frame.pushSprite(0, BODY_Y, 0, BODY_Y, SCREEN_W, BODY_H);
}

static void drawDots()
{
    frame.fillRect(0, DOTS_BAND_Y, SCREEN_W, DOTS_BAND_H, TFT_BLACK);
    int x0 = SCREEN_W / 2 - DOT_GAP * (dotTotal - 1) / 2;
    for (int i = 0; i < dotTotal; i++)
    {
        int x = x0 + i * DOT_GAP;
        if (i == dotIndex)
        {
            frame.fillSmoothCircle(x, DOTS_Y, 3, accent, TFT_BLACK);
        }
        else
        {
            frame.fillSmoothCircle(x, DOTS_Y, 2, DOT_GREY, TFT_BLACK);
        }
    }
    frame.pushSprite(0, DOTS_BAND_Y, 0, DOTS_BAND_Y, SCREEN_W, DOTS_BAND_H);
}

void panelShow(const Card &c, int index, int total)
{
    card = c;

    accent = PLAIN;
    drawIcon = nullptr;
    for (const Topic &t : TOPICS)
    {
        if (strcmp(t.id, card.id) == 0)
        {
            accent = t.accent;
            drawIcon = t.drawIcon;
            break;
        }
    }

    // pick the fonts once here, not every frame of the slide
    titleFont = fitFont(card.title, TITLE_W - TITLE_X - 4);
    for (int i = 0; i < card.lineCount; i++)
    {
        lineFont[i] = fitFont(card.lines[i], LINE_MAX_W);
    }

    dotIndex = index;
    dotTotal = total;

    slide = Slide::In;
    slideStart = millis();
    offsetX = SCREEN_W;
    dirty = true;
    dotsDirty = true;
    clearAll = false; // the new panel covers everything the old one left behind
}

void panelHide()
{
    if (slide == Slide::Hidden || slide == Slide::Out)
    {
        return;
    }
    slide = Slide::Out;
    slideStart = millis();
    dirty = true;
}

bool panelBusy()
{
    return slide == Slide::In || slide == Slide::Out;
}

void updatePanel()
{
    float t;
    switch (slide)
    {
    case Slide::In:
        t = (millis() - slideStart) / (float)SLIDE_IN_MS;
        if (t >= 1)
        {
            t = 1;
            slide = Slide::Shown;
        }
        offsetX = (int)(SCREEN_W * (1 - easeOut(t))); // 320 down to 0
        dirty = true;
        break;

    case Slide::Out:
        t = (millis() - slideStart) / (float)SLIDE_OUT_MS;
        if (t >= 1)
        {
            slide = Slide::Hidden;
            clearAll = true;
            break;
        }
        offsetX = (int)(-SCREEN_W * easeIn(t)); // 0 down to -320
        dirty = true;
        break;

    default:
        break; // Shown and Hidden don't move
    }
}

void drawPanel()
{
    if (clearAll)
    {
        frame.fillRect(0, 0, TITLE_W, TITLE_H, TFT_BLACK);
        frame.fillRect(0, BODY_Y, SCREEN_W, BODY_H, TFT_BLACK);
        frame.fillRect(0, DOTS_BAND_Y, SCREEN_W, DOTS_BAND_H, TFT_BLACK);
        frame.pushSprite(0, 0, 0, 0, TITLE_W, TITLE_H);
        frame.pushSprite(0, BODY_Y, 0, BODY_Y, SCREEN_W, BODY_H);
        frame.pushSprite(0, DOTS_BAND_Y, 0, DOTS_BAND_Y, SCREEN_W, DOTS_BAND_H);
        clearAll = false;
        dirty = false;
        dotsDirty = false;
        return;
    }

    if (dirty && slide != Slide::Hidden)
    {
        drawContent(offsetX);
        dirty = false;
    }

    if (dotsDirty)
    {
        drawDots();
        dotsDirty = false;
    }
}