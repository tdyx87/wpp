/* ===========================================================================
 *  gf_art.cpp -- Guofeng : procedural atlas
 *  See gf_art.h.  Every pixel here is generated; there is no image file.
 * ===========================================================================
 */
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wsfont.h"
#include "wserror.h"

#include "gf_art.h"

namespace gf {

/* The largest sprite box any of the bakers needs.  The bamboo is 124 tall and
   the house 112 wide, so this has to clear both -- undersizing it overflows
   Grid silently and takes the process down, which is exactly the trap the
   warfire atlas documents too. */
enum { GF_SPR_MAX = 160 };

/* --------------------------------------------------------------- utilities */

static unsigned long clamp8(int v)
{
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (unsigned long)v;
}

/* Brighten (d > 0) or darken (d < 0) a colour.  The channel weights are not
   equal on purpose: pushing green as hard as red turns a lit stone green. */
static unsigned long shade(unsigned long c, int d)
{
    const int r = (int)((c >> 16) & 0xFF) + d * 7;
    const int g = (int)((c >>  8) & 0xFF) + d * 4;
    const int b = (int)( c        & 0xFF) + d * 7;
    return (clamp8(r) << 16) | (clamp8(g) << 8) | clamp8(b);
}

/* t = 0 -> a, t = 256 -> b. */
static unsigned long mixc(unsigned long a, unsigned long b, int t)
{
    const int ar = (int)((a >> 16) & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)(a & 0xFF);
    const int br = (int)((b >> 16) & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)(b & 0xFF);
    const int r  = ar + (br - ar) * t / 256;
    const int g  = ag + (bg - ag) * t / 256;
    const int bl = ab + (bb - ab) * t / 256;
    return (clamp8(r) << 16) | (clamp8(g) << 8) | clamp8(bl);
}

/* A 32-bit integer hash.  Used for every "random" decision in the art so the
   whole atlas is reproducible byte for byte -- rand() is not, and a test that
   asserts on generated content cannot afford that. */
static unsigned h32(int x, int y, unsigned s)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + s * 2246822519u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h;
}

/* A 4x4 ordered dither matrix.  This is the whole reason a tile transition
   works at all here: see the header.  Thresholds land in 8..248 so that a
   weight of 255 always fires and a weight of 0 never does. */
static const int BAYER4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

static bool Dither(int lx, int ly, int weight)
{
    if (weight <= 0)   return false;
    if (weight >= 255) return true;
    return weight > BAYER4[ly & 3][lx & 3] * 16 + 8;
}

/* ------------------------------------------------------------------- grid */
/*  A scratch pixel buffer for hand-authoring sprites with shapes instead of
 *  with per-pixel code.  Heap allocated at every call site: at 160x160 the
 *  colour plane alone is 100 KB, which has no business on the stack. */
struct Grid {
    int w, h;
    unsigned char mask[GF_SPR_MAX * GF_SPR_MAX];
    unsigned long col [GF_SPR_MAX * GF_SPR_MAX];

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
    /* put() only if something is already there -- used to paint highlights
       that must not grow the silhouette. */
    void over(int x, int y, unsigned long c)
    {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        if (!mask[y * w + x]) return;
        col[y * w + x] = c;
    }
    void rect(int x0, int y0, int x1, int y1, unsigned long c)
    {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) put(x, y, c);
    }
    void ellipse(int cx, int cy, int rx, int ry, unsigned long c)
    {
        if (rx < 1) rx = 1;
        if (ry < 1) ry = 1;
        for (int y = -ry; y <= ry; ++y)
            for (int x = -rx; x <= rx; ++x) {
                const float fx = (float)x / (float)rx;
                const float fy = (float)y / (float)ry;
                if (fx * fx + fy * fy <= 1.0f) put(cx + x, cy + y, c);
            }
    }
    void line(int x0, int y0, int x1, int y1, unsigned long c)
    {
        int dx = abs(x1 - x0), dy = abs(y1 - y0);
        const int sx = (x0 < x1) ? 1 : -1;
        const int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;
        for (;;) {
            put(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = err * 2;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 <  dx) { err += dx; y0 += sy; }
        }
    }
    /* A leaf / blade: a tapered stroke at an arbitrary angle.  Axis-aligned
       ellipse() cannot do this and rotation does not exist in this library. */
    void blade(int x, int y, int len, float ang, int thick, unsigned long c)
    {
        const float ca = cosf(ang), sa = sinf(ang);
        for (int i = 0; i < len; ++i) {
            const int px = x + (int)(ca * (float)i);
            const int py = y + (int)(sa * (float)i);
            const int half = (i < len * 2 / 3) ? thick : 0;
            for (int k = -half; k <= half; ++k)
                put(px, py + k, (k < 0) ? shade(c, 2) : c);
        }
    }
};

/* Bakes a grid into a bitmap.
   `empty`   is written where the shape is not: COL_MASK for anything blitted
             with BLIT_MASK, black for anything blitted with BLIT_ADDITIVE.
   `outline` adds a 1 px dark rim so the sprite reads against the ground. */
static void bake(const Grid &g, WBitmap &out, int flipX, int flipY,
                 int outline, unsigned long empty)
{
    out.Create(g.w, g.h);

    static const int nx[4] = { 1, -1, 0, 0 };
    static const int ny[4] = { 0, 0, 1, -1 };

    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            const int sx = flipX ? (g.w - 1 - x) : x;
            const int sy = flipY ? (g.h - 1 - y) : y;
            const int i  = sy * g.w + sx;

            unsigned long c = empty;
            if (g.mask[i]) {
                c = g.col[i];
            } else if (outline) {
                for (int k = 0; k < 4; ++k) {
                    const int ax = sx + nx[k], ay = sy + ny[k];
                    if (ax < 0 || ay < 0 || ax >= g.w || ay >= g.h) continue;
                    if (g.mask[ay * g.w + ax]) { c = COL_INK; break; }
                }
            }
            out[y][x] = WPixel((DWORD)c);
        }
    }
}

/* ------------------------------------------------------------- tile maths */

int TileVariant(int tx, int ty)
{
    return (int)(h32(tx, ty, 77u) % (unsigned)TILE_VARIANTS);
}

bool InDiamond(int lx, int ly)
{
    /* Sampled at the pixel CENTRE.  dx and dy are half-integers and the
       doubled term is an odd integer, so the sum is always a half-integer and
       can never tie against the threshold of 32 -- which is what makes
       neighbouring diamonds partition the plane with no gap and no overlap. */
    const float dx = (float)lx - (float)TILE_HW + 0.5f;
    const float dy = (float)ly - (float)TILE_HH + 0.5f;
    return fabsf(dx) + 2.0f * fabsf(dy) <= (float)TILE_HW;
}

int EdgeWeight(int lx, int ly, int dir)
{
    if (dir < 0 || dir > 3) return 0;

    static const float ax[4] = {  1.0f, -1.0f, -1.0f,  1.0f };
    static const float ay[4] = {  2.0f,  2.0f, -2.0f, -2.0f };

    const float dx = (float)lx - (float)TILE_HW + 0.5f;
    const float dy = (float)ly - (float)TILE_HH + 0.5f;

    /* s is maximal (31.5) along the edge that faces `dir` and minimal (-31.5)
       at the opposite vertex.  t is the normalised distance from the edge. */
    const float s = ax[dir] * dx + ay[dir] * dy;
    const float t = (31.5f - s) * (1.0f / 63.0f);

    const float band = (float)EDGE_BAND_PCT / 100.0f;
    if (t >= band) return 0;

    const int w = (int)((1.0f - t / band) * 255.0f + 0.5f);
    if (w < 0)   return 0;
    if (w > 255) return 255;
    return w;
}

unsigned long TileTexel(int tile, int lx, int ly, int variant)
{
    const unsigned vs = (unsigned)variant * 1013u;

    switch (tile) {
    case T_GRASS: {
        const unsigned patch = h32(lx >> 3, ly >> 3, 17u + vs);
        const unsigned fine  = h32(lx, ly, 101u + vs);
        unsigned long c = ((patch >> 5) & 1u) ? COL_GRASS_A : COL_GRASS_B;
        if ((patch >> 9) & 1u) c = COL_GRASS_C;
        if ((fine & 31u) == 0u)       c = shade(c, -2);      /* a blade    */
        else if ((fine & 63u) == 0u)  c = shade(c,  2);      /* a highlight */
        return c;
    }
    case T_DIRT: {
        const unsigned patch = h32(lx >> 2, ly >> 2, 23u + vs);
        const unsigned fine  = h32(lx, ly, 131u + vs);
        unsigned long c = ((patch >> 6) & 1u) ? COL_DIRT_A : COL_DIRT_B;
        if ((patch >> 11) & 1u) c = COL_DIRT_C;
        if ((fine & 63u) == 0u)       c = shade(c, 2);
        else if ((fine & 127u) == 0u) c = COL_PAVE_B;        /* a pebble   */
        return c;
    }
    case T_PAVE: {
        /* The joints run along the two diamond axes (s1 is parallel to the
           upper-left edge, s2 to the upper-right one), so the slabs read as
           stone cut to the grid rather than as a square lattice pasted on. */
        const int s1 = lx + 2 * ly;
        const int s2 = lx - 2 * ly;
        const unsigned fine = h32(lx, ly, 149u + vs);
        unsigned long c = COL_PAVE_A;

        const int m1 = ((s1 % 32) + 32) % 32;
        const int m2 = ((s2 % 32) + 32) % 32;
        if (m1 == 0 || m2 == 0) {
            c = COL_PAVE_C;
        } else {
            const unsigned slab = h32(((s1 % 64) + 64) / 32,
                                      ((s2 % 64) + 64) / 32, 7u + vs);
            if ((slab & 3u) == 0u) c = COL_PAVE_B;
        }
        if ((fine & 127u) == 0u) c = shade(c, -2);
        return c;
    }
    case T_STONE: {
        const unsigned patch = h32(lx >> 2, ly >> 2, 31u + vs);
        const unsigned fine  = h32(lx, ly, 173u + vs);
        unsigned long c = ((patch >> 4) & 1u) ? COL_STONE_A : COL_STONE_B;
        if ((patch >> 10) & 1u) c = COL_STONE_C;
        if ((fine & 47u) == 0u)      c = shade(c, -3);
        else if ((fine & 95u) == 0u) c = shade(c,  2);
        return c;
    }
    case T_WATER: {
        const unsigned fine = h32(lx >> 1, ly, 199u + vs);
        unsigned long c = ((fine >> 5) & 1u) ? COL_WATER_A : COL_WATER_B;
        if ((fine & 7u) == 0u) c = COL_WATER_C;
        /* crests: horizontal on screen, so a function of ly with a slow drift
           across the tile -- anything diagonal would read as a current */
        if (((ly + (lx >> 3)) % 6) == 0 && ((fine >> 3) & 1u)) c = COL_WAVE;
        return c;
    }
    default:
        return COL_INK;
    }
}

/* --------------------------------------------------------- tileset bitmaps */

static WBitmap *bakeTileBmp(int t, int v)
{
    WBitmap *b = new WBitmap();
    b->Create(TILE_W, TILE_H);
    for (int ly = 0; ly < TILE_H; ++ly)
        for (int lx = 0; lx < TILE_W; ++lx) {
            const unsigned long c = InDiamond(lx, ly)
                                  ? TileTexel(t, lx, ly, v) : COL_MASK;
            (*b)[ly][lx] = WPixel((DWORD)c);
        }
    return b;
}

static WBitmap *bakeEdgeBmp(int t, int other, int v, int dir)
{
    WBitmap *b = new WBitmap();
    b->Create(TILE_W, TILE_H);
    for (int ly = 0; ly < TILE_H; ++ly)
        for (int lx = 0; lx < TILE_W; ++lx) {
            unsigned long c = COL_MASK;
            if (InDiamond(lx, ly)) {
                const int w = EdgeWeight(lx, ly, dir);
                if (w > 0 && Dither(lx, ly, w))
                    c = TileTexel(other, lx, ly, v);
                else if (w > 0)
                    c = TileTexel(t, lx, ly, v);
            }
            (*b)[ly][lx] = WPixel((DWORD)c);
        }
    return b;
}

/* --------------------------------------------------------------- scenery */

static WBitmap *bakeBamboo(int variant)
{
    const int W = 48, H = 124;

    Grid *g = new Grid();
    g->init(W, H);

    const int stalks = 4;

    for (int i = 0; i < stalks; ++i) {
        const int baseX = 10 + i * 8 + (int)(h32(i, variant, 5u) % 3u);
        const int top   = 14 + (int)(h32(i, variant, 9u) % 22u);
        const int lean  = (int)(h32(i, variant, 13u) % 17u) - 8;
        const unsigned long c = (i & 1) ? COL_BAMBOO_B : COL_BAMBOO_A;

        for (int y = H - 4; y >= top; --y) {
            const float f = (float)((H - 4) - y) / (float)((H - 4) - top);
            const int px = baseX + (int)(f * f * (float)lean);
            g->put(px,     y, shade(c,  2));
            g->put(px + 1, y, c);
            g->put(px + 2, y, shade(c, -3));
            if ((((H - 4) - y) % 13) == 0) {            /* a node */
                g->put(px,     y, shade(c, -4));
                g->put(px + 1, y, shade(c, -4));
                g->put(px + 2, y, shade(c, -4));
            }
        }

        /* The foliage.  A bamboo culm on its own is a green pole; what makes
           the plant read is that it carries a full crown at the tip AND a
           smaller branch of leaves lower down, both fanning the same way so
           the clump looks like one stand bending in the wind rather than a
           starburst.  Eleven blades at the tip and five on the branch is the
           least that stops the grove looking like a picket fence. */
        const int cx = baseX + lean;

        for (int k = 0; k < 11; ++k) {
            const float ang = -2.60f + 0.26f * (float)k + (float)variant * 0.04f;
            const int len = 16 + (int)(h32(i, k, 21u) % 12u);
            g->blade(cx, top + 4, len, ang, 1,
                     (k & 1) ? COL_BAMBOO_A : COL_BAMBOO_C);
        }

        /* the lower branch, shorter and offset so the two do not merge */
        {
            const float f = 0.28f + 0.10f * (float)(h32(i, variant, 27u) % 3u);
            const int by = top + 4 + (int)(f * (float)((H - 4) - top));
            const int bx = baseX + (int)((1.0f - f) * (1.0f - f) * (float)lean);
            for (int k = 0; k < 5; ++k) {
                const float ang = -2.45f + 0.30f * (float)k + (float)variant * 0.04f;
                const int len = 11 + (int)(h32(i, k, 41u) % 7u);
                g->blade(bx, by, len, ang, 1,
                         (k & 1) ? COL_BAMBOO_B : COL_BAMBOO_A);
            }
        }
    }

    WBitmap *b = new WBitmap();
    bake(*g, *b, 0, 0, 1, COL_MASK);
    delete g;
    return b;
}

static WBitmap *bakePine(int variant)
{
    const int W = 64, H = 112;

    Grid *g = new Grid();
    g->init(W, H);

    const int trunkX = 30 + (int)(h32(variant, 3, 33u) % 5u);

    /* trunk */
    for (int y = H - 6; y > 40; --y) {
        const int w = 3 + (y > 80 ? 1 : 0);
        for (int k = 0; k < w; ++k)
            g->put(trunkX + k, y, (k == 0) ? COL_TRUNK_LIT : COL_TRUNK);
    }

    /* four tiers, widest at the bottom.  Each row's half width follows a
       triangle so the tier reads as a cone rather than as a disc. */
    static const int tierY[4]  = { 40, 58, 74, 88 };
    static const int tierR[4]  = { 30, 25, 19, 12 };
    static const int tierH[4]  = { 22, 20, 17, 13 };

    for (int t = 0; t < 4; ++t) {
        const int cy = tierY[t];
        const int rx = tierR[t];
        const int hh = tierH[t];
        const unsigned long base = (t & 1) ? COL_PINE_A : COL_PINE_B;

        for (int k = 0; k < hh; ++k) {
            const float f = (float)k / (float)hh;
            const int half = (int)((float)rx * f);
            const int y = cy - k;
            for (int x = -half; x <= half; ++x) {
                unsigned long c = base;
                if (x < -half / 2)      c = shade(base,  2);   /* lit side  */
                else if (x > half / 2)  c = COL_PINE_C;        /* shade     */
                if (((x + y) & 3) == 0) c = shade(c, -1);
                g->put(trunkX + x, y, c);
            }
        }
    }

    WBitmap *b = new WBitmap();
    bake(*g, *b, 0, 0, 1, COL_MASK);
    delete g;
    return b;
}

static WBitmap *bakeRock(int variant)
{
    const int W = 48, H = 36;

    Grid *g = new Grid();
    g->init(W, H);

    const unsigned s = (unsigned)variant * 57u;

    /* three overlapping boulders, back to front */
    for (int i = 2; i >= 0; --i) {
        const int cx = 14 + i * 9 + (int)(h32(i, variant, 41u) % 4u);
        const int cy = 20 + i * 4;
        const int rx = 9 + (int)(h32(i, variant, 43u) % 5u);
        const int ry = 6 + (int)(h32(i, variant, 47u) % 3u);
        const unsigned long base = (i == 1) ? COL_ROCK_A : COL_ROCK_B;

        g->ellipse(cx, cy, rx, ry, base);
        g->ellipse(cx - rx / 3, cy - ry / 3, rx / 2, ry / 2, COL_ROCK_A);
        g->ellipse(cx + rx / 2, cy + ry / 3, rx / 3, ry / 3, COL_ROCK_C);
    }
    /* a couple of cracks so a flat boulder does not read as a ball */
    for (int k = 0; k < 3; ++k) {
        const int x = 12 + (int)(h32(k, variant, 51u) % 26u);
        g->line(x, 16, x + 3 - (int)(s % 5u), 26, COL_ROCK_C);
    }

    WBitmap *b = new WBitmap();
    bake(*g, *b, 0, 0, 1, COL_MASK);
    delete g;
    return b;
}

static WBitmap *bakeLantern(int variant)
{
    const int W = 32, H = 68;

    Grid *g = new Grid();
    g->init(W, H);

    const int px = 15;
    /* a small per-variant offset so a row of lanterns does not look stamped */
    const int drop = (int)(h32(variant, 7, 61u) % 3u);

    /* the post */
    for (int y = H - 4; y >= 20; --y) {
        g->put(px - 1, y, COL_TRUNK_LIT);
        g->put(px,     y, COL_TRUNK);
        g->put(px + 1, y, shade(COL_TRUNK, -3));
    }
    /* the cross arm */
    g->rect(px - 1, 22, px + 7, 23, COL_TRUNK);
    /* a tiny hanging ring */
    g->put(px + 6, 24, COL_GOLD_DIM);
    g->put(px + 6, 25, COL_GOLD_DIM);

    /* the lantern body: a squat ellipse with gold caps top and bottom */
    const int lx = px + 6, ly = 33 + drop;
    g->ellipse(lx, ly, 6, 7, COL_LANTERN);
    g->ellipse(lx - 2, ly - 2, 3, 3, COL_LANTERN_L);      /* lit side */
    g->rect(lx - 4, ly - 8, lx + 4, ly - 7, COL_GOLD);
    g->rect(lx - 3, ly + 7, lx + 3, ly + 8, COL_GOLD);
    /* the tassel */
    g->line(lx, ly + 9, lx, ly + 13, COL_CINNABAR);

    WBitmap *b = new WBitmap();
    bake(*g, *b, 0, 0, 1, COL_MASK);
    delete g;
    return b;
}

static WBitmap *bakeHouse(int variant)
{
    const int W = 112, H = 100;

    Grid *g = new Grid();
    g->init(W, H);

    const int cx = 56;

    /* ---- walls ---------------------------------------------------------
       Two visible faces of a box: the left face catches the light, the right
       one falls away.  The bottom edge follows the tile diamond's two lower
       edges so the building sits ON the ground instead of floating over it. */
    for (int y = 56; y <= 84; ++y) {
        const float f = (float)(y - 56) / 28.0f;
        const int half = 20 + (int)(f * 12.0f);
        for (int x = -half; x <= half; ++x) {
            unsigned long c = (x < 0) ? COL_WALL_A : COL_WALL_B;
            if (x > half - 3) c = shade(COL_WALL_B, -3);
            if (x < -half + 3) c = shade(COL_WALL_A, 2);
            g->put(cx + x, y, c);
        }
    }
    /* the plinth */
    for (int x = -32; x <= 32; ++x) {
        const int half = 32 - abs(x) / 2;
        if (half <= 0) continue;
        g->put(cx + x, 84, COL_STONE_B);
        g->put(cx + x, 85, COL_STONE_C);
    }

    /* ---- door and windows ---------------------------------------------- */
    g->rect(cx - 7, 68, cx + 7, 84, COL_INK);
    g->rect(cx - 7, 68, cx + 7, 69, COL_TRUNK);
    g->rect(cx - 6, 70, cx - 1, 83, shade(COL_TRUNK, 2));
    g->rect(cx + 1, 70, cx + 6, 83, COL_TRUNK);
    g->put(cx - 5, 76, COL_GOLD);            /* the door stud */
    g->put(cx + 5, 76, COL_GOLD);

    g->rect(cx - 22, 66, cx - 12, 74, COL_INK);
    g->rect(cx + 12, 66, cx + 22, 74, COL_INK);
    for (int i = -22; i <= -12; ++i) g->over(cx + i, 70, COL_GOLD_DIM);
    for (int i =  12; i <=  22; ++i) g->over(cx + i, 70, COL_GOLD_DIM);

    /* ---- roof -----------------------------------------------------------
       A flattened hexagonal prism: the ridge on top, two sloping planes and
       an upturned eave on each side.  Drawn as scanlines of a hexagon so the
       silhouette stays convex -- a rotated rectangle would need rotation,
       which this library does not have. */
    const int ridgeL = 34, ridgeR = 78;
    const int ridgeY = 18 + (int)(h32(variant, 11, 67u) % 3u);
    const int eaveY = 30, eaveL = 8, eaveR = 104, skirtY = 58;

    for (int y = ridgeY; y <= skirtY; ++y) {
        int xl, xr;
        if (y <= eaveY) {
            const float f = (float)(y - ridgeY) / (float)(eaveY - ridgeY);
            xl = ridgeL - (int)((float)(ridgeL - eaveL) * f);
            xr = ridgeR + (int)((float)(eaveR - ridgeR) * f);
        } else {
            const float f = (float)(y - eaveY) / (float)(skirtY - eaveY);
            xl = eaveL + (int)((float)(cx - 18 - eaveL) * f);
            xr = eaveR - (int)((float)(eaveR - (cx + 18)) * f);
        }
        for (int x = xl; x <= xr; ++x) {
            unsigned long c = COL_ROOF_A;
            if (y < ridgeY + 5)               c = COL_ROOF_L;   /* lit ridge */
            else if (y > eaveY + 10)          c = COL_ROOF_B;
            if (x > cx + 6)                   c = shade(c, -2); /* away side */
            /* tile ridges running down the slope */
            if (((x - ridgeL) % 7) == 0)      c = shade(c, -3);
            g->put(x, y, c);
        }
    }
    /* the ridge cap and the gold eave trim */
    g->rect(ridgeL, ridgeY - 2, ridgeR, ridgeY, COL_ROOF_L);
    g->line(eaveL, eaveY, eaveL + 6, eaveY - 3, COL_EAVE);
    g->line(eaveR, eaveY, eaveR - 6, eaveY - 3, COL_EAVE);
    for (int y = eaveY; y <= skirtY; ++y) {
        const float f = (float)(y - eaveY) / (float)(skirtY - eaveY);
        const int l = eaveL + (int)((float)(cx - 18 - eaveL) * f);
        const int r = eaveR - (int)((float)(eaveR - (cx + 18)) * f);
        g->put(l, y, COL_EAVE);
        g->put(r, y, COL_EAVE);
        if (y >= skirtY - 2) { g->put(l + 1, y, COL_GOLD_DIM); g->put(r - 1, y, COL_GOLD_DIM); }
    }

    WBitmap *b = new WBitmap();
    bake(*g, *b, 0, 0, 1, COL_MASK);
    delete g;
    return b;
}

static WBitmap *bakeWell(int variant)
{
    const int W = 56, H = 52;

    Grid *g = new Grid();
    g->init(W, H);

    const int cx = 28;

    /* the stone ring, drawn as an ellipse with a dark mouth inside */
    g->ellipse(cx, 40, 18, 9, COL_STONE_B);
    g->ellipse(cx, 39, 18, 8, COL_STONE_A);
    g->ellipse(cx, 39, 12, 5, COL_INK);
    g->ellipse(cx, 39, 12, 5, COL_INK);
    /* a highlight on the near rim */
    for (int x = -14; x <= 14; ++x)
        g->over(cx + x, 45 - abs(x) / 6, COL_ROCK_A);

    /* the frame: two posts and a cross beam */
    g->rect(cx - 15, 20, cx - 13, 40, COL_TRUNK);
    g->rect(cx + 13, 20, cx + 15, 40, COL_TRUNK);
    g->rect(cx - 16, 18, cx + 16, 21, COL_TRUNK_LIT);

    /* a little roof over the beam, matching the house */
    const int top = 8 + (int)(h32(variant, 13, 71u) % 3u);
    for (int y = top; y <= 18; ++y) {
        const float f = (float)(y - top) / 10.0f;
        const int half = 10 + (int)(f * 8.0f);
        for (int x = -half; x <= half; ++x) {
            unsigned long c = (x < 0) ? COL_ROOF_L : COL_ROOF_A;
            if (y > 14) c = COL_ROOF_B;
            g->put(cx + x, y, c);
        }
    }
    g->line(cx - 18, 18, cx - 13, 15, COL_EAVE);
    g->line(cx + 18, 18, cx + 13, 15, COL_EAVE);

    WBitmap *b = new WBitmap();
    bake(*g, *b, 0, 0, 1, COL_MASK);
    delete g;
    return b;
}

/* ------------------------------------------------------------------ actor */
/*  One robed figure.  Four facings, four walk frames.
 *
 *  FACE_D is the front view, FACE_U the back, and FACE_L / FACE_R share one
 *  side view mirrored -- which is the standard cheat and works because at 34
 *  pixels wide nobody can tell the near arm from the far one. */
static WBitmap *bakeHero(int face, int frame)
{
    const int W = HERO_W, H = HERO_H;

    Grid *g = new Grid();
    g->init(W, H);

    static const unsigned long ROBE      = 0xCFC7B2;
    static const unsigned long ROBE_SH   = 0x9E967F;
    static const unsigned long ROBE_HI   = 0xE8E2D2;
    static const unsigned long SKIN      = 0xE8C4A2;
    static const unsigned long SKIN_SH   = 0xC29C7C;
    static const unsigned long HAIR      = 0x241C24;
    static const unsigned long HAIR_HI   = 0x403040;

    const int cx = W / 2;
    const int side = (face == FACE_L || face == FACE_R);

    /* the bob: the body lifts a pixel on the two contact frames */
    const int bob = (frame == 1 || frame == 3) ? 1 : 0;

    /* ---- legs (behind the robe, only the feet show) --------------------- */
    {
        static const int swing[HERO_FRAMES] = { 0, 3, 0, -3 };
        const int s = swing[frame];
        const int footY = H - 3;
        if (side) {
            g->rect(cx - 3 + s, footY - 2, cx - 1 + s, footY, COL_INK);
            g->rect(cx + 1 - s, footY - 2, cx + 3 - s, footY, COL_INK);
        } else {
            g->rect(cx - 6, footY - 2 + (s > 0 ? -1 : 0), cx - 2, footY, COL_INK);
            g->rect(cx + 2, footY - 2 + (s < 0 ? -1 : 0), cx + 6, footY, COL_INK);
        }
    }

    /* ---- robe ----------------------------------------------------------- */
    for (int y = 20 - bob; y <= H - 4; ++y) {
        const float f = (float)(y - (20 - bob)) / (float)(H - 4 - (20 - bob));
        int half = side ? (5 + (int)(f * 5.0f)) : (7 + (int)(f * 5.0f));
        for (int x = -half; x <= half; ++x) {
            unsigned long c = ROBE;
            if (x < -half / 2)     c = ROBE_HI;      /* the lit side       */
            else if (x > half / 2) c = ROBE_SH;      /* the shaded side    */
            if (((x + y) & 7) == 0) c = shade(c, -1);
            g->put(cx + x, y, c);
        }
    }
    /* the sash */
    g->rect(cx - (side ? 9 : 11), 34 - bob, cx + (side ? 9 : 11), 37 - bob, COL_JADE);
    g->rect(cx - (side ? 9 : 11), 34 - bob, cx + (side ? 9 : 11), 34 - bob, 0x5ED0B0);
    /* the hem, a shade darker so the feet read */
    g->rect(cx - (side ? 9 : 11), H - 5, cx + (side ? 9 : 11), H - 4, ROBE_SH);

    /* ---- arms ----------------------------------------------------------- */
    {
        static const int swing[HERO_FRAMES] = { 0, -2, 0, 2 };
        const int s = swing[frame];
        g->rect(cx - (side ? 7 : 11), 24 - bob + s, cx - (side ? 5 : 9), 33 - bob + s, ROBE_SH);
        g->rect(cx + (side ? 5 : 9), 24 - bob - s, cx + (side ? 7 : 11), 33 - bob - s, ROBE_SH);
        g->put(cx - (side ? 6 : 10), 33 - bob + s, SKIN_SH);
        g->put(cx + (side ? 6 : 10), 33 - bob - s, SKIN_SH);
    }

    /* ---- head ----------------------------------------------------------- */
    const int hy = 14 - bob;
    g->ellipse(cx, hy, 7, 8, SKIN);
    g->ellipse(cx - 2, hy - 1, 4, 4, 0xF2D6B8);

    if (face == FACE_U) {
        /* the back of the head: all hair */
        g->ellipse(cx, hy, 7, 8, HAIR);
        g->ellipse(cx - 2, hy - 2, 4, 4, HAIR_HI);
    } else {
        /* hair as a cap over the top and the sides.  The half width follows
           the head's own ellipse, clamped at zero: VS2008's math.h predates
           C99 and has no fmaxf. */
        for (int y = -9; y <= 1; ++y) {
            float q = 49.0f - (float)(y * y) * 0.75f;
            if (q < 0.0f) q = 0.0f;
            const int half = (int)(sqrtf(q) + 0.5f);
            for (int x = -half; x <= half; ++x)
                g->over(cx + x, hy + y, (x < -2) ? HAIR_HI : HAIR);
        }
        g->rect(cx - 7, hy - 1, cx - 5, hy + 4, HAIR);
        g->rect(cx + 5, hy - 1, cx + 7, hy + 4, HAIR);

        if (face == FACE_D) {
            g->put(cx - 3, hy + 1, COL_INK);
            g->put(cx + 3, hy + 1, COL_INK);
            g->put(cx - 2, hy + 1, COL_INK);
            g->put(cx + 2, hy + 1, COL_INK);
            g->put(cx, hy + 3, SKIN_SH);
        } else {
            g->put(cx + (face == FACE_R ? 3 : -3), hy + 1, COL_INK);
            g->put(cx + (face == FACE_R ? 4 : -4), hy + 1, COL_INK);
        }
    }
    /* the topknot, on every facing */
    g->ellipse(cx, hy - 9, 3, 3, HAIR);
    g->put(cx, hy - 12, COL_GOLD);

    WBitmap *b = new WBitmap();
    /* FACE_L is the same side view as FACE_R, mirrored.  One bake, two
       facings -- and the walk frames stay in sync for free. */
    bake(*g, *b, (face == FACE_L) ? 1 : 0, 0, 1, COL_MASK);
    delete g;
    return b;
}

/* ---------------------------------------------------------------- effects */

/*  Soft blobs.  BLIT_SUBTRACT subtracts the source channel by channel, so a
 *  gradient has to live in the pixel values: black outside the shape costs
 *  nothing there, which is what makes this the one usable falloff in the
 *  library.  An isometric shadow is an ellipse, not a circle -- a circle in
 *  the world projects to a 2:1 ellipse on screen, and drawing a round shadow
 *  under an isometric actor is the single most common tell. */
static WBitmap *bakeShadow(int rx, int ry)
{
    WBitmap *b = new WBitmap();
    const int w = 2 * rx + 4, h = 2 * ry + 4;
    b->Create(w, h);

    const int cx = w / 2, cy = h / 2;
    const float frx = (float)rx, fry = (float)ry;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float dx = ((float)x - (float)cx) / frx;
            const float dy = ((float)y - (float)cy) / fry;
            const float d  = dx * dx + dy * dy;

            unsigned long c = 0;
            if (d < 1.0f) {
                /* Subtracting equally from all three channels would leave a
                   neutral grey shadow; pulling blue back a little is what
                   makes it read as shade on a lit ground rather than as a
                   hole.  Subtracting MORE green than red would tint it
                   magenta, which is the easy mistake here. */
                const int k = (int)((1.0f - d) * 44.0f + 0.5f);
                c = ((unsigned long)clamp8(k) << 16)
                  | ((unsigned long)clamp8(k) << 8)
                  | (unsigned long)clamp8(k * 3 / 4);
            }
            (*b)[y][x] = WPixel((DWORD)c);
        }
    }
    return b;
}

/*  An additive radial.  Black is the additive no-op, so the rim fades into
 *  nothing without needing an alpha channel at all. */
static WBitmap *bakeGlow(int r, unsigned long colour, int strength)
{
    WBitmap *b = new WBitmap();
    const int w = 2 * r + 2, h = 2 * r + 2;
    b->Create(w, h);

    const int cx = w / 2, cy = h / 2;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float dx = (float)(x - cx) / (float)r;
            const float dy = (float)(y - cy) / (float)r;
            const float d  = dx * dx + dy * dy;

            unsigned long c = 0;
            if (d < 1.0f) {
                /* squared falloff: a linear one leaves a visible disc edge */
                const float f = (1.0f - d) * (1.0f - d);
                const int t = (int)(f * (float)strength + 0.5f);
                c = mixc(0, colour, t);
            }
            (*b)[y][x] = WPixel((DWORD)c);
        }
    }
    return b;
}

static WBitmap *bakeSpark(int frame)
{
    WBitmap *b = new WBitmap();
    const int n = 9 + frame * 2;
    b->Create(n, n);
    b->Clear(WPixel((DWORD)COL_NOTHING));

    const int c = n / 2;
    const int arm = 2 + frame / 2;
    for (int i = -arm; i <= arm; ++i) {
        const int a = (i == 0) ? 255 : 150 - abs(i) * 30;
        (*b)[c][c + i] = WPixel((DWORD)mixc(0, COL_WAVE, a));
        (*b)[c + i][c] = WPixel((DWORD)mixc(0, COL_WAVE, a));
    }
    return b;
}

static WBitmap *bakeVignette(int w, int h)
{
    WBitmap *b = new WBitmap();
    b->Create(w, h);

    const float cx = (float)w * 0.5f, cy = (float)h * 0.5f;
    const float rx = cx * 1.42f, ry = cy * 1.42f;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float dx = ((float)x - cx) / rx;
            const float dy = ((float)y - cy) / ry;
            float d = sqrtf(dx * dx + dy * dy) - 0.52f;
            if (d < 0.0f) d = 0.0f;
            if (d > 1.0f) d = 1.0f;
            const float f = d * d;
            const int k = (int)(f * 26.0f + 0.5f);
            const int g = (int)(f * 30.0f + 0.5f);
            const int bl = (int)(f * 26.0f + 0.5f);
            (*b)[y][x] = WPixel((DWORD)(((unsigned long)clamp8(k) << 16)
                                      | ((unsigned long)clamp8(g) << 8)
                                      | (unsigned long)clamp8(bl)));
        }
    }
    return b;
}

/* -------------------------------------------------------------------- ui */

/*  A 云纹 scroll: an Archimedean spiral.  Baked once and mirrored into the
 *  four corners, because the library cannot rotate and four separate bakes
 *  would only differ by a flip anyway. */
static WBitmap *bakeScroll(int size, unsigned long colour)
{
    Grid *g = new Grid();
    g->init(size, size);

    const int c = size / 2;
    const float maxr = (float)size * 0.42f;

    for (float a = 0.0f; a < 4.6f * 3.14159265f; a += 0.04f) {
        const float r = 0.8f + a * 0.72f;
        if (r > maxr) break;
        const int x = c + (int)(cosf(a) * r + 0.5f);
        const int y = c + (int)(sinf(a) * r + 0.5f);
        g->put(x, y, colour);
        g->put(x, y + 1, shade(colour, -4));
    }
    /* a 回纹 tail leaving the scroll, so it reads as a border motif and not
       as a random doodle */
    g->line(c, size - 2, size - 2, size - 2, colour);
    g->line(size - 2, size - 2, size - 2, c, colour);
    g->line(2, c, 2, size - 2, shade(colour, -3));
    g->line(2, size - 2, c, size - 2, shade(colour, -3));

    WBitmap *b = new WBitmap();
    bake(*g, *b, 0, 0, 0, COL_MASK);
    delete g;
    return b;
}

static WBitmap *bakeCloud(int variant)
{
    const int W = 40 + variant * 14, H = 14;

    Grid *g = new Grid();
    g->init(W, H);

    /* an S curve with a curl at each end -- the standard 云纹 flourish */
    const int n = 5 + variant * 2;
    for (int i = 0; i < n; ++i) {
        const int x = 8 + i * (W - 16) / (n - 1);
        const int y = H / 2 + ((i & 1) ? -2 : 2);
        g->put(x, y, COL_GOLD);
        g->put(x, y + 1, COL_GOLD_DIM);
    }
    g->line(0, H / 2 + 3, 7, H / 2 - 1, COL_GOLD_DIM);
    g->line(W - 1, H / 2 + 3, W - 8, H / 2 - 1, COL_GOLD_DIM);
    g->put(1, H / 2 + 4, COL_GOLD_DIM);
    g->put(W - 2, H / 2 + 4, COL_GOLD_DIM);

    WBitmap *b = new WBitmap();
    bake(*g, *b, 0, 0, 0, COL_MASK);
    delete g;
    return b;
}

/* --------------------------------------------------------------- glyphs */

static void bakeGlyph(WBitmap *&out, int &outW, WFont &font,
                      const char *s, unsigned long color)
{
    int w = font.GetLength(s);
    if (w < 1) w = 1;

    /* one pixel each way so the drop shadow has somewhere to land */
    out = new WBitmap();
    out->Create(w + 2, font.GetHeight() + 2);
    out->Clear(WPixel((DWORD)COL_MASK));

    /* The padding has to be the BLIT_MASK key, not black: _font16_alpha
       blends a half-covered edge pixel with whatever is behind it, and behind
       a label that is magenta, which shows up as a pink fringe on every
       glyph.  _font16_noalpha thresholds coverage instead and writes either
       the pure colour or nothing -- cleaner, and period-correct. */
    out->SetPos(1, 1);
    font.Draw(*out, s, WPixel((DWORD)COL_INK), FONT_NOALPHA);

    out->SetPos(0, 0);
    font.Draw(*out, s, WPixel((DWORD)color), FONT_NOALPHA);

    out->SetPos(0, 0);
    outW = w + 2;
}

/* ------------------------------------------------------------------ atlas */

Atlas::Atlas() : m_ok(false)
{
    memset(m_tile,   0, sizeof(m_tile));
    memset(m_edge,   0, sizeof(m_edge));
    memset(m_deco,   0, sizeof(m_deco));
    memset(m_hero,   0, sizeof(m_hero));
    memset(m_shadow, 0, sizeof(m_shadow));
    memset(m_glow,   0, sizeof(m_glow));
    memset(m_spark,  0, sizeof(m_spark));
    memset(m_label,  0, sizeof(m_label));
    memset(m_ascii,  0, sizeof(m_ascii));
    memset(m_corner, 0, sizeof(m_corner));
    memset(m_cloud,  0, sizeof(m_cloud));

    m_ground = 0; m_groundX = m_groundY = 0;
    m_mini = 0; m_miniW = m_miniH = 0;
    m_big = 0;  m_bigW = m_bigH = 0;
    m_vignette = 0;
    m_seal = 0; m_plaque = 0; m_plaqueW = m_plaqueH = 0;
}

Atlas::~Atlas()
{
    Destroy();
}

void Atlas::Destroy()
{
    for (int t = 0; t < TILE_COUNT; ++t) {
        for (int v = 0; v < TILE_VARIANTS; ++v) {
            delete m_tile[t][v]; m_tile[t][v] = 0;
            for (int d = 0; d < 4; ++d) {
                delete m_edge[t][v][d]; m_edge[t][v][d] = 0;
            }
        }
    }
    delete m_ground;  m_ground = 0;
    delete m_mini;    m_mini = 0;
    delete m_big;     m_big = 0;
    for (int k = 0; k < DECO_COUNT; ++k) { delete m_deco[k]; m_deco[k] = 0; }
    for (int f = 0; f < FACE_COUNT; ++f)
        for (int i = 0; i < HERO_FRAMES; ++i) { delete m_hero[f][i]; m_hero[f][i] = 0; }
    for (int i = 0; i < SHADOW_TIERS; ++i) { delete m_shadow[i]; m_shadow[i] = 0; }
    for (int i = 0; i < GLOW_FRAMES; ++i) {
        delete m_glow[i];  m_glow[i] = 0;
        delete m_spark[i]; m_spark[i] = 0;
    }
    delete m_vignette; m_vignette = 0;
    for (int i = 0; i < GF_LABEL_COUNT; ++i) { delete m_label[i]; m_label[i] = 0; }
    for (int i = 0; i < GF_ASCII_COUNT; ++i) { delete m_ascii[i]; m_ascii[i] = 0; }
    for (int i = 0; i < CORNER_COUNT; ++i) { delete m_corner[i]; m_corner[i] = 0; }
    for (int i = 0; i < CLOUD_VARIANTS; ++i) { delete m_cloud[i]; m_cloud[i] = 0; }
    delete m_seal;   m_seal = 0;
    delete m_plaque; m_plaque = 0;
}

/* -------------------------------------------------------------- the sheet */

bool Atlas::bakeGround(const Scene &scene)
{
    const ScreenBox box = WorldScreenBox();
    m_groundX = box.x0;
    m_groundY = box.y0;

    const int W = box.x1 - box.x0;
    const int H = box.y1 - box.y0;

    m_ground = new WBitmap();
    if (m_ground->Create(W, H) != WS_OK) return false;

    /* Outside the map diamond there is nothing to draw, and the camera can
       reach the four corners of the bounding box.  Rather than leave that as
       flat black it gets a vertical dusk gradient, so the island reads as
       floating over something instead of being cut off. */
    for (int y = 0; y < H; ++y) {
        const int t = y * 256 / H;
        const unsigned long c = mixc(0x141C26, 0x2A3444, t);
        WPixel *row = (*m_ground)[y];
        for (int x = 0; x < W; ++x) row[x] = WPixel((DWORD)c);
    }

    for (int ty = 0; ty < MAP_H; ++ty) {
        for (int tx = 0; tx < MAP_W; ++tx) {
            int sx, sy;
            IsoToScreen(tx, ty, sx, sy);
            const int ox = sx - TILE_HW - m_groundX;
            const int oy = sy - TILE_HH - m_groundY;

            const int t = (int)scene.TileAt(tx, ty);
            const int v = TileVariant(tx, ty);

            /* which neighbours differ, in tile-space direction order */
            int other[4];
            static const int dx4[4] = {  1, 0, -1, 0 };
            static const int dy4[4] = {  0, 1,  0, -1 };
            for (int d = 0; d < 4; ++d) {
                const int nx = tx + dx4[d], ny = ty + dy4[d];
                /* Off the map counts as "no neighbour": feathering towards a
                   material that is not there would draw a soft ring all the
                   way round the island. */
                other[d] = scene.InMap(nx, ny) ? (int)scene.TileAt(nx, ny) : -1;
            }

            for (int ly = 0; ly < TILE_H; ++ly) {
                WPixel *row = (*m_ground)[oy + ly];
                for (int lx = 0; lx < TILE_W; ++lx) {
                    if (!InDiamond(lx, ly)) continue;

                    unsigned long c = TileTexel(t, lx, ly, v);

                    /* Only the NEAREST differing edge contributes.  Letting
                       every edge dither on its own would make the three
                       corners a muddle of two or three materials. */
                    int bestW = 0, bestOther = -1;
                    for (int d = 0; d < 4; ++d) {
                        if (other[d] < 0 || other[d] == t) continue;
                        const int w = EdgeWeight(lx, ly, d);
                        if (w > bestW) { bestW = w; bestOther = other[d]; }
                    }
                    if (bestOther >= 0 && Dither(lx, ly, bestW))
                        c = TileTexel(bestOther, lx, ly, v);

                    row[ox + lx] = WPixel((DWORD)c);
                }
            }
        }
    }
    return true;
}

/* The HUD thumbnail: the sheet box-averaged down by 8.  A plain point sample
   would drop the roads entirely, because a 2-tile-wide road is 4 pixels wide
   at this scale and lands on the wrong pixel most of the time. */
static WBitmap *bakeMini(const WBitmap &src, int scale)
{
    const int sw = src.GetW(), sh = src.GetH();
    const int w = sw / scale, h = sh / scale;

    WBitmap *b = new WBitmap();
    b->Create(w, h);

    for (int y = 0; y < h; ++y) {
        WPixel *row = (*b)[y];
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, bl = 0, n = 0;
            for (int j = 0; j < scale; ++j) {
                const WPixel *srow = src[y * scale + j];
                for (int i = 0; i < scale; ++i) {
                    const DWORD c = (DWORD)srow[x * scale + i];
                    r  += (int)((c >> 16) & 0xFF);
                    g  += (int)((c >>  8) & 0xFF);
                    bl += (int)( c        & 0xFF);
                    ++n;
                }
            }
            row[x] = WPixel((DWORD)(((unsigned long)(r / n) << 16)
                                  | ((unsigned long)(g / n) << 8)
                                  |  (unsigned long)(bl / n)));
        }
    }
    return b;
}

/* ------------------------------------------------------------------- build */

bool Atlas::Build(const Scene &scene)
{
    /* ---- the tileset ---------------------------------------------------- */
    for (int t = 0; t < TILE_COUNT; ++t)
        for (int v = 0; v < TILE_VARIANTS; ++v) {
            m_tile[t][v] = bakeTileBmp(t, v);
            for (int d = 0; d < 4; ++d)
                m_edge[t][v][d] = bakeEdgeBmp(t, (t + 1) % TILE_COUNT, v, d);
        }

    /* ---- the composed sheet and its thumbnails --------------------------- */
    if (!bakeGround(scene)) return false;
    /* 1/16 for the HUD corner (4 px per tile) and 1/4 for the M screen
       (16 px per tile, which is where the roads and the pond actually read). */
    m_mini = bakeMini(*m_ground, 16);
    m_miniW = m_mini->GetW();
    m_miniH = m_mini->GetH();
    m_big = bakeMini(*m_ground, 4);
    m_bigW = m_big->GetW();
    m_bigH = m_big->GetH();

    /* ---- scenery --------------------------------------------------------
       Sizes are per kind rather than one shared box: a rock that is 124 tall
       costs the same to blit as one that is 36, but the anchor arithmetic is
       easier to get right when each kind states its own numbers. */
    struct DecoDef { int w, h, ax, ay; };
    static const DecoDef def[DECO_COUNT] = {
        {   0,   0,  0,  0 },                       /* D_NONE            */
        {  48, 124, 24, 112 },                      /* D_BAMBOO          */
        {  64, 112, 32, 104 },                      /* D_PINE            */
        {  48,  36, 24,  30 },                      /* D_ROCK            */
        {  32,  68, 16,  56 },                      /* D_LANTERN         */
        { 112, 100, 56,  84 },                      /* D_HOUSE           */
        {  56,  52, 28,  44 }                       /* D_WELL            */
    };

    m_deco[D_BAMBOO]  = bakeBamboo(0);
    m_deco[D_PINE]    = bakePine(0);
    m_deco[D_ROCK]    = bakeRock(0);
    m_deco[D_LANTERN] = bakeLantern(0);
    m_deco[D_HOUSE]   = bakeHouse(0);
    m_deco[D_WELL]    = bakeWell(0);

    for (int k = 0; k < DECO_COUNT; ++k) {
        m_decoW[k]  = def[k].w;
        m_decoH[k]  = def[k].h;
        m_decoAX[k] = def[k].ax;
        m_decoAY[k] = def[k].ay;
        m_decoUp[k] = def[k].ay;
    }

    /* ---- the actor ------------------------------------------------------ */
    for (int f = 0; f < FACE_COUNT; ++f)
        for (int i = 0; i < HERO_FRAMES; ++i)
            m_hero[f][i] = bakeHero(f, i);

    /* ---- effects -------------------------------------------------------- */
    m_shadow[0] = bakeShadow( 9, 5);      /* the hero                       */
    m_shadow[1] = bakeShadow(14, 8);      /* bamboo / pine / lantern / well */
    m_shadow[2] = bakeShadow(34, 17);     /* a house                        */

    static const int glowR[GLOW_FRAMES] = { 22, 25, 21, 24 };
    for (int i = 0; i < GLOW_FRAMES; ++i) {
        m_glow[i]  = bakeGlow(glowR[i], 0xE8903A, 150);
        m_spark[i] = bakeSpark(i);
    }
    m_vignette = bakeVignette(FIELD_W, FIELD_H);

    /* ---- ui ------------------------------------------------------------- */
    for (int i = 0; i < CORNER_COUNT; ++i) {
        WBitmap *base = bakeScroll(20, COL_GOLD);
        WBitmap *c = new WBitmap();
        /* mirroring one bake into the four corners: the library cannot
           rotate, but a flip is a per-pixel index change and the motif is
           symmetric enough that the eye reads the set as a matched frame */
        c->Create(base->GetW(), base->GetH());
        for (int y = 0; y < base->GetH(); ++y)
            for (int x = 0; x < base->GetW(); ++x) {
                const int sx = (i & 1) ? base->GetW() - 1 - x : x;
                const int sy = (i & 2) ? base->GetH() - 1 - y : y;
                (*c)[y][x] = (*base)[sy][sx];
            }
        delete base;
        m_corner[i] = c;
    }
    for (int i = 0; i < CLOUD_VARIANTS; ++i) m_cloud[i] = bakeCloud(i);

    return bakeUi();
}

/*  The GDI half of the atlas.  Split out because it is the only part that can
 *  fail on a headless machine, and the caller deserves to know which half
 *  broke. */
bool Atlas::bakeUi()
{
    /* "微软雅黑" in GBK */
    static const char YAHEI[] = "\xCE\xA2\xC8\xED\xD1\xC5\xBA\xDA";

    HFONT hf = CreateFontA(17, 0, 0, 0, FW_BOLD, 0, 0, 0, GB2312_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, YAHEI);
    if (!hf) { m_ok = false; return false; }

    WFont fLabel;
    fLabel.Create(hf, 16, 1024);
    DeleteObject(hf);

    hf = CreateFontA(40, 0, 0, 0, FW_BOLD, 0, 0, 0, GB2312_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, YAHEI);
    if (!hf) { m_ok = false; return false; }

    WFont fBig;
    fBig.Create(hf, 16, 1024);
    DeleteObject(hf);

    /* Labels are baked twice: once in the ink colour for a 1 px drop shadow
       and once in silk.  Light text straight onto light stone is unreadable
       without it, and this is cheaper than a runtime shadow pass. */
    for (int i = 0; i < GF_LABEL_COUNT; ++i)
        bakeGlyph(m_label[i], m_labelW[i], fLabel, GF_LABEL[i], COL_SILK);

    /* ---- the printable ASCII range --------------------------------------
       One bitmap per character, in the label face.  WFont can rasterise
       ASCII at runtime, but it does so in its own fixed pitch 7x12 face, and
       a HUD that sets "15,16" in a different typeface from the Chinese label
       beside it looks like two programs sharing a window.  Baking the range
       costs 95 small bitmaps and gives the interface one voice. */
    {
        char one[2];
        one[1] = 0;
        for (int c = ASCII_FIRST; c <= ASCII_LAST; ++c) {
            one[0] = (char)c;
            bakeGlyph(m_ascii[c - ASCII_FIRST], m_asciiW[c - ASCII_FIRST],
                      fLabel, one, COL_SILK);
        }
    }

    /* ---- the 印章 -------------------------------------------------------
       A cinnabar ground with the character knocked out in silk, which is how
       a seal actually prints. */
    {
        const int S = 30;
        m_seal = new WBitmap();
        m_seal->Create(S, S);
        m_seal->Clear(WPixel((DWORD)COL_CINNABAR));
        for (int y = 1; y < S - 1; ++y) {
            (*m_seal)[y][1] = WPixel((DWORD)COL_GOLD);
            (*m_seal)[y][S - 2] = WPixel((DWORD)COL_GOLD);
        }
        for (int x = 1; x < S - 1; ++x) {
            (*m_seal)[1][x] = WPixel((DWORD)COL_GOLD);
            (*m_seal)[S - 2][x] = WPixel((DWORD)COL_GOLD);
        }
        m_seal->SetPos(6, 5);
        fLabel.Draw(*m_seal, GF_LABEL[GF_LBL_SEAL], WPixel((DWORD)COL_SILK),
                    FONT_NOALPHA);
        m_seal->SetPos(0, 0);
    }

    /* ---- the 匾额 -------------------------------------------------------
       The title board: dark lacquered wood, a gold frame and the village name
       in gold.  Baked rather than composed at runtime because there is no
       scaling -- a 40 px headline has to exist at 40 px. */
    {
        const char *name = GF_LABEL[GF_LBL_TITLE];
        const int textW = fBig.GetLength(name);
        const int textH = fBig.GetHeight();

        m_plaqueW = textW + 64;
        m_plaqueH = textH + 34;

        m_plaque = new WBitmap();
        m_plaque->Create(m_plaqueW, m_plaqueH);
        m_plaque->Clear(WPixel((DWORD)COL_MASK));

        /* the board: a rounded slab with a lit top edge and a dark base */
        for (int y = 3; y < m_plaqueH - 3; ++y) {
            const int inset = (y < 8) ? (8 - y) : ((y > m_plaqueH - 9) ? (y - (m_plaqueH - 9)) : 0);
            for (int x = inset + 3; x < m_plaqueW - inset - 3; ++x) {
                unsigned long c = 0x3E241A;
                if (y < 9)                     c = 0x5E3A28;
                else if (y > m_plaqueH - 10)   c = 0x281610;
                if (((x + y) & 7) == 0)        c = shade(c, -1);
                (*m_plaque)[y][x] = WPixel((DWORD)c);
            }
        }
        /* the gold frame */
        for (int y = 3; y < m_plaqueH - 3; ++y) {
            const int inset = (y < 8) ? (8 - y) : ((y > m_plaqueH - 9) ? (y - (m_plaqueH - 9)) : 0);
            (*m_plaque)[y][inset + 3] = WPixel((DWORD)COL_GOLD);
            (*m_plaque)[y][m_plaqueW - inset - 4] = WPixel((DWORD)COL_GOLD);
        }
        for (int x = 6; x < m_plaqueW - 6; ++x) {
            (*m_plaque)[4][x] = WPixel((DWORD)COL_GOLD_LIT);
            (*m_plaque)[m_plaqueH - 5][x] = WPixel((DWORD)COL_GOLD_DIM);
        }

        m_plaque->SetPos(32, 17);
        fBig.Draw(*m_plaque, name, WPixel((DWORD)COL_INK), FONT_NOALPHA);
        m_plaque->SetPos(31, 16);
        fBig.Draw(*m_plaque, name, WPixel((DWORD)COL_GOLD_LIT), FONT_NOALPHA);
        m_plaque->SetPos(0, 0);
    }

    m_ok = true;
    return true;
}

/* ---------------------------------------------------------------- getters */

WBitmap *Atlas::Tile(int t, int v) const
{
    if (t < 0 || t >= TILE_COUNT) return 0;
    if (v < 0 || v >= TILE_VARIANTS) v = 0;
    return m_tile[t][v];
}

WBitmap *Atlas::Edge(int t, int v, int dir) const
{
    if (t < 0 || t >= TILE_COUNT) return 0;
    if (v < 0 || v >= TILE_VARIANTS) v = 0;
    if (dir < 0 || dir >= 4) return 0;
    return m_edge[t][v][dir];
}

WBitmap *Atlas::Deco(int kind) const
{
    if (kind <= 0 || kind >= DECO_COUNT) return 0;
    return m_deco[kind];
}

int Atlas::DecoW(int kind) const
{
    if (kind <= 0 || kind >= DECO_COUNT) return 0;
    return m_decoW[kind];
}

int Atlas::DecoH(int kind) const
{
    if (kind <= 0 || kind >= DECO_COUNT) return 0;
    return m_decoH[kind];
}

int Atlas::DecoAnchorX(int kind) const
{
    if (kind <= 0 || kind >= DECO_COUNT) return 0;
    return m_decoAX[kind];
}

int Atlas::DecoAnchorY(int kind) const
{
    if (kind <= 0 || kind >= DECO_COUNT) return 0;
    return m_decoAY[kind];
}

int Atlas::DecoUp(int kind) const
{
    if (kind <= 0 || kind >= DECO_COUNT) return 0;
    return m_decoUp[kind];
}

WBitmap *Atlas::Hero(int face, int frame) const
{
    if (face < 0 || face >= FACE_COUNT) face = FACE_D;
    if (frame < 0 || frame >= HERO_FRAMES) frame = 0;
    return m_hero[face][frame];
}

WBitmap *Atlas::Shadow(int tier) const
{
    if (tier < 0 || tier >= SHADOW_TIERS) tier = 0;
    return m_shadow[tier];
}

WBitmap *Atlas::Glow(int frame) const
{
    if (frame < 0 || frame >= GLOW_FRAMES) frame = 0;
    return m_glow[frame];
}

WBitmap *Atlas::Spark(int frame) const
{
    if (frame < 0 || frame >= GLOW_FRAMES) frame = 0;
    return m_spark[frame];
}

WBitmap *Atlas::Vignette() const { return m_vignette; }

WBitmap *Atlas::Label(int id) const
{
    if (id < 0 || id >= GF_LABEL_COUNT) return 0;
    return m_label[id];
}

int Atlas::LabelW(int id) const
{
    if (id < 0 || id >= GF_LABEL_COUNT) return 0;
    return m_labelW[id];
}

WBitmap *Atlas::Ascii(int c) const
{
    if (c < ASCII_FIRST || c > ASCII_LAST) return 0;
    return m_ascii[c - ASCII_FIRST];
}

int Atlas::AsciiW(int c) const
{
    if (c < ASCII_FIRST || c > ASCII_LAST) return 0;
    return m_asciiW[c - ASCII_FIRST];
}

WBitmap *Atlas::Corner(int i) const
{
    if (i < 0 || i >= CORNER_COUNT) i = 0;
    return m_corner[i];
}

WBitmap *Atlas::Cloud(int i) const
{
    if (i < 0 || i >= CLOUD_VARIANTS) i = 0;
    return m_cloud[i];
}

WBitmap *Atlas::Seal() const { return m_seal; }

WBitmap *Atlas::Plaque() const { return m_plaque; }

} /* namespace gf */
