#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include "colors.h"
#include "reading.h"

extern TFT_eSprite frame;

static constexpr uint16_t PAPER = rgb565(232, 228, 216);
static constexpr uint16_t PAPER_SHADE = rgb565(208, 203, 190); // the right page, turned a little away
static constexpr uint16_t FOLD = rgb565(160, 155, 145);
static constexpr uint16_t INK = rgb565(70, 70, 70);       // mastheads and headlines
static constexpr uint16_t TEXT = rgb565(150, 148, 140);   // body text lines
static constexpr uint16_t PHOTO = rgb565(175, 172, 164);
static constexpr uint16_t EDGE = rgb565(180, 175, 163);   // the paper's thickness along the top

// the paper is designed flat, PAPER_W x PAPER_H, and every piece of it is mapped onto the
// leaning shape when it's drawn
static const float PAPER_W = 180;
static const float PAPER_H = 104;
static const float PAPER_CX = 160;
static const int EDGE_H = 3;
static const float HAND_V = 58;  // how far down the page the hands hold it
static const int HAND_REACH = 12; // how far the hands stick out past the paper's sides

// the leaning shape: where the top edge is, how wide the top and bottom are, and how tall
// it looks. A wider top than bottom is what makes it lean toward you
struct Pose
{
    float top;
    float topW;
    float bottomW;
    float height;
};

static const Pose READ_POSE = {128, 210, 168, 78}; // tipped toward you, reading
static const Pose PEEK_POSE = {140, 193, 168, 84}; // lowered and straighter, looking at you

// rising into view and dropping away
static const float OFFSCREEN = 130; // pushes the paper's top below the bottom of the screen
static const unsigned long RISE_MS = 400;
static const unsigned long DROP_MS = 300;

// reading: the eyes move along a line, then flick back to the start of the next one
static const float SCAN_X = 14;
static const float READ_LOOK_Y = 10; // looking down at the page
static const unsigned long LINE_MS = 2400;
static const Expression READING_LIDS = {8, 8, 0}; // lids a little lowered, focused

// peeking over the top
static const long PEEK_MIN_MS = 15000;
static const long PEEK_MAX_MS = 25000;
static const unsigned long PEEK_MOVE_MS = 400;
static const unsigned long PEEK_HOLD_MS = 2000;
static const float PEEK_LOOK_Y = -6;

// a slow bob so he isn't frozen, one pixel either way
static const float BOB_PX = 1;
static const float BOB_PERIOD_MS = 3000;

enum class Paper
{
    Hidden,
    Rising,
    Up,
    Dropping
};

static Paper paper = Paper::Hidden;
static unsigned long paperStart = 0; // when the rise or drop began
static bool peeking = false;
static unsigned long peekStart = 0;
static unsigned long nextPeekAt = 0;

static Pose pose = READ_POSE; // this frame's shape and position
static Rect box = {0, 0, 0, 0};     // where the paper is this frame
static Rect lastBox = {0, 0, 0, 0}; // and where it was last frame, which also needs clearing
static bool clearPending = false;
static Rect clearBox = {0, 0, 0, 0};

static float easeOut(float t)
{
    float u = 1 - t;
    return 1 - u * u * u;
}

static float easeIn(float t)
{
    return t * t * t;
}

static float easeInOut(float t)
{
    return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}

static Pose lerpPose(const Pose &a, const Pose &b, float t)
{
    return {a.top + (b.top - a.top) * t,
            a.topW + (b.topW - a.topW) * t,
            a.bottomW + (b.bottomW - a.bottomW) * t,
            a.height + (b.height - a.height) * t};
}

static bool isEmpty(const Rect &r)
{
    return r.w <= 0 || r.h <= 0;
}

// the smallest box covering both, where an empty box doesn't count
static Rect unite(const Rect &a, const Rect &b)
{
    if (isEmpty(a))
    {
        return b;
    }
    if (isEmpty(b))
    {
        return a;
    }
    int left = min(a.x, b.x);
    int top = min(a.y, b.y);
    int right = max(a.x + a.w, b.x + b.w);
    int bottom = max(a.y + a.h, b.y + b.h);
    return {left, top, right - left, bottom - top};
}

// the box around the paper, its top edge and the hands, cut to the screen
static Rect paperBox(const Pose &p)
{
    float widest = max(p.topW, p.bottomW);
    int x0 = (int)(PAPER_CX - widest / 2) - HAND_REACH;
    int x1 = (int)(PAPER_CX + widest / 2) + HAND_REACH;
    int y0 = (int)p.top - EDGE_H - 1;
    int y1 = (int)(p.top + p.height) + 2;
    if (y0 < 0)
    {
        y0 = 0;
    }
    if (y1 > 240)
    {
        y1 = 240;
    }
    if (y0 >= y1)
    {
        return {0, 0, 0, 0}; // all of it is below the screen
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

// a point on the flat paper (u across, v down) to where it lands on the screen.
// Further down the page the paper gets narrower, because the bottom is further from you
static void mapPoint(float u, float v, int &x, int &y)
{
    float t = v / PAPER_H;
    float rowW = pose.topW + (pose.bottomW - pose.topW) * t;
    x = lroundf(PAPER_CX - rowW / 2 + (u / PAPER_W) * rowW);
    y = lroundf(pose.top + t * pose.height);
}

// a flat rectangle on the paper, drawn as the four-sided shape it becomes, in two triangles
static void quad(float u0, float v0, float u1, float v1, uint16_t c)
{
    int ax, ay, bx, by, cx, cy, dx, dy;
    mapPoint(u0, v0, ax, ay);
    mapPoint(u1, v0, bx, by);
    mapPoint(u1, v1, cx, cy);
    mapPoint(u0, v1, dx, dy);
    frame.fillTriangle(ax, ay, bx, by, cx, cy, c);
    frame.fillTriangle(ax, ay, cx, cy, dx, dy, c);
}

void readingShow()
{
    if (paper == Paper::Up || paper == Paper::Rising)
    {
        return;
    }
    paper = Paper::Rising;
    paperStart = millis();
    peeking = false;
    setExpression(READING_LIDS);
}

void readingHide()
{
    if (paper == Paper::Hidden || paper == Paper::Dropping)
    {
        return;
    }
    paper = Paper::Dropping;
    paperStart = millis();
    peeking = false;
    clearGazeTarget();
    setExpression(NEUTRAL);
}

bool readingActive()
{
    return paper != Paper::Hidden;
}

void updateReading()
{
    if (paper == Paper::Hidden)
    {
        return;
    }
    unsigned long now = millis();

    // how far below its spot the paper is, for rising and dropping
    float drop = 0;
    if (paper == Paper::Rising)
    {
        float t = (now - paperStart) / (float)RISE_MS;
        if (t >= 1)
        {
            t = 1;
            paper = Paper::Up;
            nextPeekAt = now + random(PEEK_MIN_MS, PEEK_MAX_MS);
        }
        drop = OFFSCREEN * (1 - easeOut(t));
    }
    else if (paper == Paper::Dropping)
    {
        float t = (now - paperStart) / (float)DROP_MS;
        if (t >= 1)
        {
            // gone: main wipes wherever it was drawn last, once
            paper = Paper::Hidden;
            clearBox = unite(box, lastBox);
            clearPending = true;
            box = lastBox = {0, 0, 0, 0};
            return;
        }
        drop = OFFSCREEN * easeIn(t);
    }

    // 0 while reading, 1 while peeking at you, eased in between
    float peek = 0;
    if (paper == Paper::Up)
    {
        if (!peeking && (long)(now - nextPeekAt) >= 0)
        {
            peeking = true;
            peekStart = now;
            setExpression(NEUTRAL); // eyes open fully to look at you
        }
        if (peeking)
        {
            unsigned long e = now - peekStart;
            if (e < PEEK_MOVE_MS)
            {
                peek = easeInOut(e / (float)PEEK_MOVE_MS);
            }
            else if (e < PEEK_MOVE_MS + PEEK_HOLD_MS)
            {
                peek = 1;
            }
            else if (e < 2 * PEEK_MOVE_MS + PEEK_HOLD_MS)
            {
                peek = 1 - easeInOut((e - PEEK_MOVE_MS - PEEK_HOLD_MS) / (float)PEEK_MOVE_MS);
            }
            else
            {
                peeking = false;
                setExpression(READING_LIDS);
                nextPeekAt = now + random(PEEK_MIN_MS, PEEK_MAX_MS);
            }
        }
    }

    // where the eyes look: at you while peeking, along the line while reading
    if (paper != Paper::Dropping)
    {
        if (peek > 0.5f)
        {
            setGazeTarget(0, PEEK_LOOK_Y);
        }
        else
        {
            float along = (now % LINE_MS) / (float)LINE_MS;
            setGazeTarget(-SCAN_X + 2 * SCAN_X * along, READ_LOOK_Y);
        }
    }

    pose = lerpPose(READ_POSE, PEEK_POSE, peek);
    pose.top += drop + BOB_PX * sinf(now / BOB_PERIOD_MS * 2 * PI);

    lastBox = box;
    box = paperBox(pose);
}

void drawReading()
{
    if (paper == Paper::Hidden)
    {
        return;
    }
    const float mid = PAPER_W / 2;

    // the two pages and the fold between them
    quad(0, 0, mid, PAPER_H, PAPER);
    quad(mid, 0, PAPER_W, PAPER_H, PAPER_SHADE);
    quad(mid - 1, 0, mid + 1, PAPER_H, FOLD);

    // left page: masthead, a headline, a photo and its story
    quad(8, 8, mid - 8, 13, INK);
    quad(8, 19, mid - 34, 22, INK);
    quad(8, 28, 40, 58, PHOTO);
    for (int i = 0; i < 8; i++)
    {
        float y = 28 + i * 7;
        if (y > PAPER_H - 10)
        {
            break;
        }
        quad(46, y, mid - 8, y + 2, TEXT);
    }

    // right page: two columns, each with a heading. The (i % 3) makes the lines ragged
    for (int col = 0; col < 2; col++)
    {
        float x0 = mid + 8 + col * 40;
        quad(x0, 9, x0 + 32, 13, INK);
        for (int i = 0; i < 11; i++)
        {
            float y = 20 + i * 7;
            if (y > PAPER_H - 10)
            {
                break;
            }
            quad(x0, y, x0 + 32 - (i % 3) * 5, y + 2, TEXT);
        }
    }

    // the paper's thickness, only visible because the top leans toward you.
    // The top edge is level, so a plain rectangle does it
    int ax, ay, bx, by;
    mapPoint(0, 0, ax, ay);
    mapPoint(PAPER_W, 0, bx, by);
    frame.fillRect(ax, ay - EDGE_H, bx - ax, EDGE_H, EDGE);

    // his hands, over the edges so they look like they're gripping it
    for (int side = -1; side <= 1; side += 2)
    {
        int hx, hy;
        mapPoint(side < 0 ? 0 : PAPER_W, HAND_V, hx, hy);
        frame.fillRoundRect(hx - 7 + side * 2, hy - 10, 14, 20, 6, TFT_WHITE);
    }
}

Rect readingRegion()
{
    // last frame's spot too, or the old paper would be left behind as it moves
    return unite(box, lastBox);
}

bool readingTakeClear(Rect &out)
{
    if (!clearPending)
    {
        return false;
    }
    clearPending = false;
    out = clearBox;
    return !isEmpty(out);
}