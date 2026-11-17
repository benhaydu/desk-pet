#include <Arduino.h>
#include <TFT_eSPI.h>
#include "colors.h"
#include "vine.h"

extern TFT_eSprite frame;

static constexpr uint16_t POT = rgb565(196, 102, 62);
static constexpr uint16_t RIM = rgb565(220, 128, 86);
static constexpr uint16_t LIP = rgb565(238, 152, 108);      // the top edge of the rim, catching the light
static constexpr uint16_t POT_INSIDE = rgb565(130, 64, 38); // the inside of the back wall, in shadow
static constexpr uint16_t SOIL = rgb565(90, 60, 40);
static constexpr uint16_t STEM = rgb565(90, 170, 80);
static constexpr uint16_t LEAF = rgb565(110, 200, 95);
static constexpr uint16_t LEAF_DARK = rgb565(80, 160, 70); // every other leaf, so neighbours don't blend together
static constexpr uint16_t PETALS[] = {rgb565(240, 130, 160), rgb565(250, 190, 90), rgb565(170, 140, 240)};
static constexpr uint16_t FLOWER_MIDDLE = rgb565(255, 235, 150);

// the pot sits in the bottom right corner. Seen from a little above, so the top of the
// rim is an oval: the back of the lip shows above the soil, and the front is the rim's band
static const int POT_X = 258;      // its middle
static const int POT_BOTTOM = 196;
static const int BODY_TOP = POT_BOTTOM - 22;
static const int RIM_TOP = POT_BOTTOM - 30;  // the middle of the oval opening
static const int RIM_HALF_W = 23;
static const int SOIL_Y = RIM_TOP + 1;       // height 0 of the vine: the middle of the soil
static const int FULL_HEIGHT = 145;          // full grown, the tip is at y 22

// it winds side to side as it climbs: 16 px each way, one full wave about every 140 px.
// The winding fades in over the first 28 px, so it comes straight up out of the middle of
// the soil before it starts to wander
static const float WIND_PX = 16;
static const float WIND_SCALE = 22;
static const float WIND_RAMP = 28;

static const int STEM_STEP = 3; // the stem is short straight pieces this tall
static const int STEM_WIDTH = 4;

// a leaf every 22 px of vine, alternating sides. Each one unfurls over the 18 px of
// growth after the vine passes it, so they grow in instead of appearing
static const int LEAF_FIRST = 18;
static const int LEAF_GAP = 22;
static const int LEAF_COUNT = (FULL_HEIGHT - LEAF_FIRST + LEAF_GAP - 1) / LEAF_GAP; // 6
static const float LEAF_LEN = 20;
static const float LEAF_UNFURL = 18;
static const float LEAF_WIDTH = 0.34f; // half its width at the widest point, as a fraction of its length
static const int LEAF_SIDES = 6;       // points along each edge of the leaf shape

// flowers sit just past every other leaf's tip, plus a bigger one on top
static const float FLOWER_R = 7;
static const float TOP_FLOWER = 1.4f;
static const float FLOWER_OUT_X = 16, FLOWER_OUT_Y = -6;
static const float POP_STAGGER = 0.18f; // each flower starts this far into the bloom after the one below
static const float POP_LENGTH = 0.3f;   // and takes this much of it to open

static float clamp01(float v)
{
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

// overshoots a little past 1 and settles back, so the flowers pop
static float easeOutBack(float t)
{
    const float c1 = 1.70158f;
    const float c3 = c1 + 1;
    float u = t - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}

// the vine's middle at a height above the soil
static float stemX(float h)
{
    float ramp = clamp01(h / WIND_RAMP);
    ramp = ramp * ramp * (3 - 2 * ramp); // eases in and out, so there's no kink where it starts
    return POT_X + WIND_PX * ramp * sinf(h / WIND_SCALE);
}

static float stemY(float h)
{
    return SOIL_Y - h;
}

int vineGrowthPx(float growth)
{
    return (int)(clamp01(growth) * FULL_HEIGHT + 0.5f);
}

void vineTip(float growth, int &x, int &y)
{
    float h = vineGrowthPx(growth);
    x = (int)stemX(h);
    y = (int)stemY(h);
}

static void drawPot(int shiftX)
{
    int x = POT_X + shiftX;

    // the body narrows toward the bottom: a trapezoid as two triangles
    frame.fillTriangle(x - 20, BODY_TOP, x + 20, BODY_TOP, x + 15, POT_BOTTOM, POT);
    frame.fillTriangle(x - 20, BODY_TOP, x + 15, POT_BOTTOM, x - 15, POT_BOTTOM, POT);

    // the rim: a band in front, with the oval of its top edge on top of that, a shade lighter
    // so the two read as separate faces
    frame.fillRoundRect(x - RIM_HALF_W, RIM_TOP, 2 * RIM_HALF_W + 1, 9, 2, RIM);
    frame.fillEllipse(x, RIM_TOP, RIM_HALF_W, 5, LIP);

    // inside the lip: the shaded back wall, then the soil a pixel lower so a sliver
    // of that wall shows above it. That sliver is what makes the pot read as a pot
    frame.fillEllipse(x, RIM_TOP, RIM_HALF_W - 3, 3, POT_INSIDE);
    frame.fillEllipse(x, SOIL_Y, RIM_HALF_W - 4, 2, SOIL);
}

// a pointed oval from its base along an angle, drawn as a fan of triangles from the base.
// The shape is convex, so a fan from any point on its edge covers it exactly
static void drawLeaf(float bx, float by, float len, float angleDeg, uint16_t colour)
{
    float a = angleDeg * PI / 180;
    float ca = cosf(a), sa = sinf(a);

    // the outline in the leaf's own direction: along it is u, across it is v.
    // Up one edge to the tip, back down the other
    float px[2 * LEAF_SIDES];
    float py[2 * LEAF_SIDES];
    int n = 0;
    for (int i = 1; i <= LEAF_SIDES; i++)
    {
        float t = i / (float)LEAF_SIDES;
        float u = t * len, v = -sinf(t * PI) * len * LEAF_WIDTH;
        px[n] = bx + u * ca - v * sa;
        py[n] = by + u * sa + v * ca;
        n++;
    }
    for (int i = LEAF_SIDES - 1; i >= 0; i--)
    {
        float t = i / (float)LEAF_SIDES;
        float u = t * len, v = sinf(t * PI) * len * LEAF_WIDTH;
        px[n] = bx + u * ca - v * sa;
        py[n] = by + u * sa + v * ca;
        n++;
    }
    for (int i = 0; i + 1 < n; i++)
    {
        frame.fillTriangle(bx, by, px[i], py[i], px[i + 1], py[i + 1], colour);
    }
}

static void drawFlower(float x, float y, float r, uint16_t colour)
{
    if (r < 1)
    {
        return;
    }
    // five round petals around a middle, the first pointing straight up. drawSpot takes a
    // float radius, so a flower opening grows smoothly instead of in whole-pixel steps
    for (int k = 0; k < 5; k++)
    {
        float a = k * 2 * PI / 5 - PI / 2;
        frame.drawSpot(x + cosf(a) * r, y + sinf(a) * r, r * 0.75f, colour);
    }
    frame.drawSpot(x, y, r * 0.55f, FLOWER_MIDDLE);
}

void drawVine(float growth, float bloom, int shiftX)
{
    drawPot(shiftX);

    float h = vineGrowthPx(growth);
    if (h <= 0)
    {
        return;
    }

    // the stem, bottom to top. No background colour given, so the smooth edges blend with
    // whatever's under them (the soil, the piece before) instead of with black. That's why
    // timerview always clears the vine's box before drawing it
    // starting a pixel up, so the stem's rounded end stays inside the soil and off the rim
    float lastX = stemX(1) + shiftX, lastY = stemY(1);
    for (float s = STEM_STEP; s < h + STEM_STEP; s += STEM_STEP)
    {
        float at = s < h ? s : h; // the last piece stops exactly at the tip
        float x = stemX(at) + shiftX, y = stemY(at);
        frame.drawWideLine(lastX, lastY, x, y, STEM_WIDTH, STEM);
        lastX = x;
        lastY = y;
    }

    // leaves, and where each odd one's flower goes. Folded leaves get no flower, so the
    // flowers go away with their leaves when the vine pulls back in
    float flowerX[LEAF_COUNT / 2 + 1], flowerY[LEAF_COUNT / 2 + 1], flowerSize[LEAF_COUNT / 2 + 1];
    int flowers = 0;
    for (int i = 0; i < LEAF_COUNT; i++)
    {
        float lh = LEAF_FIRST + i * LEAF_GAP;
        float open = clamp01((h - lh) / LEAF_UNFURL);
        if (open <= 0)
        {
            break; // the rest are higher up
        }
        float x = stemX(lh) + shiftX, y = stemY(lh);
        bool right = i % 2 == 0;
        // pointing up and out: 25 degrees above horizontal on either side
        drawLeaf(x, y, LEAF_LEN * open, right ? -25 : 205, right ? LEAF : LEAF_DARK);
        if (!right)
        {
            flowerX[flowers] = x - FLOWER_OUT_X;
            flowerY[flowers] = y + FLOWER_OUT_Y;
            flowerSize[flowers] = open;
            flowers++;
        }
    }

    if (bloom <= 0)
    {
        // still growing: a little curl at the tip, which the top flower covers later
        if (h < FULL_HEIGHT)
        {
            float cx = stemX(h) + shiftX, cy = stemY(h) - 4;
            float px = cx + 6 * cosf(150 * PI / 180), py = cy + 6 * sinf(150 * PI / 180);
            for (int deg = 180; deg <= 420; deg += 30)
            {
                float a = deg * PI / 180;
                float nx = cx + 6 * cosf(a), ny = cy + 6 * sinf(a);
                frame.drawWideLine(px, py, nx, ny, 3, STEM);
                px = nx;
                py = ny;
            }
        }
        return;
    }

    // and the top one, on the tip
    flowerX[flowers] = stemX(h) + shiftX;
    flowerY[flowers] = stemY(h);
    flowerSize[flowers] = TOP_FLOWER;
    flowers++;

    // bottom to top, each one popping open a little after the one below it
    for (int n = 0; n < flowers; n++)
    {
        float t = clamp01((bloom - n * POP_STAGGER) / POP_LENGTH);
        if (t <= 0)
        {
            continue;
        }
        drawFlower(flowerX[n], flowerY[n], FLOWER_R * flowerSize[n] * easeOutBack(t), PETALS[n % 3]);
    }
}