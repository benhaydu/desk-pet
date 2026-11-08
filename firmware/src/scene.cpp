#include <TFT_eSPI.h>
#include "eyes.h"
#include "scene.h"

extern TFT_eSprite frame;

// how long a scene stays up, about as long as the longest headline
static const unsigned long SCENE_HOLD_MS = 7000;

// the face's width, from the top of the screen down to the headline strip.
// staying inside the face's width keeps the push time reasonable
static const Rect SCENE_AREA = {REGION_X, 0, REGION_W, REGION_Y + REGION_H};

// colours as (r, g, b), worked out at compile time
static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static constexpr uint16_t RAIN_BLUE = rgb565(55, 138, 221);
static constexpr uint16_t UMBRELLA_RED = rgb565(226, 75, 74);
static constexpr uint16_t UMBRELLA_WHITE = rgb565(241, 239, 232);
static constexpr uint16_t RIB_RED = rgb565(163, 45, 45);
static constexpr uint16_t FINIAL_GREY = rgb565(211, 209, 199);
static constexpr uint16_t SHAFT_GREY = rgb565(180, 178, 169);
static constexpr uint16_t HANDLE_BROWN = rgb565(133, 79, 11);
static constexpr uint16_t SUN_YELLOW = rgb565(250, 199, 117);
static constexpr uint16_t CLOUD_LIGHT = rgb565(211, 209, 199);
static constexpr uint16_t CLOUD_MID = rgb565(180, 178, 169);
static constexpr uint16_t CLOUD_GREY = rgb565(136, 135, 128);
static constexpr uint16_t CLOUD_DARK = rgb565(95, 94, 90);
static constexpr uint16_t STORM_CLOUD = rgb565(68, 68, 65);
static constexpr uint16_t STORM_CLOUD_DARK = rgb565(44, 44, 42);
static constexpr uint16_t STORM_FLASH = rgb565(57, 57, 57);
static constexpr uint16_t LIGHTNING = rgb565(250, 199, 117);
static constexpr uint16_t SNOW_WHITE = rgb565(255, 255, 255);
static constexpr uint16_t SNOW_GROUND = rgb565(241, 239, 232);
static constexpr uint16_t HAT_DARK = rgb565(44, 44, 42);
static constexpr uint16_t CARROT = rgb565(216, 90, 48);
static constexpr uint16_t TWIG_BROWN = rgb565(153, 60, 29);
static constexpr uint16_t FOG_GREY = rgb565(180, 178, 169);

// faces the scenes put on
static const Expression SUN_SQUINT = {0, 0, 22};
static const Expression FOG_SQUINT = {15, 15, 14};
static const Expression WORRIED = {0, 20, 0};

struct Pt
{
    float x, y;
};

// the same number in always gives the same "random" 0..1 out,
// so each drop, flake or bolt keeps its own spot from frame to frame
static float hash01(uint32_t n)
{
    n *= 2654435761u;
    n ^= n >> 13;
    n *= 0x5bd1e995u;
    n ^= n >> 15;
    return (n & 0xFFFF) / 65535.0f;
}

// a point along a curve from a to b that bends toward c, u goes 0..1
static Pt bezier(Pt a, Pt c, Pt b, float u)
{
    float v = 1 - u;
    return {v * v * a.x + 2 * v * u * c.x + u * u * b.x,
            v * v * a.y + 2 * v * u * c.y + u * u * b.y};
}

// any four-sided shape is two triangles
static void fillQuad(Pt a, Pt b, Pt c, Pt d, uint16_t color)
{
    frame.fillTriangle(a.x, a.y, b.x, b.y, c.x, c.y, color);
    frame.fillTriangle(a.x, a.y, c.x, c.y, d.x, d.y, color);
}

// a puffy cloud from four circles and a flat bottom, s scales it
static void drawCloud(float x, float y, float s, uint16_t color)
{
    frame.fillCircle(x, y, 22 * s, color);
    frame.fillCircle(x + 22 * s, y - 10 * s, 26 * s, color);
    frame.fillCircle(x + 48 * s, y, 22 * s, color);
    frame.fillCircle(x + 24 * s, y + 6 * s, 24 * s, color);
    frame.fillRect(x - 4 * s, y, 56 * s, 22 * s, color);
}

// slanted rain drops falling through the scene area
static void drawRainDrops(unsigned long t, int count, int speed, float slant, int top, int fallH, int offsetX)
{
    for (int i = 0; i < count; i++)
    {
        int y = top + (t * speed / 1000 + (int)(hash01(i + 100) * fallH)) % fallH;
        int x = SCENE_AREA.x + hash01(i) * (SCENE_AREA.w + 60) - (y - top) * slant + offsetX;
        int dx = 2 + slant * 6;
        frame.drawLine(x, y, x - dx, y + 9, RAIN_BLUE);
        frame.drawLine(x + 1, y, x + 1 - dx, y + 9, RAIN_BLUE);
    }
}

// rain: smaller eyes tucked under an umbrella

static const Pt APEX = {160, 40};
static const int RIBS = 6;         // so 6 panels, 7 rib ends
static const float SPAN = 104;     // half the canopy's width
static const float EDGE_Y = 98;    // where the outer rib ends sit
static const float EDGE_LIFT = 10; // the middle rib ends sit a bit higher
static const int STEPS = 5;        // straight pieces per curve, more = smoother

static const int DROPS = 26;
static const int DROP_LEN = 9;
static const int DROP_SPEED = 150; // px per second
static const int FALL_H = 190;     // drops fall from the top to just above the headline

// where rib i ends, spread along a shallow arc
static Pt ribEnd(int i)
{
    float a = PI - i * PI / RIBS;
    return {APEX.x + cosf(a) * SPAN, EDGE_Y - sinf(a) * EDGE_LIFT};
}

// a rib bulges outward a little on its way down from the top
static Pt ribControl(Pt end)
{
    return {end.x + (APEX.x - end.x) * 0.15f, APEX.y + 4};
}

static void drawUmbrellaDrops(unsigned long t)
{
    for (int i = 0; i < DROPS; i++)
    {
        int x = SCENE_AREA.x + hash01(i) * SCENE_AREA.w;
        // each drop starts at its own height, then they all fall at the same speed
        int y = (t * DROP_SPEED / 1000 + (int)(hash01(i + 100) * FALL_H)) % FALL_H;

        // under the canopy it's dry, the umbrella hides the ones above it
        bool underCanopy = x > APEX.x - SPAN && x < APEX.x + SPAN && y + DROP_LEN > EDGE_Y - EDGE_LIFT;
        if (underCanopy)
        {
            continue;
        }
        // two lines side by side, one pixel looked too thin
        frame.drawLine(x, y, x - 2, y + DROP_LEN, RAIN_BLUE);
        frame.drawLine(x + 1, y, x - 1, y + DROP_LEN, RAIN_BLUE);
    }
}

static void drawUmbrella()
{
    // shaft first so the canopy covers its top
    frame.fillRect(APEX.x - 1, APEX.y, 3, 128, SHAFT_GREY);

    // J-shaped handle: a short straight bit, then half a circle made of dots
    frame.fillRect(APEX.x - 2, 160, 5, 12, HANDLE_BROWN);
    for (int k = 0; k <= 10; k++)
    {
        float a = k * PI / 10;
        frame.fillCircle(APEX.x + 9 - 9 * cosf(a), 172 + 9 * sinf(a), 2, HANDLE_BROWN);
    }

    // sample every rib once, panels and rib lines both use the same points
    Pt rib[RIBS + 1][STEPS + 1];
    for (int i = 0; i <= RIBS; i++)
    {
        Pt end = ribEnd(i);
        Pt ctrl = ribControl(end);
        for (int k = 0; k <= STEPS; k++)
        {
            rib[i][k] = bezier(APEX, ctrl, end, k / (float)STEPS);
        }
    }

    for (int i = 0; i < RIBS; i++)
    {
        uint16_t color = (i % 2 == 0) ? UMBRELLA_WHITE : UMBRELLA_RED;

        // fill the panel between rib i and rib i + 1, one strip at a time
        for (int k = 0; k < STEPS; k++)
        {
            fillQuad(rib[i][k], rib[i][k + 1], rib[i + 1][k + 1], rib[i + 1][k], color);
        }

        // the panel's bottom edge curves up between rib ends, so cut that bit back out
        Pt a = rib[i][STEPS];
        Pt b = rib[i + 1][STEPS];
        Pt dip = {(a.x + b.x) / 2, min(a.y, b.y) - 12};
        Pt prev = a;
        for (int k = 1; k <= STEPS; k++)
        {
            Pt next = bezier(a, dip, b, k / (float)STEPS);
            frame.fillTriangle(a.x, a.y, prev.x, prev.y, next.x, next.y, TFT_BLACK);
            prev = next;
        }
    }

    // rib lines over the panels
    for (int i = 0; i <= RIBS; i++)
    {
        for (int k = 0; k < STEPS; k++)
        {
            frame.drawLine(rib[i][k].x, rib[i][k].y, rib[i][k + 1].x, rib[i][k + 1].y, RIB_RED);
        }
    }

    // the little knob on top
    frame.fillCircle(APEX.x, APEX.y - 6, 4, FINIAL_GREY);
}

static void rainBegin()
{
    setFaceScale(0.62f, 20); // smaller and lower, so the umbrella covers him
}

static void rainEnd()
{
    setFaceScale(1.0f, 0);
}

static void rainFront(unsigned long t)
{
    drawUmbrellaDrops(t); // first, so the canopy hides the ones falling onto it
    drawUmbrella();
}

// sun: overhead, with straight beams fanning down behind him

static const Pt SUN = {160, 36};
static const int SUN_R = 18;
static const int SUN_RAYS = 10;       // the short spikes around the sun
static const int BEAMS = 9;           // the long beams fanning down
static const float BEAM_LEN = 340;    // long enough to reach past the screen's edges
static const float BEAM_HALF = 0.05f; // half a beam's width, in radians

static void sunBegin()
{
    setExpression(SUN_SQUINT);
    setGazeTarget(0, -10); // looking up toward the sun
}

static void sunEnd()
{
    setExpression(NEUTRAL);
    clearGazeTarget();
}

// a point dist away from the sun's centre, in direction a (radians, 0 = right, PI/2 = down)
static Pt fromSun(float a, float dist)
{
    return {SUN.x + cosf(a) * dist, SUN.y + sinf(a) * dist};
}

static void sunBack(unsigned long t)
{
    float ts = t / 1000.0f;

    // beams fan out below the sun, from down-right round to down-left.
    // over a black background a see-through beam looks the same as a solid dark one,
    // so each beam is a plain colour blended once per frame, way cheaper than real transparency
    float sway = 0.04f * sinf(ts * 0.5f);
    for (int i = 0; i < BEAMS; i++)
    {
        float a = PI * 0.2f + i * PI * 0.075f + sway;
        // every other beam a little brighter, and each one pulses on its own
        int alpha = 31 + 15 * sinf(ts * 1.3f + i * 1.7f) + (i % 2 == 0 ? 13 : 0);
        uint16_t color = frame.alphaBlend(alpha, SUN_YELLOW, TFT_BLACK);

        Pt left = fromSun(a - BEAM_HALF, BEAM_LEN);
        Pt right = fromSun(a + BEAM_HALF, BEAM_LEN);
        frame.fillTriangle(SUN.x, SUN.y, left.x, left.y, right.x, right.y, color);
    }

    // the sun on top of its beams, with short spikes slowly turning around it
    frame.fillCircle(SUN.x, SUN.y, SUN_R, SUN_YELLOW);
    for (int i = 0; i < SUN_RAYS; i++)
    {
        float a = i * 2 * PI / SUN_RAYS + ts * 0.35f;
        float len = SUN_R + 13 + 2 * sinf(ts * 3 + i);

        // a thin triangle: a 5 px wide base just off the sun, coming to a point further out
        Pt base = fromSun(a, SUN_R + 5);
        float px = -sinf(a) * 2.5f, py = cosf(a) * 2.5f; // sideways from the spike's direction
        Pt tip = fromSun(a, len);
        frame.fillTriangle(base.x + px, base.y + py, base.x - px, base.y - py, tip.x, tip.y, SUN_YELLOW);
    }
}

// cloud: five clouds drifting at different heights and speeds, he watches the biggest

struct DriftingCloud
{
    float y;
    float scale;
    float speed; // px per second
    uint16_t color;
    float offset; // where it starts along its loop
};

static const DriftingCloud CLOUDS[] = {
    {20, 0.8f, 12, CLOUD_DARK, 0},
    {38, 1.1f, 20, CLOUD_MID, 160},
    {26, 0.6f, 30, CLOUD_GREY, 290},
    {64, 0.7f, 16, CLOUD_LIGHT, 60},
    {14, 0.9f, 24, CLOUD_LIGHT, 230},
};
static const int NUM_CLOUDS = sizeof(CLOUDS) / sizeof(CLOUDS[0]);
static const float CLOUD_LOOP = 460; // each cloud travels this far, then starts again off the left

static float cloudX(const DriftingCloud &cl, float ts)
{
    return fmodf(ts * cl.speed + cl.offset, CLOUD_LOOP) - 90;
}

static void cloudEnd()
{
    clearGazeTarget();
}

static void cloudFront(unsigned long t)
{
    float ts = t / 1000.0f;

    // watch the biggest cloud that's on screen
    float best = 0, targetX = 0;
    for (int i = 0; i < NUM_CLOUDS; i++)
    {
        float x = cloudX(CLOUDS[i], ts);
        if (CLOUDS[i].scale > best && x > 0 && x < 300)
        {
            best = CLOUDS[i].scale;
            targetX = (x + 24 * CLOUDS[i].scale - 160) / 3;
        }
    }
    // scenes only draw, but this is the simplest place to steer the eyes each frame
    setGazeTarget(targetX, -12);

    for (int i = 0; i < NUM_CLOUDS; i++)
    {
        drawCloud(cloudX(CLOUDS[i], ts), CLOUDS[i].y, CLOUDS[i].scale, CLOUDS[i].color);
    }
}

// snow: falling flakes, a snow bank along the bottom and a little snowman

static const int FLAKES = 26;
static const int GROUND_Y = 186;
static const Pt SNOWMAN = {36, 0}; // only x is used, it stands on the ground

static void snowEnd()
{
    clearGazeTarget();
}

static void drawSnowman()
{
    int x = SNOWMAN.x;
    frame.fillCircle(x, 174, 13, SNOW_WHITE); // bottom
    frame.fillCircle(x, 154, 9, SNOW_WHITE);  // middle
    frame.fillCircle(x, 139, 7, SNOW_WHITE);  // head

    frame.fillRect(x - 7, 126, 14, 8, HAT_DARK); // hat
    frame.fillRect(x - 10, 133, 20, 3, HAT_DARK);

    frame.fillRect(x - 3, 137, 2, 2, TFT_BLACK); // eyes
    frame.fillRect(x + 2, 137, 2, 2, TFT_BLACK);
    frame.fillTriangle(x, 140, x + 8, 141, x, 142, CARROT);

    frame.drawLine(x - 9, 152, x - 20, 146, TWIG_BROWN); // arms
    frame.drawLine(x + 9, 152, x + 20, 146, TWIG_BROWN);
}

static void snowFront(unsigned long t)
{
    float ts = t / 1000.0f;

    // every 7 seconds he spends 2 looking over at the snowman
    if (t % 7000 > 5000)
    {
        setGazeTarget(-45, 20);
    }
    else
    {
        clearGazeTarget();
    }

    for (int i = 0; i < FLAKES; i++)
    {
        float x = fmodf(hash01(i) * 320 + sinf(ts * 1.5f + i) * 8 + 320, 320);
        float y = fmodf(ts * 26 + hash01(i + 3) * 190, 190);
        if (y < GROUND_Y - 4) // flakes vanish into the snow bank
        {
            frame.fillCircle(x, y, 1 + hash01(i + 5) * 2, SNOW_WHITE);
        }
    }

    // a flat bank with bumps along the top
    frame.fillRect(SCENE_AREA.x, GROUND_Y, SCENE_AREA.w, SCENE_AREA.y + SCENE_AREA.h - GROUND_Y, SNOW_GROUND);
    for (int i = 0; i < 9; i++)
    {
        frame.fillEllipse(i * 40 + 10, GROUND_Y + 1, 26, 6, SNOW_GROUND);
    }

    drawSnowman();
}

// fog: four bands drifting both ways, see-through so his eyes still show

static const int FOG_BANDS = 4;

// a rounded band drawn on every other row, which looks see-through and costs nothing
static void drawFogBand(int x, int y, int w, int h)
{
    float r = h / 2.0f;
    for (int row = y; row < y + h; row += 2)
    {
        // pull the ends in near the top and bottom so the band has rounded ends
        float dy = row - (y + r);
        int inset = r - sqrtf(max(0.0f, r * r - dy * dy));
        frame.drawFastHLine(x + inset, row, w - 2 * inset, FOG_GREY);
    }
}

static void fogBegin()
{
    setExpression(FOG_SQUINT);
}

static void fogEnd()
{
    setExpression(NEUTRAL);
}

static void fogFront(unsigned long t)
{
    float ts = t / 1000.0f;
    for (int i = 0; i < FOG_BANDS; i++)
    {
        int y = 64 + i * 32;
        float dir = (i % 2) ? -1 : 1;
        // the + 440 keeps fmodf positive when the band moves left
        float off = fmodf(fmodf(dir * ts * (12 + i * 5) + i * 97, 440) + 440, 440) - 110;
        drawFogBand(off - 150, y, 290, 21);
        drawFogBand(off + 175, y + 5, 230, 17);
    }
}

// storm: dark cloud bank, slanted rain, worried eyes, lightning with a startle and a shake

static const unsigned long STORM_CYCLE_MS = 3400;

struct StormMoment
{
    uint32_t strike; // which strike this is, picks its side and shape
    unsigned long p; // ms since this strike
    bool flash;      // the double flicker
    float startle;   // 0..1..0 just after the strike
    int shake;       // px, the "thunder" a moment later
    int side;        // -1 left, 1 right
};

static StormMoment stormAt(unsigned long t)
{
    StormMoment m;
    m.strike = t / STORM_CYCLE_MS;
    m.p = t % STORM_CYCLE_MS;
    m.flash = m.p < 70 || (m.p > 130 && m.p < 200);
    m.startle = m.p < 450 ? sinf(m.p / 450.0f * PI) : 0;
    m.shake = 0;
    if (m.p > 250 && m.p < 700)
    {
        float fade = 1 - (m.p - 250) / 450.0f;
        m.shake = 3 * fade * sinf(m.p * 0.09f);
    }
    m.side = hash01(m.strike) > 0.5f ? 1 : -1;
    return m;
}

static void stormBegin()
{
    setExpression(WORRIED);
}

static void stormEnd()
{
    setExpression(NEUTRAL);
    setFaceNudge(0, 0);
    setFaceStretch(1.0f);
}

static void stormBack(unsigned long t)
{
    // the flash lights up everything behind him
    if (stormAt(t).flash)
    {
        frame.fillRect(SCENE_AREA.x, SCENE_AREA.y, SCENE_AREA.w, SCENE_AREA.h, STORM_FLASH);
    }
}

static void stormFront(unsigned long t)
{
    StormMoment m = stormAt(t);

    // he flinches away from the strike, then the thunder shakes him
    setFaceNudge(-m.side * 6 * m.startle + m.shake, -8 * m.startle);
    setFaceStretch(1 + 0.1f * m.startle);

    drawRainDrops(t, 34, 230, 0.35f, 30, 165, m.shake);

    for (int i = 0; i < 9; i++)
    {
        float s = 0.95f + hash01(i + 40) * 0.3f;
        drawCloud(-30 + i * 42 + m.shake, 8 + (i % 2) * 6, s, (i % 2) ? STORM_CLOUD_DARK : STORM_CLOUD);
    }

    if (m.flash)
    {
        // a jagged bolt off to one side, each strike gets its own shape
        int x = 160 + m.side * (95 + hash01(m.strike + 3) * 25) + m.shake;
        int y = 34;
        for (int s = 1; s <= 5; s++)
        {
            int nx = x + (hash01(m.strike * 7 + s) * 2 - 1) * 14;
            int ny = y + 22;
            for (int w = -1; w <= 1; w++)
            {
                frame.drawLine(x + w, y, nx + w, ny, LIGHTNING);
            }
            if (s == 2)
            {
                // a small branch partway down
                frame.drawLine(nx, ny, nx + m.side * 18, ny + 18, LIGHTNING);
            }
            x = nx;
            y = ny;
        }
    }
}

struct Scene
{
    const char *name;
    void (*begin)();                // runs once when the scene starts, or nullptr
    void (*end)();                  // runs once when it ends, to undo begin
    void (*back)(unsigned long t);  // drawn before the face, or nullptr
    void (*front)(unsigned long t); // drawn after the face, or nullptr
    Rect region;
};

// names have to match the server's Scene list exactly
static const Scene SCENES[] = {
    {"rain", rainBegin, rainEnd, nullptr, rainFront, SCENE_AREA},
    {"sun", sunBegin, sunEnd, sunBack, nullptr, SCENE_AREA},
    {"cloud", nullptr, cloudEnd, nullptr, cloudFront, SCENE_AREA},
    {"snow", nullptr, snowEnd, nullptr, snowFront, SCENE_AREA},
    {"fog", fogBegin, fogEnd, nullptr, fogFront, SCENE_AREA},
    {"storm", stormBegin, stormEnd, stormBack, stormFront, SCENE_AREA},
};
static const size_t NUM_SCENES = sizeof(SCENES) / sizeof(SCENES[0]);

static const Scene *current = nullptr; // nullptr = no scene
static unsigned long shownAt = 0;

// after a scene ends, its area still has the last frame on screen
static bool clearPending = false;
static Rect clearRegion = {0, 0, 0, 0};

void sceneShow(const char *name)
{
    for (size_t i = 0; i < NUM_SCENES; i++)
    {
        if (strcmp(SCENES[i].name, name) == 0)
        {
            // undo whatever the old scene did to the face before the new one starts
            if (current != nullptr && current->end != nullptr)
            {
                current->end();
            }
            current = &SCENES[i];
            shownAt = millis();
            if (current->begin != nullptr)
            {
                current->begin();
            }
            return;
        }
    }
    // "none", or a scene the board doesn't know yet
    sceneHide();
}

void updateScene()
{
    if (current != nullptr && millis() - shownAt >= SCENE_HOLD_MS)
    {
        sceneHide();
    }
}

// clip to the scene's area, so nothing can draw into the headline strip
// or outside the part of the screen that gets pushed
static void drawLayer(void (*layer)(unsigned long t))
{
    if (current == nullptr || layer == nullptr)
    {
        return;
    }
    const Rect &r = current->region;
    frame.setViewport(r.x, r.y, r.w, r.h, false); // false = keep using screen coordinates
    layer(millis() - shownAt);
    frame.resetViewport();
}

void drawSceneBack()
{
    if (current != nullptr)
    {
        drawLayer(current->back);
    }
}

void drawSceneFront()
{
    if (current != nullptr)
    {
        drawLayer(current->front);
    }
}

Rect sceneRegion()
{
    if (current == nullptr)
    {
        return {0, 0, 0, 0};
    }
    return current->region;
}

bool sceneTakeClear(Rect &out)
{
    if (!clearPending)
    {
        return false;
    }
    out = clearRegion;
    clearPending = false;
    return true;
}

bool sceneActive()
{
    return current != nullptr;
}

void sceneHide()
{
    if (current == nullptr)
    {
        return; // nothing showing, nothing to clear
    }
    // main wipes and pushes this area once, so the last frame doesn't stay on screen
    clearRegion = current->region;
    clearPending = true;
    if (current->end != nullptr)
    {
        current->end();
    }
    current = nullptr;
}