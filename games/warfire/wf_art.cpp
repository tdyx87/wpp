/* ===========================================================================
 *  wf_art.cpp -- Warfire : sprite atlas
 *  See wf_art.h.  Everything here is generated at run time.
 * ===========================================================================
 */
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wsfont.h"

#include "wf_art.h"
#include "wf_text.h"

namespace wf {

static const unsigned long COL_OUTLINE = 0x0C1016;
static const unsigned long COL_ROCK_HI = 0x9A9AA2;
static const unsigned long COL_WAVE    = 0x4A7AB0;
static const unsigned long COL_TRUNK   = 0x4A3620;
static const unsigned long COL_LEAF_HI = 0x2C6430;
static const unsigned long COL_BOW     = 0x8A6A3A;
static const unsigned long COL_STRING  = 0xE8E0C0;
static const unsigned long COL_HAFT    = 0x6A5A4A;
static const unsigned long COL_HAMMER  = 0x9A8A78;
static const unsigned long COL_EYE     = 0xFFE24A;
static const unsigned long COL_CROWN   = 0xFFF0A0;

/* The colour BLIT_MASK treats as "leave the destination alone".  It is NOT
   black: blit16.asm loads mm7 from `mask dd 0xf81ff81f`, i.e. 0xF81F in
   RGB565 -- magenta.  Baking sprites on a black background therefore paints
   a black box around every sprite.  Additive blits want the opposite: they
   add the source, so their empty colour has to be black.  Every sprite says
   which one it wants. */
static const unsigned long COL_MASK    = 0xFF00FF;   /* -> 0xF81F          */
static const unsigned long COL_NOTHING = 0x000000;   /* additive no-op     */

/* Added on top of a sprite that has just been hit, to make it flare. */
static const unsigned long COL_FLASH   = 0x606060;

/* ------------------------------------------------------------------ canvas */

struct Grid {
    int w, h;
    unsigned char mask[SPR_MAX * SPR_MAX];
    unsigned long col [SPR_MAX * SPR_MAX];

    void init(int ww, int hh)
    {
        w = ww; h = hh;
        memset(mask, 0, sizeof(mask));
        memset(col,  0, sizeof(col));
    }
    void put(int x, int y, unsigned long c)
    {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        mask[y * w + x] = 1;
        col [y * w + x] = c;
    }
    void rect(int x0, int y0, int x1, int y1, unsigned long c)
    {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) put(x, y, c);
    }
    void circle(int cx, int cy, int r, unsigned long c)
    {
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if (x * x + y * y <= r * r) put(cx + x, cy + y, c);
    }
    void ellipse(int cx, int cy, int rx, int ry, unsigned long c)
    {
        if (rx < 1) rx = 1;
        if (ry < 1) ry = 1;
        for (int y = -ry; y <= ry; ++y)
            for (int x = -rx; x <= rx; ++x) {
                float fx = (float)x / (float)rx;
                float fy = (float)y / (float)ry;
                if (fx * fx + fy * fy <= 1.0f) put(cx + x, cy + y, c);
            }
    }
    void line(int x0, int y0, int x1, int y1, unsigned long c)
    {
        int dx = abs(x1 - x0), dy = abs(y1 - y0);
        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;
        for (;;) {
            put(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = err * 2;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 <  dx) { err += dx; y0 += sy; }
        }
    }
    void arc(int cx, int cy, int r, float a0, float a1, unsigned long c)
    {
        int steps = r * 10 + 10;
        for (int i = 0; i <= steps; ++i) {
            float a = a0 + (a1 - a0) * (float)i / (float)steps;
            put(cx + (int)(cosf(a) * (float)r + 0.5f),
                cy + (int)(sinf(a) * (float)r + 0.5f), c);
        }
    }
};

/* Bakes the grid into a WBitmap.
   `empty`    is written where the shape is not: COL_MASK for sprites that
              will be blitted with BLIT_MASK, black for additive ones.
   `outline`  adds a 1px dark rim so the sprite reads against the terrain.
   `override` when non-zero, replaces every body colour -- used to bake the
              flat silhouette that flares when an actor is hit. */
static void bake(const Grid &g, WBitmap &out, int flipX, int flipY,
                 int outline, unsigned long empty, unsigned long override)
{
    out.Create(g.w, g.h);

    static const int nx[4] = { 1, -1, 0, 0 };
    static const int ny[4] = { 0, 0, 1, -1 };

    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            int sx = flipX ? (g.w - 1 - x) : x;
            int sy = flipY ? (g.h - 1 - y) : y;
            int i  = sy * g.w + sx;

            unsigned long c = empty;
            if (g.mask[i]) {
                c = override ? override : g.col[i];
            } else if (outline) {
                for (int k = 0; k < 4; ++k) {
                    int ax = sx + nx[k], ay = sy + ny[k];
                    if (ax < 0 || ay < 0 || ax >= g.w || ay >= g.h) continue;
                    if (g.mask[ay * g.w + ax]) { c = COL_OUTLINE; break; }
                }
            }
            out[y][x] = WPixel((DWORD)c);
        }
    }
}

/* ------------------------------------------------- soft shadow + vignette */
/*  Both of these are blitted with BLIT_SUBTRACT, which subtracts the source
 *  pixel from the destination pixel channel by channel.  That makes them the
 *  one easy way to get a *gradient* out of this library: BLIT_MASK is a binary
 *  key and BLIT_MASK|BLIT_TRANS would give every pixel the same single alpha,
 *  so a real falloff has to be carried in the pixel values.  Writing pure
 *  black outside the shape therefore costs nothing there.
 * ------------------------------------------------------------------------- */

static WBitmap *bakeShadow(int rx, int ry)
{
    WBitmap *b = new WBitmap();
    const int w = 2 * rx + 4, h = 2 * ry + 3;
    b->Create(w, h);

    const int cx = w / 2, cy = h / 2;
    /* COL_SHADOW is only 0x101418, i.e. 2/5/3 steps deep in 565 -- just enough
       to read as "this actor is standing on something". */
    const int sr = (int)((COL_SHADOW >> 16) & 0xFF);
    const int sg = (int)((COL_SHADOW >> 8) & 0xFF);
    const int sb = (int)(COL_SHADOW & 0xFF);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float dx = (float)(x - cx) / (float)(rx + 1);
            float dy = (float)(y - cy) / (float)(ry + 1);
            float t  = 1.0f - (dx * dx + dy * dy);
            if (t < 0.02f) t = 0.0f;
            if (t > 1.0f)  t = 1.0f;
            (*b)[y][x] = WPixel((int)(sr * t), (int)(sg * t), (int)(sb * t));
        }

    b->SetPos(0, 0);
    return b;
}

/* Darkens the screen edge and leaves the middle exactly alone.  The camera
   keeps the hero near the centre, so this frames the action without hiding
   any of it, and it hides the flatness of the RGB565 terrain palette. */
static WBitmap *bakeVignette(void)
{
    WBitmap *b = new WBitmap();
    b->Create(FIELD_W, FIELD_H);

    const float cx = FIELD_W * 0.5f, cy = FIELD_H * 0.5f;
    const float inner = 0.62f, span = 1.0f - inner;

    for (int y = 0; y < FIELD_H; ++y)
        for (int x = 0; x < FIELD_W; ++x) {
            float dx = (float)(x - cx) / cx;
            float dy = (float)(y - cy) / cy;
            /* scaled so the corner is 1.0 and the mid-edge is 0.707 */
            float d = sqrtf(dx * dx + dy * dy) * 0.7071f;
            float t = (d - inner) / span;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            t *= t;                                  /* ease in, not linear */
            (*b)[y][x] = WPixel((int)(26.0f * t), (int)(32.0f * t), (int)(26.0f * t));
        }

    return b;
}

/* ------------------------------------------------------------------- tiles */

static void tileGrass(Grid &g, unsigned &rng)
{
    g.init(TILE_SIZE, TILE_SIZE);
    for (int y = 0; y < TILE_SIZE; ++y)
        for (int x = 0; x < TILE_SIZE; ++x) {
            unsigned r = wf_rand(rng);
            g.put(x, y, ((r & 15u) == 0u) ? COL_GRASS_B : COL_GRASS_A);
        }
    for (int n = 0; n < 18; ++n) {
        int x = 2 + (int)(wf_rand(rng) % 28u);
        int y = 3 + (int)(wf_rand(rng) % 26u);
        g.put(x, y, COL_GRASS_B);
        g.put(x, y - 1, COL_GRASS_B);
        g.put(x + 1, y, COL_GRASS_B);
    }
}

static void tileDirt(Grid &g, unsigned &rng)
{
    g.init(TILE_SIZE, TILE_SIZE);
    for (int y = 0; y < TILE_SIZE; ++y)
        for (int x = 0; x < TILE_SIZE; ++x) {
            unsigned r = wf_rand(rng);
            g.put(x, y, ((r & 7u) == 0u) ? COL_DIRT_B : COL_DIRT_A);
        }
    for (int n = 0; n < 20; ++n) {
        int x = (int)(wf_rand(rng) % 32u);
        int y = (int)(wf_rand(rng) % 32u);
        g.put(x, y, COL_DIRT_B);
        g.put(x + 1, y + 1, COL_DIRT_B);
    }
}

static void tileRock(Grid &g, unsigned &rng)
{
    tileGrass(g, rng);
    g.ellipse(16, 21, 12, 8, COL_ROCK_B);
    g.ellipse(16, 18, 11, 7, COL_ROCK_A);
    g.ellipse(12, 15, 4, 3, COL_ROCK_HI);
}

static void tileWater(Grid &g, unsigned &rng)
{
    g.init(TILE_SIZE, TILE_SIZE);
    for (int y = 0; y < TILE_SIZE; ++y)
        for (int x = 0; x < TILE_SIZE; ++x) {
            unsigned r = wf_rand(rng);
            g.put(x, y, ((r & 7u) == 0u) ? COL_WATER_B : COL_WATER_A);
        }
    for (int n = 0; n < 5; ++n) {
        int y = 3 + (int)(wf_rand(rng) % 26u);
        int x = (int)(wf_rand(rng) % 18u);
        g.rect(x, y, x + 11, y, COL_WAVE);
    }
}

static void tileTree(Grid &g, unsigned &rng)
{
    tileGrass(g, rng);
    g.rect(14, 19, 17, 31, COL_TRUNK);
    g.circle(16, 14, 11, COL_TREE_B);
    g.circle(15, 12, 9, COL_TREE_A);
    g.circle(11, 9, 4, COL_LEAF_HI);
}

/* -------------------------------------------------------------------- hero */

/* 24x24.  view: 0 = facing the camera, 1 = back to camera, 2 = side on. */
static void heroGrid(Grid &g, int view, int unused)
{
    (void)unused;
    g.init(24, 24);

    /* no baked shadow here: Renderer draws a soft one at the feet */

    if (view == 1) {                                   /* back to camera */
        g.rect(8, 16, 10, 21, COL_HERO_B);
        g.rect(13, 16, 15, 21, COL_HERO_B);
        g.rect(6, 11, 17, 19, COL_HERO_B);             /* cloak          */
        g.rect(7, 11, 16, 18, COL_HERO_A);
        g.rect(5, 11, 6, 16, COL_HERO_B);
        g.rect(17, 11, 18, 16, COL_HERO_B);
        g.circle(12, 7, 4, COL_HERO_HAIR);
        g.rect(8, 2, 16, 6, COL_HERO_HAIR);
        g.rect(19, 5, 20, 15, COL_BLADE);
        g.put(19, 4, COL_BLADE);
        g.put(20, 4, COL_BLADE);
        g.rect(18, 15, 21, 17, COL_UI_EDGE);
        return;
    }

    if (view == 2) {                                   /* side on        */
        g.rect(9, 16, 11, 21, COL_HERO_B);
        g.rect(13, 16, 15, 21, COL_HERO_B);
        g.rect(7, 11, 15, 19, COL_HERO_B);
        g.rect(8, 11, 14, 18, COL_HERO_A);
        g.rect(6, 12, 7, 16, COL_HERO_B);
        g.circle(11, 7, 4, COL_HERO_SKIN);
        g.rect(8, 2, 14, 5, COL_HERO_HAIR);
        g.put(13, 7, COL_OUTLINE);
        g.rect(18, 6, 19, 16, COL_BLADE);              /* blade forward  */
        g.put(18, 5, COL_BLADE);
        g.put(19, 5, COL_BLADE);
        g.rect(16, 14, 19, 16, COL_UI_EDGE);
        return;
    }

    /* view 0: facing the camera */
    g.rect(8, 16, 10, 21, COL_HERO_B);                 /* legs           */
    g.rect(13, 16, 15, 21, COL_HERO_B);
    g.rect(6, 11, 17, 19, COL_HERO_B);                 /* cloak          */
    g.rect(7, 11, 16, 18, COL_HERO_A);                 /* torso          */
    g.rect(7, 17, 16, 18, COL_UI_EDGE);                /* belt           */
    g.rect(5, 11, 6, 16, COL_HERO_B);                  /* arms           */
    g.rect(17, 11, 18, 16, COL_HERO_B);
    g.circle(12, 7, 4, COL_HERO_SKIN);                 /* head           */
    g.rect(8, 2, 16, 5, COL_HERO_HAIR);                /* helm           */
    g.put(7, 6, COL_HERO_HAIR);
    g.put(17, 6, COL_HERO_HAIR);
    g.put(10, 7, COL_OUTLINE);                         /* eyes           */
    g.put(14, 7, COL_OUTLINE);
    g.rect(19, 5, 20, 15, COL_BLADE);                  /* blade          */
    g.put(19, 4, COL_BLADE);
    g.put(20, 4, COL_BLADE);
    g.rect(18, 15, 21, 17, COL_UI_EDGE);               /* hilt           */
}

/* ---------------------------------------------------------------- monsters */

/* S x S, where S depends on the kind.  view: 0 front, 1 back, 2 side. */
static void monGrid(Grid &g, int kind, int view)
{
    int S = 24;
    if (kind == MON_BRUTE) S = 28;
    if (kind == MON_BOSS)  S = 36;
    g.init(S, S);

    const int cx    = S / 2;
    const int feet  = S - 2;
    const int headR = (S >= 36) ? 7 : 5;
    const int headY = feet - 17;
    const int tw    = S / 4;                    /* half torso width      */
    const int lw    = S / 8;                    /* leg half width        */

    /* no baked shadow: Renderer draws a soft one at the feet */

    unsigned long bodyA, bodyB;
    switch (kind) {
    case MON_ARCHER: bodyA = COL_ARCHER_A; bodyB = COL_ARCHER_B; break;
    case MON_BRUTE:  bodyA = COL_BRUTE_A;  bodyB = COL_BRUTE_B;  break;
    case MON_BOSS:   bodyA = COL_BOSS_A;   bodyB = COL_BOSS_B;   break;
    default:         bodyA = COL_GRUNT_A;  bodyB = COL_GRUNT_B;  break;
    }


    /* legs */
    g.rect(cx - lw - 3, feet - 6, cx - 2, feet, bodyB);
    g.rect(cx + 2, feet - 6, cx + lw + 3, feet, bodyB);

    /* torso: narrow shoulders, wide hips */
    g.rect(cx - tw, feet - 13, cx + tw, feet - 5, bodyA);
    g.rect(cx - tw + 2, feet - 16, cx + tw - 2, feet - 12, bodyA);

    /* arms */
    g.rect(cx - tw - 4, feet - 14, cx - tw - 2, feet - 7, bodyB);
    g.rect(cx + tw + 2, feet - 14, cx + tw + 4, feet - 7, bodyB);

    /* head */
    g.circle(cx, headY, headR, bodyA);

    /* eyes */
    if (view != 1) {
        if (view == 2) {
            g.put(cx + 2, headY - 1, COL_EYE);
        } else {
            g.put(cx - 2, headY - 1, COL_EYE);
            g.put(cx + 2, headY - 1, COL_EYE);
        }
    }

    /* props that tell the kinds apart */
    if (kind == MON_GRUNT) {
        g.put(cx - 3, headY - headR - 1, bodyB);
        g.put(cx - 4, headY - headR,     bodyB);
        g.put(cx + 3, headY - headR - 1, bodyB);
        g.put(cx + 4, headY - headR,     bodyB);
    } else if (kind == MON_ARCHER) {
        int bx = cx + tw + 7;
        int by = feet - 12;
        g.arc(bx, by, 9, -1.25f, 1.25f, COL_BOW);
        g.line(bx + 3, by - 9, bx + 3, by + 9, COL_STRING);
    } else if (kind == MON_BRUTE) {
        int hx = cx + tw + 5;
        int hy = feet - 13;
        g.rect(hx, hy, hx + 2, hy + 11, COL_HAFT);
        g.rect(hx - 3, hy + 10, hx + 5, hy + 16, COL_HAMMER);
    } else {
        g.put(cx - headR + 1, headY - headR - 1, bodyB);
        g.put(cx - headR,     headY - headR - 3, bodyB);
        g.put(cx + headR - 1, headY - headR - 1, bodyB);
        g.put(cx + headR,     headY - headR - 3, bodyB);
        g.rect(cx - 2, headY - headR - 2, cx + 2, headY - headR + 1, COL_CROWN);
    }
}

/* --------------------------------------------------------------- effects */

/* A crescent band in the swing direction; drawn as a fading arc. */
static void slashGrid(Grid &g, int facing, int unused)
{
    (void)unused;
    const int S = 46;
    g.init(S, S);

    const int cx = S / 2, cy = S / 2;

    float base;
    switch (facing) {
    case FACE_DOWN: base =  1.5707963f; break;
    case FACE_UP:   base = -1.5707963f; break;
    case FACE_LEFT: base =  3.1415927f; break;
    default:        base =  0.0f;       break;
    }

    for (int r = 13; r <= 19; ++r)
        for (int i = -11; i <= 11; ++i) {
            float a = base + (float)i * 0.052f;
            unsigned long c = (r >= 17) ? 0xFFFFFF : 0xBFE8FF;
            g.put(cx + (int)(cosf(a) * (float)r + 0.5f),
                  cy + (int)(sinf(a) * (float)r + 0.5f), c);
        }
}

/* A hollow ring, no outline: rings are meant to read as light, not as art. */
static void ringGrid(Grid &g, int size, int unused)
{
    (void)unused;
    g.init(size, size);

    const int cx = size / 2, cy = size / 2;
    const int r  = size / 2 - 3;
    const int steps = size * 24;

    for (int i = 0; i < steps; ++i) {
        float a = (float)i * 6.2831853f / (float)steps;
        g.put(cx + (int)(cosf(a) * (float)r + 0.5f),
              cy + (int)(sinf(a) * (float)r + 0.5f), 0xFFFFFF);
    }
    for (int i = 0; i < steps; ++i) {
        float a = (float)i * 6.2831853f / (float)steps;
        g.put(cx + (int)(cosf(a) * (float)(r - 1) + 0.5f),
              cy + (int)(sinf(a) * (float)(r - 1) + 0.5f), 0xDCEFFF);
    }
}

static void sparkGrid(Grid &g, int unused0, int unused1)
{
    (void)unused0;
    (void)unused1;
    g.init(4, 4);
    g.rect(0, 0, 3, 3, 0xFFFFFF);
}

static void coinGrid(Grid &g, int unused0, int unused1)
{
    (void)unused0;
    (void)unused1;
    g.init(14, 14);
    g.circle(7, 6, 5, COL_DMG_CRIT);
    g.circle(7, 6, 3, 0xFFF0A0);
    g.put(7, 6, 0xE0A020);
    g.put(7, 5, 0xE0A020);
}

static void potionGrid(Grid &g, int unused0, int unused1)
{
    (void)unused0;
    (void)unused1;
    g.init(14, 16);
    g.rect(5, 1, 8, 3, 0xC8C8D0);          /* cork   */
    g.rect(4, 3, 9, 5, 0xA8A8B4);          /* neck   */
    g.circle(7, 10, 5, 0xD03030);          /* flask  */
    g.circle(7, 9, 4, 0xE85050);
    g.ellipse(5, 8, 2, 2, 0xFFA0A0);       /* glint  */
}

/* ------------------------------------------------------------- bake helpers */

static WBitmap *bakeSprite(void (*fn)(Grid &, int, int), int a0, int a1,
                           int flipX, int flipY, int outline,
                           unsigned long empty, unsigned long override)
{
    /* Grid is ~80 KB once SPR_MAX covers the shock ring, so it lives on the
       heap rather than in a stack frame. */
    Grid *g = new Grid();
    fn(*g, a0, a1);

    WBitmap *b = new WBitmap();
    bake(*g, *b, flipX, flipY, outline, empty, override);

    delete g;
    return b;
}

static void bakeGlyph(WBitmap *&out, int &outW, WFont &font,
                      const char *s, unsigned long color, int extra)
{
    int w = font.GetLength(s);
    if (w < 1) w = 1;

    /* one extra pixel each way so the drop shadow has somewhere to land */
    const int pad = extra + 1;

    out = new WBitmap();
    out->Create(w + pad, font.GetHeight() + pad);

    /* The padding has to be the BLIT_MASK key, not black -- see COL_MASK.
       The glyph itself is rasterised without anti-aliasing: _font16_alpha
       blends a half-covered edge pixel with whatever is behind it, and with
       a magenta padding that shows up as a pink fringe around every letter.
       _font16_noalpha instead thresholds coverage at 4/16 and writes either
       the pure colour or nothing, which is both clean and period-correct.

       The shadow is painted first and the letter on top of it, so the two
       only need one bitmap between them.  Light HUD text sitting directly
       on light terrain is unreadable without it. */
    out->Clear(WPixel((DWORD)COL_MASK));

    out->SetPos(1, 1);
    font.Draw(*out, s, WPixel((DWORD)COL_SHADOW), FONT_NOALPHA);

    out->SetPos(0, 0);
    font.Draw(*out, s, WPixel((DWORD)color), FONT_NOALPHA);

    out->SetPos(0, 0);
    outW = w + pad;
}

/* -------------------------------------------------------------------- atlas */

Atlas::Atlas() : m_ok(false)
{
    memset(m_tile,  0, sizeof(m_tile));
    memset(m_hero,  0, sizeof(m_hero));
    memset(m_mon,   0, sizeof(m_mon));
    memset(m_heroFlash, 0, sizeof(m_heroFlash));
    memset(m_monFlash,  0, sizeof(m_monFlash));
    memset(m_digit, 0, sizeof(m_digit));
    memset(m_label, 0, sizeof(m_label));
    memset(m_slash, 0, sizeof(m_slash));
    memset(m_ring,  0, sizeof(m_ring));
    m_title  = 0;
    m_titleW = 0;
    m_spark = 0;
    m_coin  = 0;
    m_potion = 0;
    memset(m_shadow, 0, sizeof(m_shadow));
    m_vignette = 0;
    memset(m_labelW, 0, sizeof(m_labelW));
    memset(m_digitW, 0, sizeof(m_digitW));
    memset(m_digitH, 0, sizeof(m_digitH));
}

Atlas::~Atlas()
{
    Destroy();
}

void Atlas::Destroy()
{
    for (int i = 0; i < TILE_COUNT; ++i) { delete m_tile[i]; m_tile[i] = 0; }
    for (int i = 0; i < FACE_COUNT; ++i) { delete m_hero[i]; m_hero[i] = 0; }
    for (int k = 0; k < MON_KIND_COUNT; ++k)
        for (int f = 0; f < FACE_COUNT; ++f) { delete m_mon[k][f]; m_mon[k][f] = 0; }
    for (int i = 0; i < FACE_COUNT; ++i) { delete m_heroFlash[i]; m_heroFlash[i] = 0; }
    for (int k = 0; k < MON_KIND_COUNT; ++k)
        for (int f = 0; f < FACE_COUNT; ++f) { delete m_monFlash[k][f]; m_monFlash[k][f] = 0; }
    for (int s = 0; s < DIGIT_SET_COUNT; ++s)
        for (int d = 0; d < 10; ++d) { delete m_digit[s][d]; m_digit[s][d] = 0; }
    for (int i = 0; i < WF_LABEL_COUNT; ++i) { delete m_label[i]; m_label[i] = 0; }
    for (int i = 0; i < FACE_COUNT; ++i) { delete m_slash[i]; m_slash[i] = 0; }
    for (int i = 0; i < 2; ++i) { delete m_ring[i]; m_ring[i] = 0; }
    delete m_title;  m_title = 0;  m_titleW = 0;
    delete m_spark; m_spark = 0;
    delete m_coin;  m_coin = 0;
    delete m_potion; m_potion = 0;
    for (int i = 0; i < SHADOW_TIERS; ++i) { delete m_shadow[i]; m_shadow[i] = 0; }
    delete m_vignette; m_vignette = 0;
    m_ok = false;
}

bool Atlas::Build()
{
    Destroy();

    /* ---- tiles --------------------------------------------------------- */
    unsigned rng = 0x13579BDFu;
    Grid *gp = new Grid();
    Grid &g = *gp;
    for (int t = 0; t < TILE_COUNT; ++t) {
        switch (t) {
        case TILE_GRASS: tileGrass(g, rng); break;
        case TILE_DIRT:  tileDirt(g, rng);  break;
        case TILE_ROCK:  tileRock(g, rng);  break;
        case TILE_WATER: tileWater(g, rng); break;
        default:         tileTree(g, rng);  break;
        }
        m_tile[t] = new WBitmap();
        bake(g, *m_tile[t], 0, 0, 1, COL_NOTHING, 0);
    }
    delete gp;

    /* ---- hero ---------------------------------------------------------- */
    /* every actor is baked twice: the real sprite keyed on magenta, and a
       flat silhouette on black that is added on top when the actor is hit */
    m_hero[FACE_DOWN]  = bakeSprite(heroGrid, 0, 0, 0, 0, 1, COL_MASK, 0);
    m_hero[FACE_UP]    = bakeSprite(heroGrid, 1, 0, 0, 0, 1, COL_MASK, 0);
    m_hero[FACE_RIGHT] = bakeSprite(heroGrid, 2, 0, 0, 0, 1, COL_MASK, 0);
    m_hero[FACE_LEFT]  = bakeSprite(heroGrid, 2, 0, 1, 0, 1, COL_MASK, 0);

    m_heroFlash[FACE_DOWN]  = bakeSprite(heroGrid, 0, 0, 0, 0, 0, COL_NOTHING, COL_FLASH);
    m_heroFlash[FACE_UP]    = bakeSprite(heroGrid, 1, 0, 0, 0, 0, COL_NOTHING, COL_FLASH);
    m_heroFlash[FACE_RIGHT] = bakeSprite(heroGrid, 2, 0, 0, 0, 0, COL_NOTHING, COL_FLASH);
    m_heroFlash[FACE_LEFT]  = bakeSprite(heroGrid, 2, 0, 1, 0, 0, COL_NOTHING, COL_FLASH);

    /* ---- monsters ------------------------------------------------------ */
    for (int k = 0; k < MON_KIND_COUNT; ++k) {
        m_mon[k][FACE_DOWN]  = bakeSprite(monGrid, k, 0, 0, 0, 1, COL_MASK, 0);
        m_mon[k][FACE_UP]    = bakeSprite(monGrid, k, 1, 0, 0, 1, COL_MASK, 0);
        m_mon[k][FACE_RIGHT] = bakeSprite(monGrid, k, 2, 0, 0, 1, COL_MASK, 0);
        m_mon[k][FACE_LEFT]  = bakeSprite(monGrid, k, 2, 1, 0, 1, COL_MASK, 0);

        m_monFlash[k][FACE_DOWN]  = bakeSprite(monGrid, k, 0, 0, 0, 0, COL_NOTHING, COL_FLASH);
        m_monFlash[k][FACE_UP]    = bakeSprite(monGrid, k, 1, 0, 0, 0, COL_NOTHING, COL_FLASH);
        m_monFlash[k][FACE_RIGHT] = bakeSprite(monGrid, k, 2, 0, 0, 0, COL_NOTHING, COL_FLASH);
        m_monFlash[k][FACE_LEFT]  = bakeSprite(monGrid, k, 2, 1, 0, 0, COL_NOTHING, COL_FLASH);
    }

    /* ---- combat effects (all additive, so they key on black) ------------ */
    for (int f = 0; f < FACE_COUNT; ++f)
        m_slash[f] = bakeSprite(slashGrid, f, 0, 0, 0, 1, COL_NOTHING, 0);
    /* ring[0] is the small kill-ring, ring[1] spans the whirlwind reach
       (SPIN_RANGE * 2 = 124), so it is the biggest sprite in the game. */
    m_ring[0] = bakeSprite(ringGrid, 44, 0, 0, 0, 0, COL_NOTHING, 0);
    m_ring[1] = bakeSprite(ringGrid, 128, 0, 0, 0, 0, COL_NOTHING, 0);
    m_spark   = bakeSprite(sparkGrid, 0, 0, 0, 0, 0, COL_NOTHING, 0);
    m_coin    = bakeSprite(coinGrid,  0, 0, 0, 0, 1, COL_MASK, 0);
    m_potion  = bakeSprite(potionGrid, 0, 0, 0, 0, 1, COL_MASK, 0);

    /* three sizes because the actors are three different sizes and there is
       no scaling available -- see bakeShadow */
    m_shadow[0] = bakeShadow( 8, 3);
    m_shadow[1] = bakeShadow(11, 4);
    m_shadow[2] = bakeShadow(14, 5);
    m_vignette  = bakeVignette();

    /* ---- GDI font for digits and labels -------------------------------- */
    /* "微软雅黑" in GBK */
    static const char YAHEI[] = "\xCE\xA2\xC8\xED\xD1\xC5\xBA\xDA";

    HFONT hf = CreateFontA(15, 0, 0, 0, FW_BOLD, 0, 0, 0, GB2312_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, YAHEI);
    if (!hf) { m_ok = false; return false; }

    WFont fSmall, fBig;
    fSmall.Create(hf, 16, 1024);
    DeleteObject(hf);

    hf = CreateFontA(24, 0, 0, 0, FW_BOLD, 0, 0, 0, GB2312_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, YAHEI);
    if (!hf) { m_ok = false; return false; }
    fBig.Create(hf, 16, 1024);
    DeleteObject(hf);

    /* ---- digits -------------------------------------------------------- */
    struct { WFont *font; int set; unsigned long color; } dcfg[DIGIT_SET_COUNT] = {
        { &fSmall, DIGIT_SMALL, COL_DMG      },
        { &fBig,   DIGIT_BIG,   COL_DMG_CRIT },
        { &fSmall, DIGIT_HURT,  COL_DMG_HURT }
    };

    for (int i = 0; i < DIGIT_SET_COUNT; ++i) {
        WFont &f = *dcfg[i].font;
        const int set = dcfg[i].set;
        m_digitH[set] = f.GetHeight() + 2;

        for (int d = 0; d < 10; ++d) {
            char buf[2];
            buf[0] = (char)('0' + d);
            buf[1] = 0;
            bakeGlyph(m_digit[set][d], m_digitW[set][d], f, buf, dcfg[i].color, 2);
        }
    }

    /* ---- Chinese labels ------------------------------------------------ */
    static const unsigned long LABEL_COL[WF_LABEL_COUNT] = {
        COL_UI_TEXT,    /* 等级      */
        COL_EXP,        /* 经验      */
        COL_DMG_CRIT,   /* 金币      */
        COL_UI_DIM,     /* 击杀      */
        COL_HP,         /* 生命      */
        COL_MP,         /* 怒气      */
        COL_BANNER,     /* 第        */
        COL_BANNER,     /* 波        */
        COL_BANNER,     /* 烽火行    */
        COL_HP,         /* 阵亡      */
        COL_UI_TEXT,    /* 按 R 重来 */
        COL_UI_DIM,     /* 剩余      */
        /* ---- product shell (title / howto / perk cards / run summary) ---- */
        COL_UI_TEXT,    /* 开始游戏          */
        COL_UI_DIM,     /* 返回              */
        COL_UI_TEXT,    /* 操作说明          */
        COL_BANNER,     /* 选择强化          */
        COL_UI_DIM,     /* 时长              */
        COL_DMG_CRIT,   /* 最高纪录          */
        COL_BANNER,     /* 过关斩将！        */
        COL_UI_DIM,     /* 目标：坚守到第 10 波并击败首领 */
        COL_UI_TEXT,    /* WASD / 方向键 移动 */
        COL_UI_TEXT,    /* J / 空格 / 左键 攻击 */
        COL_UI_TEXT,    /* K / Shift / 右键 冲刺 */
        COL_UI_TEXT,    /* L / Ctrl 旋风斩（消耗怒气） */
        COL_UI_DIM,     /* M 地图 放大查看（暂停） */
        COL_UI_DIM,     /* R 重来  Esc 退出 */
        /* perk name (gold: the card reads as the reward it is) ... */
        COL_BANNER,     /* 猛攻   */
        COL_BANNER,     /* 铁骨   */
        COL_BANNER,     /* 风靴   */
        COL_BANNER,     /* 迅击   */
        COL_BANNER,     /* 致命   */
        COL_BANNER,     /* 嗜血   */
        COL_BANNER,     /* 飘逸   */
        COL_BANNER,     /* 旋风   */
        /* ... and the perk effect line (dim, so the name carries the card) */
        COL_UI_DIM,     /* 攻击 +5           */
        COL_UI_DIM,     /* 生命上限 +26      */
        COL_UI_DIM,     /* 移动速度提升      */
        COL_UI_DIM,     /* 攻击速度提升      */
        COL_UI_DIM,     /* 暴击率 +6%        */
        COL_UI_DIM,     /* 命中回复生命      */
        COL_UI_DIM,     /* 冲刺冷却缩短      */
        COL_UI_DIM,     /* 旋风斩强化        */
        COL_UI_TEXT,    /* 按 Enter 确认     */
        COL_UI_DIM      /* 上下键 / 1 2 切换 */
    };

    for (int i = 0; i < WF_LABEL_COUNT; ++i)
        bakeGlyph(m_label[i], m_labelW[i], fBig, WF_LABEL[i], LABEL_COL[i], 2);

    /* ---- title logo ----------------------------------------------------- */
    /* The menu headline deserves more than the 24 px HUD font, so the title is
       rasterised once at 72 px into its own bitmap.  Same trick as the labels:
       no anti-aliasing, a hard shadow one pass below, magenta padding for
       BLIT_MASK. */
    hf = CreateFontA(72, 0, 0, 0, FW_BOLD, 0, 0, 0, GB2312_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, YAHEI);
    if (!hf) { m_ok = false; return false; }
    {
        WFont fHuge;
        fHuge.Create(hf, 16, 1024);
        DeleteObject(hf);
        bakeGlyph(m_title, m_titleW, fHuge, WF_LABEL[WF_LBL_TITLE],
                  (unsigned long)COL_BANNER, 8);
    }

    m_ok = true;
    return true;
}

/* --------------------------------------------------------------- accessors */

WBitmap *Atlas::Tile(int id) const
{
    if (id < 0 || id >= TILE_COUNT) return m_tile[TILE_GRASS];
    return m_tile[id];
}

WBitmap *Atlas::Hero(int facing) const
{
    if (facing < 0 || facing >= FACE_COUNT) facing = FACE_DOWN;
    return m_hero[facing];
}

WBitmap *Atlas::Monster(int kind, int facing) const
{
    if (kind < 0 || kind >= MON_KIND_COUNT) kind = MON_GRUNT;
    if (facing < 0 || facing >= FACE_COUNT) facing = FACE_DOWN;
    return m_mon[kind][facing];
}

WBitmap *Atlas::HeroFlash(int facing) const
{
    if (facing < 0 || facing >= FACE_COUNT) facing = FACE_DOWN;
    return m_heroFlash[facing];
}

WBitmap *Atlas::MonsterFlash(int kind, int facing) const
{
    if (kind < 0 || kind >= MON_KIND_COUNT) kind = MON_GRUNT;
    if (facing < 0 || facing >= FACE_COUNT) facing = FACE_DOWN;
    return m_monFlash[kind][facing];
}

WBitmap *Atlas::Digit(int set, int d) const
{
    if (set < 0 || set >= DIGIT_SET_COUNT) set = DIGIT_SMALL;
    if (d < 0 || d > 9) d = 0;
    return m_digit[set][d];
}

WBitmap *Atlas::Label(int id) const
{
    if (id < 0 || id >= WF_LABEL_COUNT) id = 0;
    return m_label[id];
}

int Atlas::LabelW(int id) const
{
    if (id < 0 || id >= WF_LABEL_COUNT) id = 0;
    return m_labelW[id];
}

int Atlas::DigitW(int set, int d) const
{
    if (set < 0 || set >= DIGIT_SET_COUNT) set = DIGIT_SMALL;
    if (d < 0 || d > 9) d = 0;
    return m_digitW[set][d];
}

int Atlas::DigitH(int set) const
{
    if (set < 0 || set >= DIGIT_SET_COUNT) set = DIGIT_SMALL;
    return m_digitH[set];
}

WBitmap *Atlas::Slash(int facing) const
{
    if (facing < 0 || facing >= FACE_COUNT) facing = FACE_DOWN;
    return m_slash[facing];
}

WBitmap *Atlas::Ring(int i) const
{
    if (i < 0 || i > 1) i = 0;
    return m_ring[i];
}

WBitmap *Atlas::Spark() const
{
    return m_spark;
}

WBitmap *Atlas::Coin() const
{
    return m_coin;
}

WBitmap *Atlas::Potion() const
{
    return m_potion;
}

WBitmap *Atlas::Shadow(int tier) const
{
    if (tier < 0 || tier >= SHADOW_TIERS) tier = 0;
    return m_shadow[tier];
}

WBitmap *Atlas::Vignette() const
{
    return m_vignette;
}

int Atlas::ShadowTierOf(int monsterKind) const
{
    if (monsterKind == MON_BOSS)  return 2;
    if (monsterKind == MON_BRUTE) return 1;
    return 0;
}

} /* namespace wf */
