/* ===========================================================================
 *  pvz_art.cpp -- Lawn Defense : the procedural baker
 * ---------------------------------------------------------------------------
 *  Palette, pixel primitives, a cel-outline dilator, then the striped lawn, the
 *  house, the three plants, the zombies, the pea, the sun, the seed packets,
 *  the title/win/lose plaques and the GDI label/ASCII glyphs.  See pvz_art.h
 *  for the "why" of every choice.
 *
 *  ASCII only.  The Chinese labels come from pvz_text.h + a GB2312 GDI face.
 * ===========================================================================
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wsfont.h"
#include "wserror.h"

#include "pvz_art.h"

namespace pvz {

/* ------------------------------------------------------------------ palette
   0xRRGGBB; WPixel folds it to RGB565 on the way into a bitmap.  A bright,
   saturated, high-contrast cartoon set -- the two greens of the lawn do most
   of the work of making it read as "lawn" at a glance. */
static const unsigned long
    /* lawn */
    C_GRASS_A  = 0x74B33C, C_GRASS_B  = 0x5E9B2E,
    C_GRASS_LN = 0x4A7A24, C_SPEC_L   = 0x8FCB4A, C_SPEC_D = 0x4E7F26,
    C_FLOWER_W = 0xFBF6E8, C_FLOWER_Y = 0xFFD94A, C_FLOWER_P = 0xF5A8C0,
    C_FLOWER_C = 0xE8B400, C_TUFT     = 0x3F6B1E,
    /* sky (title backdrop) */
    C_SKY_HI   = 0x8ED4F5, C_SKY_LO   = 0xDCF2FC, C_CLOUD   = 0xFFFFFF,
    C_CLOUD_SH = 0xC7E4F5, C_HILL_A   = 0x5E9B2E, C_HILL_B  = 0x4A7A24,
    /* top bar / wood */
    C_WOOD_HI  = 0xC89B6A, C_WOOD     = 0x8A5E33, C_WOOD_LO = 0x6B4423,
    C_WOOD_LN  = 0x4E3016,
    /* house */
    C_WALL = 0xE8DCC0, C_WALL_SH = 0xC2B48E, C_ROOF = 0xC4453A,
    C_ROOF_SH = 0x8E2C22, C_DOOR = 0x6D4C41, C_DOOR_LT = 0x8A6A5C, C_WINDOW = 0x9AD9E8,
    C_BRICK = 0xB8A784, C_GLASS_HI = 0xC8ECF7, C_GLASS_LO = 0x74BEDA,
    /* peashooter */
    C_PEASH = 0x6BBB46, C_PEASH_DK = 0x3F8B2E, C_PEASH_LT = 0x93DB6B,
    C_ICE = 0x55BDE8, C_ICE_DK = 0x3184B2, C_ICE_LT = 0xA4E8FF,
    C_STEM = 0x4E9C39, C_STEM_LT = 0x6FC258, C_STEM_DK = 0x3A7326,
    C_LEAF = 0x57A83C, C_LEAF_LT = 0x7FCC58, C_LEAF_DK = 0x3E7A2A,
    /* sunflower */
    C_PETAL = 0xF7D13B, C_PETAL_DK = 0xF2B824, C_PETAL_LT = 0xFBE98A,
    C_CENTER = 0x8A5524, C_CENTER_L = 0xA9702F, C_SEED_DK = 0x5E3A16,
    /* wall-nut */
    C_WNUT = 0xC08B3E, C_WNUT_LT = 0xDCAA5C, C_WNUT_DK = 0x96682C,
    /* zombie */
    C_ZSKIN = 0xB0BE78, C_ZSKIN_SH = 0x8E9A5C, C_ZSKIN_LT = 0xCBD48F,
    C_ZCOAT = 0x5A4A38, C_ZCOAT_DK = 0x3E3226, C_ZCOAT_LT = 0x6E5A44,
    C_SLEEVE = 0x4A3E2E, C_SLEEVE_LT = 0x6E5A44, C_SHOE = 0x241C14,
    C_ZHAIR = 0x2E2620, C_ZEYE = 0xEDEADF, C_ZMOUTH = 0x2A1A1A,
    C_BTN   = 0xD9B25A,
    /* props */
    C_PEA = 0x7FC24A, C_PEA_LT = 0xA6DA6A, C_PEA_DK = 0x4E8C2E,
    C_SUN1 = 0xFFE24A, C_SUN2 = 0xF6C02A,
    C_SHADOW = 0x242424,
    /* ui */
    C_SEEDBG = 0xE9C77B, C_SEED_LO = 0xB08A48, C_SEED_LT = 0xF4DCA8,
    C_BADGE  = 0x3A2A16,
    C_GOLD_HI = 0xF2D98A, C_GOLD = 0xB8893A, C_GOLD_LO = 0x8A6224,
    C_WIN_HI = 0x8FD96A, C_WIN = 0x4E8C3A, C_WIN_LO = 0x356428,
    C_LOSE_HI = 0xE07A63, C_LOSE = 0xC4523E, C_LOSE_LO = 0x8A2E22,
    C_WHITE = 0xFFFFFF, C_NEARBLACK = 0x101010;

/* The BLIT_MASK key in RGB565 is magenta 0xF81F; WPixel(0xFF00FF) folds to it. */
#define MASKKEY 0xFF00FFul

/* --------------------------------------------------------------- primitives */

static WPixel mk(unsigned long c) { return WPixel((DWORD)c); }

static void pxb(WBitmap &b, int x, int y, unsigned long c)
{
    const int w = b.GetW(), h = b.GetH();
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    b[y][x] = mk(c);
}

static void fillRect(WBitmap &b, int x, int y, int w, int h, unsigned long c)
{
    const int bw = b.GetW(), bh = b.GetH();
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > bw) w = bw - x;
    if (y + h > bh) h = bh - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            b[yy][xx] = mk(c);
}

/* A filled, axis-aligned ellipse.  Every cartoon shape here is an ellipse or
   a rectangle, so this and fillRect are the whole drawing vocabulary. */
static void disc(WBitmap &b, int cx, int cy, int rx, int ry, unsigned long c)
{
    if (rx <= 0 || ry <= 0) return;
    for (int yy = cy - ry; yy <= cy + ry; ++yy)
        for (int xx = cx - rx; xx <= cx + rx; ++xx) {
            const float fx = (float)(xx - cx) / (float)rx;
            const float fy = (float)(yy - cy) / (float)ry;
            if (fx * fx + fy * fy <= 1.0f) {
                const int bw = b.GetW(), bh = b.GetH();
                if (xx >= 0 && yy >= 0 && xx < bw && yy < bh) b[yy][xx] = mk(c);
            }
        }
}

/* ---------------------------------------------------------------- shading
   The whole step up in picture quality is cel SHADING: every blob is drawn as
   a lit volume instead of a flat cut-out.  Three primitives do it, all in
   8-bit maths folded to RGB565 by mk():

     discShade  -- base ellipse + a light crescent top-left + a dark crescent
                   bottom-right, so one call gives a rounded solid;
     shadeRect  -- flat body with a lit top band and a shaded bottom band;
     glowDisc   -- concentric rings from a hot core to a cool rim (the sun);
     vgrad      -- a per-row vertical interpolation (sky, lawn depth);
     blend      -- the 8-bit lerp all of them share.
   -------------------------------------------------------------------------- */

static unsigned long blend(unsigned long a, unsigned long b, float f)
{
    int ar = (int)((a >> 16) & 0xff), ag = (int)((a >> 8) & 0xff), ab = (int)(a & 0xff);
    const int br = (int)((b >> 16) & 0xff), bg = (int)((b >> 8) & 0xff), bb = (int)(b & 0xff);
    const int r = ar + (int)((br - ar) * f);
    const int g = ag + (int)((bg - ag) * f);
    const int l = ab + (int)((bb - ab) * f);
    return ((unsigned long)(r & 0xff) << 16) | ((unsigned long)(g & 0xff) << 8) |
           (unsigned long)(l & 0xff);
}

static void discShade(WBitmap &b, int cx, int cy, int rx, int ry,
                      unsigned long base, unsigned long light, unsigned long dark)
{
    disc(b, cx, cy, rx, ry, base);
    if (rx <= 0 || ry <= 0) return;
    /* light crescent: the upper-left quadrant at ~55% radius */
    for (int yy = cy - ry; yy <= cy; ++yy)
        for (int xx = cx - rx; xx <= cx; ++xx) {
            const float fx = (float)(xx - cx) / (float)rx;
            const float fy = (float)(yy - cy) / (float)ry;
            if (fx * fx + fy * fy <= 0.30f) pxb(b, xx, yy, light);
        }
    /* dark crescent: the lower-right quadrant at ~72% radius */
    for (int yy = cy; yy <= cy + ry; ++yy)
        for (int xx = cx; xx <= cx + rx; ++xx) {
            const float fx = (float)(xx - cx) / (float)rx;
            const float fy = (float)(yy - cy) / (float)ry;
            if (fx * fx + fy * fy <= 0.52f) pxb(b, xx, yy, dark);
        }
}

static void shadeRect(WBitmap &b, int x, int y, int w, int h,
                      unsigned long base, unsigned long light, unsigned long dark,
                      int band)
{
    fillRect(b, x, y, w, h, base);
    if (h < band * 2 + 2 || w <= 0) return;
    fillRect(b, x, y, w, band, light);
    fillRect(b, x, y + h - band, w, band, dark);
}

/* Concentric rings, hot centre to cool rim; the sun and its rays use it. */
static void glowDisc(WBitmap &b, int cx, int cy, int rx, int ry,
                     unsigned long core, unsigned long edge, int bands)
{
    if (bands < 2) bands = 2;
    for (int i = bands; i >= 1; --i) {
        const float f = (float)(i - 1) / (float)(bands - 1);   /* 0 core..1 rim */
        const int rrx = (int)(rx * i / bands), rry = (int)(ry * i / bands);
        disc(b, cx, cy, rrx, rry, blend(edge, core, 1.0f - f));
    }
    disc(b, cx, cy, rx / 4 + 1, ry / 4 + 1, blend(core, 0xFFFFFF, 0.55f));
}

static void vgrad(WBitmap &b, int x, int y, int w, int h,
                  unsigned long top, unsigned long bottom)
{
    if (h <= 0 || w <= 0) return;
    for (int i = 0; i < h; ++i) {
        const float f = (h > 1) ? (float)i / (float)(h - 1) : 0.0f;
        fillRect(b, x, y + i, w, 1, blend(top, bottom, f));
    }
}

/* Rounded rectangle: a fillRect whose four corner rows are clipped. */
static void roundRect(WBitmap &b, int x, int y, int w, int h, int r, unsigned long c)
{
    if (r < 1) r = 1;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int yy = 0; yy < h; ++yy) {
        int inset = 0;
        if (yy < r) inset = r - yy;
        else if (yy >= h - r) inset = yy - (h - r) + 1;
        fillRect(b, x + inset, y + yy, w - inset * 2, 1, c);
    }
}

/*  The cel outline: dilate the silhouette into the mask background N passes
 *  and paint the dilation a near-black tone.  Pure green-screen maths -- no
 *  WZoomBitmap required.  A pass reads the whole mask state then writes, so
 *  the dilation thickens monotonically pass by pass. */
static void celOutline(WBitmap &b, unsigned long edge, int passes)
{
    const int w = b.GetW(), h = b.GetH();
    const WORD mask = WPixel((DWORD)MASKKEY).color;
    for (int p = 0; p < passes; ++p) {
        int *xs = new int[w * h];
        int *ys = new int[w * h];
        int n = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (b[y][x].color != mask) continue;       /* already content */
                int touch = 0;
                for (int dy = -1; dy <= 1 && !touch; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        if (dx == 0 && dy == 0) continue;
                        if (b[ny][nx].color != mask) { touch = 1; break; }
                    }
                if (touch) { xs[n] = x; ys[n] = y; ++n; }
            }
        }
        for (int i = 0; i < n; ++i) b[ys[i]][xs[i]] = mk(edge);
        delete[] xs; delete[] ys;
    }
}

/*  Copy a baked glyph into dst, tinted to `col`, leaving the mask as-is.  Used
 *  for the seed-packet costs and, in the renderer, the HUD numbers. */
static void stampGlyph(WBitmap &dst, WBitmap *g, int x, int y, unsigned long col)
{
    if (!g) return;
    const WORD mask = WPixel((DWORD)MASKKEY).color;
    const int sw = g->GetW(), sh = g->GetH();
    const int dw = dst.GetW(), dh = dst.GetH();
    for (int sy = 0; sy < sh; ++sy) {
        const int dy = y + sy;
        if (dy < 0 || dy >= dh) continue;
        for (int sx = 0; sx < sw; ++sx) {
            const int dx = x + sx;
            if (dx < 0 || dx >= dw) continue;
            if ((*g)[sy][sx].color == mask) continue;
            dst[dy][dx] = mk(col);
        }
    }
}

/* A deterministic grass speckle: 0 = "leave the row base", else a tuft colour.
   The single source of truth for the baked lawn and for test [art]. */
unsigned long LawnSpeckle(int x, int y)
{
    unsigned int h = (unsigned int)(x * 2654435761u) ^ (unsigned int)(y * 40503u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    const unsigned int r = h % 100u;
    if (r < 8u)    return C_SPEC_L;
    if (r >= 92u)  return C_SPEC_D;
    return 0;
}

/* A different hash for the lawn DECOR, so it never lands on the speckle's
   rhythm.  Drives flowers, grass blades and pebbles. */
static unsigned long DecoHash(int x, int y)
{
    unsigned int h = (unsigned int)(x * 374761393u) ^ (unsigned int)(y * 668265263u);
    h ^= h >> 13; h *= 0x85ebca6bu; h ^= h >> 16; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}

/* The striped lawn itself, painted into `b` from (0, y0) down.  Shared by the
   gameplay background and the title backdrop so both read as the same turf:
   a per-row vertical gradient (depth), the two-green row alternation, the
   speckle, and then deterministic decor -- flowers and grass blades that
   dodge the placement grid lines. */
static void paintLawn(WBitmap &b, int y0, int rows)
{
    const int lawnH = ROW_H * rows;
    /* per-row gradient, alternating the two greens */
    for (int r = 0; r < rows; ++r) {
        const unsigned long base = (r & 1) ? C_GRASS_B : C_GRASS_A;
        const unsigned long top  = blend(base, 0xFFFFFF, 0.10f);
        const unsigned long bot  = blend(base, 0x000000, 0.14f);
        vgrad(b, 0, y0 + r * ROW_H, FIELD_W, ROW_H, top, bot);
    }
    /* speckle texture */
    for (int y = y0; y < y0 + lawnH; ++y)
        for (int x = 0; x < FIELD_W; ++x) {
            const unsigned long s = LawnSpeckle(x, y);
            if (s) b[y][x] = mk(s);
        }
    /* decor: tiny flowers (5px) and grass blades (1x3), both dodging the
       cell guide lines so the grid stays readable */
    for (int y = y0 + 3; y < y0 + lawnH - 3; ++y) {
        for (int x = 3; x < FIELD_W - 3; ++x) {
            if ((y - y0) % ROW_H < 3 || (y - y0) % ROW_H > ROW_H - 4) continue;
            if (x % COL_W < 3 || x % COL_W > COL_W - 4) continue;
            const unsigned long h = DecoHash(x, y);
            if (h % 977u == 0u) {                    /* a flower */
                const unsigned long petal = (h % 3u == 0u) ? C_FLOWER_W :
                                            (h % 3u == 1u) ? C_FLOWER_P : C_FLOWER_Y;
                pxb(b, x, y - 1, petal); pxb(b, x - 1, y, petal);
                pxb(b, x + 1, y, petal); pxb(b, x, y + 1, petal);
                pxb(b, x, y, C_FLOWER_C);
            } else if (h % 131u == 0u) {             /* a grass blade */
                pxb(b, x, y - 2, C_TUFT);
                pxb(b, x, y - 1, C_TUFT);
                pxb(b, x, y, blend(C_TUFT, 0xFFFFFF, 0.2f));
            }
        }
    }
    /* row + column guides make the cells legible for placement */
    for (int r = 1; r < rows; ++r)
        fillRect(b, 0, y0 + r * ROW_H, FIELD_W, 1, C_GRASS_LN);
    for (int c = 1; c < COLS; ++c)
        fillRect(b, GRID_X0 + c * COL_W - 1, y0, 1, lawnH, C_GRASS_LN);
}

/* ------------------------------------------------------------------- atlas */

Atlas::Atlas() : m_ok(false)
{
    m_bg = m_house = m_titlebg = m_pea = m_icepea = m_shadow = m_sun = 0;
    m_cloud = m_mote = 0;
    for (int i = 0; i < PL_COUNT; ++i) m_plant[i] = 0;
    for (int i = 0; i < 3; ++i) m_wnut[i] = 0;
    for (int f = 0; f < ZOMBIE_FRAMES; ++f)
        for (int e = 0; e < ZOMBIE_EATS; ++e)
            for (int k = 0; k < ZOMBIE_KINDS; ++k)
                for (int c = 0; c < ZOMBIE_CHILL; ++c) m_zombie[f][e][k][c] = 0;
    for (int i = 0; i < SPLAT_FRAMES; ++i) m_splat[i] = 0;
    for (int i = 0; i < BLAST_FRAMES; ++i) m_blast[i] = 0;
    for (int i = 0; i < PL_COUNT - 1; ++i) m_seed[i] = 0;
    for (int i = 0; i < 3; ++i) m_plaque[i] = 0;
    for (int i = 0; i < PVZ_LABEL_COUNT; ++i) { m_label[i] = 0; m_labelW[i] = 0; }
    for (int i = 0; i < PVZ_ASCII_COUNT; ++i) { m_ascii[i] = 0; m_asciiW[i] = 0; }
}

Atlas::~Atlas() { Destroy(); }

void Atlas::Destroy()
{
    delete m_bg;      m_bg = 0;
    delete m_house;   m_house = 0;
    delete m_titlebg; m_titlebg = 0;
    delete m_pea;     m_pea = 0;
    delete m_icepea;  m_icepea = 0;
    delete m_shadow;  m_shadow = 0;
    delete m_sun;     m_sun = 0;
    delete m_cloud;   m_cloud = 0;
    delete m_mote;    m_mote = 0;
    /* the wall-nut's stage-0 bitmap lives in m_wnut[]; m_plant[] only CACHEs
       the pointer, so it must not be deleted a second time */
    m_plant[PL_WALLNUT] = 0;
    for (int i = 0; i < PL_COUNT; ++i) { delete m_plant[i]; m_plant[i] = 0; }
    for (int i = 0; i < 3; ++i) { delete m_wnut[i]; m_wnut[i] = 0; }
    for (int f = 0; f < ZOMBIE_FRAMES; ++f)
        for (int e = 0; e < ZOMBIE_EATS; ++e)
            for (int k = 0; k < ZOMBIE_KINDS; ++k)
                for (int c = 0; c < ZOMBIE_CHILL; ++c) {
                    delete m_zombie[f][e][k][c];
                    m_zombie[f][e][k][c] = 0;
                }
    for (int i = 0; i < SPLAT_FRAMES; ++i) { delete m_splat[i]; m_splat[i] = 0; }
    for (int i = 0; i < BLAST_FRAMES; ++i) { delete m_blast[i]; m_blast[i] = 0; }
    for (int i = 0; i < PL_COUNT - 1; ++i) { delete m_seed[i]; m_seed[i] = 0; }
    for (int i = 0; i < 3; ++i) { delete m_plaque[i]; m_plaque[i] = 0; }
    for (int i = 0; i < PVZ_LABEL_COUNT; ++i) { delete m_label[i]; m_label[i] = 0; }
    for (int i = 0; i < PVZ_ASCII_COUNT; ++i) { delete m_ascii[i]; m_ascii[i] = 0; }
}

WBitmap *Atlas::Plant(int kind) const
{
    if (kind <= PL_NONE || kind >= PL_COUNT) return 0;
    return m_plant[kind];
}

WBitmap *Atlas::PlantHp(int kind, int hp) const
{
    if (kind != PL_WALLNUT) return Plant(kind);
    if (hp > PLANT_HP_WALLNUT * 2 / 3) return m_plant[PL_WALLNUT];
    if (hp > PLANT_HP_WALLNUT / 3)     return m_wnut[1];
    return m_wnut[2];
}

WBitmap *Atlas::Splat(int frame) const
{
    if (frame < 0) frame = 0;
    if (frame >= SPLAT_FRAMES) frame = SPLAT_FRAMES - 1;
    return m_splat[frame];
}

WBitmap *Atlas::Zombie(int frame, int eating, int kind, int chilled) const
{
    if (frame < 0) frame = 0;
    if (frame >= ZOMBIE_FRAMES) frame = ZOMBIE_FRAMES - 1;
    if (kind < 0 || kind >= ZOMBIE_KINDS) kind = ZK_NORMAL;
    if (eating) frame = frame;
    return m_zombie[frame][eating ? 1 : 0][kind][chilled ? 1 : 0];
}

WBitmap *Atlas::Blast(int frame) const
{
    if (frame < 0) frame = 0;
    if (frame >= BLAST_FRAMES) frame = BLAST_FRAMES - 1;
    return m_blast[frame];
}

WBitmap *Atlas::Seed(int i) const
{
    if (i < 0 || i >= PL_COUNT - 1) return 0;
    return m_seed[i];
}

WBitmap *Atlas::Plaque(int which) const
{
    if (which < 0 || which > 2) which = 0;
    return m_plaque[which];
}

/* --------------------------------------------------------------- the build */

bool Atlas::Build()
{
    Destroy();                                 /* idempotent */

    m_cloud  = bakeCloud();
    m_mote   = bakeMote();
    m_bg     = bakeBackground();
    m_house  = bakeHouse();
    m_titlebg = bakeTitleBg();
    m_plant[PL_PEASHOOTER] = bakePeashooter(0);
    m_plant[PL_SUNFLOWER]  = bakeSunflower();
    for (int s = 0; s < 3; ++s) m_wnut[s] = bakeWallnut(s);
    m_plant[PL_WALLNUT] = m_wnut[0];
    m_plant[PL_SNOWPEA] = bakePeashooter(1);
    m_plant[PL_CHERRY] = bakeCherry();
    for (int f = 0; f < ZOMBIE_FRAMES; ++f)
        for (int e = 0; e < ZOMBIE_EATS; ++e)
            for (int k = 0; k < ZOMBIE_KINDS; ++k)
                for (int c = 0; c < ZOMBIE_CHILL; ++c)
                    m_zombie[f][e][k][c] = bakeZombie(f, e, k, c);
    m_pea = bakePea(0);
    m_icepea = bakePea(1);
    for (int i = 0; i < SPLAT_FRAMES; ++i) m_splat[i] = bakeSplat(i);
    for (int i = 0; i < BLAST_FRAMES; ++i) m_blast[i] = bakeBlast(i);
    m_shadow = bakeShadow();
    m_sun    = bakeSun();
    m_plaque[0] = bakePlaque(0);
    m_plaque[1] = bakePlaque(1);
    m_plaque[2] = bakePlaque(2);

    if (!bakeUi()) { m_ok = false; return false; }  /* GDI face could fail */

    for (int i = 0; i < PL_COUNT - 1; ++i) m_seed[i] = bakeSeed(i);

    m_ok = true;
    return true;
}

/* --------------------------------------------------------------- the world */

WBitmap *Atlas::bakeBackground()
{
    WBitmap *b = new WBitmap();
    b->Create(FIELD_W, FIELD_H);

    /* ---- the wooden seed bar: vertical planks over a flat body ---------- */
    fillRect(*b, 0, 0, FIELD_W, TOP_H - 4, C_WOOD);
    /* grain: two faint darker hairlines per plank, from the decor hash */
    for (int px0 = 0; px0 < FIELD_W; px0 += 80)
        for (int y = 12; y < TOP_H - 12; y += 14) {
            const unsigned long h = DecoHash(px0, y);
            if (h % 2u == 0u) fillRect(*b, px0 + 6, y, 68, 1, C_WOOD_LO);
            else              fillRect(*b, px0 + 10, y + 7, 60, 1, C_WOOD_LO);
        }
    /* plank seams + nails */
    for (int x = 40; x < FIELD_W; x += 80) {
        fillRect(*b, x, 6, 2, TOP_H - 14, C_WOOD_LN);
        pxb(*b, x + 5, 14, C_WOOD_LN); pxb(*b, x + 5, TOP_H - 16, C_WOOD_LN);
    }
    /* top light band, bottom shadow band, and a gold trim for pop */
    fillRect(*b, 0, 0, FIELD_W, 6, C_WOOD_HI);
    fillRect(*b, 0, 6, FIELD_W, 1, blend(C_WOOD_HI, 0xFFFFFF, 0.4f));
    fillRect(*b, 0, TOP_H - 8, FIELD_W, 4, C_WOOD_LO);
    fillRect(*b, 0, TOP_H - 4, FIELD_W, 3, C_WOOD_LN);
    fillRect(*b, 0, TOP_H - 7, FIELD_W, 1, C_GOLD);

    /* ---- the lawn ------------------------------------------------------- */
    paintLawn(*b, LAWN_Y0, ROWS);
    fillRect(*b, 0, FIELD_H - 3, FIELD_W, 3, C_GRASS_LN);
    return b;
}

WBitmap *Atlas::bakeHouse()
{
    WBitmap *b = new WBitmap();
    b->Create(HOUSE_W, FIELD_H);

    /* ---- the wall: brick courses with staggered joints ------------------ */
    fillRect(*b, 0, 56, HOUSE_W, FIELD_H - 56, C_WALL);
    for (int y = 56, course = 0; y < FIELD_H; y += 16, ++course) {
        fillRect(*b, 0, y, HOUSE_W, 1, C_BRICK);                 /* mortar */
        for (int jx = (course & 1) ? 20 : 0; jx < HOUSE_W; jx += 40) {
            if (y + 1 < FIELD_H) fillRect(*b, jx, y + 1, 1, 14, C_BRICK);
        }
    }
    fillRect(*b, HOUSE_W - 8, 56, 8, FIELD_H - 56, C_WALL_SH);   /* side shade */

    /* ---- the roof: three scalloped shingle rows + ridge ----------------- */
    fillRect(*b, 0, 0, HOUSE_W, 8, C_ROOF_SH);                   /* ridge cap */
    for (int sy = 8; sy < 56; sy += 16) {
        for (int x = -6; x < HOUSE_W + 6; x += 19)
            disc(*b, x + 9, sy + 6, 10, 8, C_ROOF);
        fillRect(*b, 0, sy + 11, HOUSE_W, 2, C_ROOF_SH);         /* shadow gap */
        fillRect(*b, 0, sy, HOUSE_W, 1, blend(C_ROOF, 0xFFFFFF, 0.25f));
    }
    fillRect(*b, 0, 52, HOUSE_W, 5, C_ROOF_SH);                  /* eave */

    /* ---- the window: frame, two-pane light, sill ------------------------ */
    fillRect(*b, 14, 150, 34, 30, C_WOOD_LN);                    /* frame */
    vgrad(*b, 16, 152, 30, 26, C_GLASS_HI, C_GLASS_LO);          /* glass */
    fillRect(*b, 29, 152, 2, 26, C_WOOD_LN);                     /* mullion v */
    fillRect(*b, 16, 164, 30, 2, C_WOOD_LN);                     /* mullion h */
    fillRect(*b, 18, 154, 8, 3, blend(C_GLASS_HI, 0xFFFFFF, 0.6f)); /* glare */
    fillRect(*b, 12, 180, 38, 3, C_WOOD_LO);                     /* sill */
    fillRect(*b, 12, 180, 38, 1, blend(C_WOOD_LO, 0xFFFFFF, 0.35f));

    /* ---- the door: frame, two inset panels, knob, step ------------------ */
    fillRect(*b, 18, 380, 30, 130, C_WOOD_LO);                   /* frame */
    fillRect(*b, 20, 382, 26, 126, C_DOOR);
    fillRect(*b, 20, 382, 26, 3, C_DOOR_LT);                     /* top light */
    fillRect(*b, 20, 505, 26, 3, C_DOOR);                        /* foot shade */
    for (int py = 392; py < 500; py += 56) {                     /* panels */
        fillRect(*b, 25, py, 16, 48, C_DOOR_LT);
        fillRect(*b, 25, py, 16, 2, C_DOOR);
        fillRect(*b, 25, py + 46, 16, 2, C_DOOR);
        fillRect(*b, 25, py, 2, 48, C_DOOR);
        fillRect(*b, 39, py, 2, 48, C_DOOR);
    }
    disc(*b, 42, 448, 3, 3, C_GOLD);
    pxb(*b, 42, 447, blend(C_GOLD, 0xFFFFFF, 0.5f));
    fillRect(*b, 10, 510, 46, 6, C_BRICK);                       /* step */
    fillRect(*b, 10, 510, 46, 2, blend(C_BRICK, 0xFFFFFF, 0.3f));

    /* a dark right border so the house reads against the lawn */
    fillRect(*b, HOUSE_W - 3, 0, 3, FIELD_H, C_WOOD_LN);
    return b;
}

/* ------------------------------------------------------------------- actors */

/* A new sprite on the magenta mask field. */
static WBitmap *newMasked(int w, int h)
{
    WBitmap *b = new WBitmap();
    b->Create(w, h);
    b->Clear(WPixel((DWORD)MASKKEY));
    return b;
}

WBitmap *Atlas::bakePeashooter(int snow) /* 64 x 80 */
{
    const unsigned long head = snow ? C_ICE : C_PEASH;
    const unsigned long headLt = snow ? C_ICE_LT : C_PEASH_LT;
    const unsigned long headDk = snow ? C_ICE_DK : C_PEASH_DK;
    WBitmap *b = newMasked(64, 80);
    /* the leaf pair, lit from the upper left */
    discShade(*b, 14, 66, 16, 8, C_LEAF, C_LEAF_LT, C_LEAF_DK);
    discShade(*b, 50, 66, 16, 8, C_LEAF, C_LEAF_LT, C_LEAF_DK);
    pxb(*b, 8, 66, C_LEAF_DK); pxb(*b, 56, 66, C_LEAF_DK);
    /* the stem with a lit seam and a soil collar */
    fillRect(*b, 26, 44, 7, 36, C_STEM);
    fillRect(*b, 26, 44, 2, 36, C_STEM_LT);
    fillRect(*b, 24, 74, 11, 5, C_STEM_DK);
    fillRect(*b, 24, 74, 11, 2, blend(C_STEM_DK, 0xFFFFFF, 0.25f));
    /* the head */
    discShade(*b, 29, 36, 22, 22, head, headLt, headDk);
    /* the snout: a tube, lit on top, with a dark bore */
    fillRect(*b, 42, 18, 16, 22, headDk);
    fillRect(*b, 42, 18, 16, 3, head);
    fillRect(*b, 42, 37, 16, 3, headDk);
    discShade(*b, 57, 29, 7, 11, headDk, head, headDk);
    disc(*b, 58, 29, 5, 9, 0x243C18);
    disc(*b, 58, 27, 3, 6, blend(0x243C18, 0xFFFFFF, 0.18f));
    fillRect(*b, 40, 40, 10, 2, headDk);             /* the mouth seam */
    /* eyes: almond, pupil, glint; plus heavy cartoon brows */
    disc(*b, 20, 27, 5, 6, C_WHITE);
    disc(*b, 21, 28, 2, 3, C_NEARBLACK);
    pxb(*b, 20, 26, C_WHITE);
    disc(*b, 33, 25, 5, 6, C_WHITE);
    disc(*b, 34, 26, 2, 3, C_NEARBLACK);
    pxb(*b, 33, 24, C_WHITE);
    fillRect(*b, 15, 20, 7, 2, headDk);
    fillRect(*b, 30, 18, 7, 2, headDk);
    /* cheeks */
    pxb(*b, 13, 37, headDk); pxb(*b, 14, 38, headDk);
    pxb(*b, 44, 36, headDk); pxb(*b, 45, 37, headDk);
    /* two-tone cel outline: inner hue plus near-black outside */
    celOutline(*b, snow ? 0x1A5574 : 0x1E4D14, 1);
    celOutline(*b, 0x0E2A0A, 1);
    return b;
}

WBitmap *Atlas::bakeSunflower()           /* 60 x 76 */
{
    static const int px[12] = { 21, 18, 10, 0, -10, -18, -21, -18, -10, 0, 10, 18 };
    static const int py[12] = {  0, 10, 16, 21, 16,  10,   0, -10, -16, -21, -16, -10 };
    WBitmap *b = newMasked(60, 76);
    /* two petal rings, outer full-sized, inner at 0.62 and shade-swapped so
       the whorl reads as overlapping petals rather than one flat star */
    for (int i = 0; i < 12; ++i)
        discShade(*b, 30 + px[i], 28 + py[i], 8, 7,
                  (i & 1) ? C_PETAL : C_PETAL_DK, C_PETAL_LT, C_PETAL_DK);
    for (int i = 0; i < 12; ++i)
        discShade(*b, 30 + (px[i] * 63) / 100, 28 + (py[i] * 63) / 100, 5, 4,
                  (i & 1) ? C_PETAL_DK : C_PETAL, C_PETAL_LT, blend(C_PETAL_DK, 0x000000, 0.2f));
    /* the seed head: dark ring, lighter face, sunflower seeds on a ring */
    disc(*b, 30, 28, 12, 12, C_CENTER);
    disc(*b, 30, 28, 9, 9, C_CENTER_L);
    for (int i = 0; i < 12; ++i) {
        const int sx = 30 + (px[i] * 7) / 10, sy = 28 + (py[i] * 7) / 10;
        pxb(*b, sx, sy, C_SEED_DK);
        pxb(*b, sx, sy + 1, blend(C_SEED_DK, C_CENTER, 0.4f));
    }
    /* the stem with a lit seam and two shaded leaves with veins */
    fillRect(*b, 27, 40, 6, 36, C_STEM);
    fillRect(*b, 27, 40, 2, 36, C_STEM_LT);
    fillRect(*b, 25, 74, 10, 5, C_STEM_DK);
    discShade(*b, 13, 64, 15, 8, C_LEAF, C_LEAF_LT, C_LEAF_DK);
    discShade(*b, 47, 64, 15, 8, C_LEAF, C_LEAF_LT, C_LEAF_DK);
    pxb(*b, 14, 64, C_LEAF_DK); pxb(*b, 46, 64, C_LEAF_DK);
    /* the face */
    disc(*b, 24, 26, 3, 4, C_WHITE); disc(*b, 25, 27, 1, 2, C_NEARBLACK);
    disc(*b, 36, 26, 3, 4, C_WHITE); disc(*b, 35, 27, 1, 2, C_NEARBLACK);
    fillRect(*b, 24, 33, 12, 2, C_NEARBLACK);          /* a small smile */
    pxb(*b, 21, 31, blend(C_PETAL_DK, 0xE86A6A, 0.5f));
    pxb(*b, 39, 31, blend(C_PETAL_DK, 0xE86A6A, 0.5f));
    celOutline(*b, 0x6B4A12, 1);
    celOutline(*b, 0x2E1E08, 1);
    return b;
}

/* Wall-nut, three damage stages: 0 healthy, 1 chipped, 2 cracked + worried.
   The renderer picks one from the plant's remaining hp, so damage is visible
   without touching the simulation. */
WBitmap *Atlas::bakeWallnut(int stage)     /* 58 x 66 */
{
    WBitmap *b = newMasked(58, 66);
    discShade(*b, 29, 36, 24, 28, C_WNUT, C_WNUT_LT, C_WNUT_DK);
    /* a glossy top sheen and two wood-grain arcs */
    disc(*b, 24, 22, 11, 7, blend(C_WNUT_LT, 0xFFFFFF, 0.30f));
    static const int gvx[10] = { -15, -11, -7, -3, 0, 3, 7, 11, 15, 18 };
    static const int gvy[10] = {   6,  11, 15, 18, 20, 18, 15, 11,  6,  0 };
    for (int i = 0; i < 10; ++i) {
        pxb(*b, 29 + gvx[i], 24 + gvy[i] / 2, blend(C_WNUT_DK, C_WNUT, 0.35f));
        pxb(*b, 25 + gvx[i], 40 + gvy[i] / 2, blend(C_WNUT_DK, C_WNUT, 0.25f));
    }
    /* the face */
    disc(*b, 22, 32, 4, 5, C_WHITE); disc(*b, 23, 33, 2, 2, C_NEARBLACK);
    disc(*b, 36, 32, 4, 5, C_WHITE); disc(*b, 35, 33, 2, 2, C_NEARBLACK);
    if (stage >= 2) {                                   /* wide, worried mouth */
        disc(*b, 29, 46, 6, 4, C_NEARBLACK);
        pxb(*b, 26, 45, C_WHITE); pxb(*b, 32, 45, C_WHITE);
        fillRect(*b, 17, 26, 7, 2, C_WNUT_DK);          /* angry brows */
        fillRect(*b, 34, 26, 7, 2, C_WNUT_DK);
    } else {
        fillRect(*b, 24, 44, 10, 2, C_NEARBLACK);
        if (stage == 1) pxb(*b, 24, 47, C_NEARBLACK);   /* the mouth tips down */
    }
    /* damage: a short jagged crack at stage 1, two long ones at stage 2 */
    if (stage >= 1) {
        pxb(*b, 41, 15, C_WNUT_DK); pxb(*b, 40, 16, C_WNUT_DK);
        pxb(*b, 41, 17, C_WNUT_DK); pxb(*b, 40, 18, C_WNUT_DK);
        pxb(*b, 41, 19, C_WNUT_DK);
    }
    if (stage >= 2) {
        pxb(*b, 17, 20, C_WNUT_DK); pxb(*b, 18, 21, C_WNUT_DK);
        pxb(*b, 17, 22, C_WNUT_DK); pxb(*b, 18, 23, C_WNUT_DK);
        pxb(*b, 17, 24, C_WNUT_DK); pxb(*b, 18, 25, C_WNUT_DK);
        pxb(*b, 48, 30, C_WNUT_DK); pxb(*b, 47, 31, C_WNUT_DK);
        pxb(*b, 48, 32, C_WNUT_DK);
    }
    celOutline(*b, 0x5E4212, 1);
    celOutline(*b, 0x2E1F08, 1);
    return b;
}

WBitmap *Atlas::bakeCherry()
{
    WBitmap *b = newMasked(58, 66);
    /* forked stem, leaf and two glossy cherry heads */
    fillRect(*b, 27, 13, 3, 15, C_STEM_DK);
    fillRect(*b, 27, 13, 1, 14, C_STEM_LT);
    fillRect(*b, 28, 22, 11, 3, C_STEM);
    discShade(*b, 16, 42, 13, 14, 0xD8333B, 0xFF7771, 0x8F1827);
    discShade(*b, 40, 42, 13, 14, 0xD8333B, 0xFF7771, 0x8F1827);
    disc(*b, 12, 36, 3, 5, C_WHITE);
    disc(*b, 36, 36, 3, 5, C_WHITE);
    discShade(*b, 36, 17, 8, 4, C_LEAF, C_LEAF_LT, C_LEAF_DK);
    pxb(*b, 42, 18, C_LEAF_DK);
    celOutline(*b, 0x7E1420, 1);
    celOutline(*b, 0x260A0E, 1);
    return b;
}

/* ------------------------------------------------------------------- props */

WBitmap *Atlas::bakePea(int icy)
{
    const unsigned long base = icy ? C_ICE : C_PEA;
    const unsigned long light = icy ? C_ICE_LT : C_PEA_LT;
    const unsigned long dark = icy ? C_ICE_DK : C_PEA_DK;
    WBitmap *b = newMasked(PEA_W, PEA_H);
    discShade(*b, 6, 6, 5, 5, base, light, dark);
    disc(*b, 4, 4, 2, 2, blend(light, 0xFFFFFF, 0.55f));
    celOutline(*b, icy ? 0x1A5574 : 0x356B18, 1);
    return b;
}

/*  A subtractive shadow: a full-black field (black adds nothing on BLIT_
 *  SUBTRACT) with a soft dark ellipse where the shadow is.  The falloff is
 *  four nested rings rather than one flat blob, so the actor looks planted
 *  instead of pasted.  WBox cannot subtract at all (the slot is NULL), so
 *  this has to be a bitmap. */
WBitmap *Atlas::bakeShadow()
{
    WBitmap *b = newMasked(46, 20);
    fillRect(*b, 0, 0, 46, 20, 0x000000);
    disc(*b, 23, 10, 21, 9, 0x141414);
    disc(*b, 23, 10, 16, 7, C_SHADOW);
    disc(*b, 23, 11, 11, 5, 0x161616);
    disc(*b, 23, 11, 6, 3, 0x0A0A0A);
    return b;
}

WBitmap *Atlas::bakeSun()
{
    static const int rx[8] = { 10, 7, 0, -7, -10, -7, 0, 7 };
    static const int ry[8] = {  0, 7, 10, 7,  0, -7, -10, -7 };
    WBitmap *b = newMasked(26, 26);
    for (int i = 0; i < 8; ++i) {                    /* the rays, two tones */
        disc(*b, 13 + rx[i], 13 + ry[i], 4, 4, (i & 1) ? C_SUN2 : C_SUN1);
        pxb(*b, 13 + rx[i], 13 + ry[i] - 3, blend(C_SUN1, 0xFFFFFF, 0.5f));
    }
    glowDisc(*b, 13, 13, 8, 8, C_SUN1, C_SUN2, 5);   /* the hot core */
    pxb(*b, 10, 10, C_WHITE); pxb(*b, 11, 10, C_WHITE);
    pxb(*b, 10, 11, C_WHITE);
    celOutline(*b, 0xE0A000, 1);
    return b;
}

WBitmap *Atlas::bakeBlast(int frame)
{
    WBitmap *b = newMasked(112, 112);
    const int radius = 22 + frame * 10;
    const int cx = 56, cy = 56;
    static const int rx[12] = { 0, 8, 16, 8, 0, -8, -16, -8, 12, -12, 12, -12 };
    static const int ry[12] = { -17, -12, 0, 12, 17, 12, 0, -12, -9, -9, 9, 9 };
    for (int i = 0; i < 12; ++i) {
        const int outer = radius + ((i & 1) ? 4 : 11);
        disc(*b, cx + rx[i] * radius / 20, cy + ry[i] * radius / 20,
             4 + frame, outer / 3, (i & 1) ? 0xF04A24 : 0xFFD34A);
    }
    disc(*b, cx, cy, radius, radius, frame < 2 ? 0xF04A24 : 0xE95A24);
    disc(*b, cx, cy, radius * 3 / 4, radius * 3 / 4,
         frame < 2 ? 0xFFB52E : 0xFF8C29);
    disc(*b, cx - 5, cy - 7, radius / 2, radius / 2,
         frame < 2 ? 0xFFF07A : 0xFFD34A);
    for (int i = 0; i < 8; ++i) {
        const int sign = (i & 1) ? -1 : 1;
        const int px = cx + sign * (18 + i * 3);
        const int py = cy + ((i & 2) ? -1 : 1) * (14 + (i % 3) * 7);
        disc(*b, px, py, 2 + (frame < 2), 3 + (frame & 1),
             (i & 1) ? 0xFFD34A : 0xD83221);
    }
    celOutline(*b, 0x9B2418, 1);
    return b;
}

/*  The pea splat, three frames: fresh, spread, fading. */
WBitmap *Atlas::bakeSplat(int frame)
{
    WBitmap *b = newMasked(18, 18);
    const int cx = 9, cy = 9;
    const int r[3] = { 3, 5, 6 };
    const int rr = r[frame];
    disc(*b, cx, cy, rr, rr - 1, C_PEA_DK);
    disc(*b, cx, cy, rr - 1, rr - 2, C_PEA);
    disc(*b, cx - 1, cy - 1, (rr > 3) ? 2 : 1, (rr > 3) ? 2 : 1,
         blend(C_PEA_LT, 0xFFFFFF, 0.4f));
    /* droplets, shrinking as the stain fades */
    if (frame < 2) {
        pxb(*b, cx - rr - 2, cy - 1, C_PEA_DK); pxb(*b, cx - rr - 3, cy - 1, C_PEA);
        pxb(*b, cx + rr + 2, cy + 1, C_PEA_DK); pxb(*b, cx + rr + 3, cy + 1, C_PEA);
        pxb(*b, cx, cy - rr - 2, C_PEA_DK);
    } else {
        pxb(*b, cx - rr - 1, cy, blend(C_PEA_DK, C_PEA, 0.6f));
        pxb(*b, cx + rr + 1, cy, blend(C_PEA_DK, C_PEA, 0.6f));
    }
    return b;
}

/*  A soft cloud cluster -- overlapping discs with a shaded underside, no
 *  outline, because soft things do not have hard edges. */
WBitmap *Atlas::bakeCloud()
{
    WBitmap *b = newMasked(120, 46);
    discShade(*b, 34, 28, 26, 14, C_CLOUD, C_CLOUD, C_CLOUD_SH);
    discShade(*b, 62, 22, 24, 16, C_CLOUD, C_CLOUD, C_CLOUD_SH);
    discShade(*b, 88, 30, 22, 12, C_CLOUD, C_CLOUD, C_CLOUD_SH);
    fillRect(*b, 22, 40, 76, 3, C_CLOUD_SH);
    return b;
}

/*  A drifting mote for the title screen: a pale seed with a blue shade. */
WBitmap *Atlas::bakeMote()
{
    WBitmap *b = newMasked(5, 5);
    discShade(*b, 2, 2, 2, 2, C_WHITE, C_WHITE, C_CLOUD_SH);
    return b;
}

/*  Copy a baked sprite onto an opaque backdrop, leaving the mask alone. */
static void paste(WBitmap &dst, const WBitmap &src, int x, int y)
{
    const WORD mask = WPixel((DWORD)MASKKEY).color;
    const int sw = src.GetW(), sh = src.GetH();
    const int dw = dst.GetW(), dh = dst.GetH();
    for (int sy = 0; sy < sh; ++sy) {
        const int dy = y + sy;
        if (dy < 0 || dy >= dh) continue;
        for (int sx = 0; sx < sw; ++sx) {
            const int dx = x + sx;
            if (dx < 0 || dx >= dw) continue;
            if (src[sy][sx].color == mask) continue;
            dst[dy][dx] = src[sy][sx];
        }
    }
}

/*  The title backdrop: a graded sky with a low sun and distant hills,
 *  blending into the very same lawn the game uses, so the menu reads as a
 *  garden rather than an empty stadium. */
WBitmap *Atlas::bakeTitleBg()
{
    WBitmap *b = new WBitmap();
    b->Create(FIELD_W, FIELD_H);

    const int hillY = LAWN_Y0 - 14;
    vgrad(*b, 0, 0, FIELD_W, hillY, C_SKY_HI, C_SKY_LO);
    /* distant tree line: two overlapping mounds */
    disc(*b, 90,  hillY + 6, 130, 30, C_HILL_A);
    disc(*b, 90,  hillY + 6, 130, 22, blend(C_HILL_A, 0xFFFFFF, 0.10f));
    disc(*b, 660, hillY + 10, 150, 34, C_HILL_B);
    disc(*b, 660, hillY + 10, 150, 26, blend(C_HILL_B, 0xFFFFFF, 0.08f));
    disc(*b, 380, hillY + 12, 90, 20, C_HILL_B);
    /* a low sun with a halo */
    glowDisc(*b, 706, 66, 30, 30, C_SUN1, C_SUN2, 6);
    glowDisc(*b, 706, 66, 44, 44, blend(C_SUN1, C_SKY_HI, 0.55f),
             blend(C_SUN2, C_SKY_HI, 0.75f), 3);
    /* two baked-in clouds; the third one drifts at render time */
    if (m_cloud) {
        paste(*b, *m_cloud, 120, 40);
        paste(*b, *m_cloud, 420, 74);
    }
    paintLawn(*b, LAWN_Y0, ROWS);
    fillRect(*b, 0, FIELD_H - 3, FIELD_W, 3, C_GRASS_LN);
    return b;
}

/* ---------------------------------------------------------------- zombies
   40 x 88, feet on the bottom row.  A zombie walks LEFT (towards the house on
   the far left), so its face, arms and snout all point left.  Two walk frames
   swap the legs; the eating frame hunches forward and opens the mouth. */
WBitmap *Atlas::bakeZombie(int frame, int eating, int kind, int chilled)
{
    WBitmap *b = newMasked(ZOMBIE_W, ZOMBIE_H);
    const int lean = eating ? 2 : 0;
    const int bob  = (frame & 1) ? 1 : 0;          /* the torso rides 1 px */
    const int legA = (frame & 1) ? 14 : 12;
    const int legB = (frame & 1) ? 25 : 27;

    /* legs: dark trousers with a lit seam, then heavy shoes */
    shadeRect(*b, legA, 60, 9, 24, C_ZCOAT_DK, C_ZCOAT_LT, 0x2A2118, 3);
    shadeRect(*b, legB, 60, 9, 24, C_ZCOAT_DK, C_ZCOAT_LT, 0x2A2118, 3);
    fillRect(*b, legA - 3, 82, 15, 6, C_SHOE);
    fillRect(*b, legB - 3, 82, 15, 6, C_SHOE);
    fillRect(*b, legA - 3, 82, 15, 2, blend(C_SHOE, 0xFFFFFF, 0.18f));
    fillRect(*b, legB - 3, 82, 15, 2, blend(C_SHOE, 0xFFFFFF, 0.18f));

    /* the coat: graded body, collar, a zig-zag torn hem, three buttons */
    shadeRect(*b, 12 + lean, 38 + bob, 22, 26, C_ZCOAT, C_ZCOAT_LT, C_ZCOAT_DK, 4);
    fillRect(*b, 12 + lean, 62 + bob, 22, 2, C_ZCOAT_DK);
    for (int hx = 0; hx < 22; hx += 4)
        fillRect(*b, 12 + lean + hx, 62 + bob + ((hx & 4) ? 2 : 0), 2, 2, C_ZCOAT_DK);
    fillRect(*b, 12 + lean, 38 + bob, 22, 4, C_ZSKIN_SH);
    pxb(*b, 23 + lean, 45 + bob, C_BTN);
    pxb(*b, 23 + lean, 51 + bob, C_BTN);
    pxb(*b, 23 + lean, 57 + bob, C_BTN);

    /* arms forward: shaded sleeve + shaded hand, both hands while eating */
    shadeRect(*b, 2 + lean, 40 + bob, 15, 7, C_SLEEVE, C_SLEEVE_LT, 0x2E2519, 2);
    discShade(*b, 3 + lean, 45 + bob, 4, 5, C_ZSKIN, C_ZSKIN_LT, C_ZSKIN_SH);
    shadeRect(*b, 2 + lean, 50 + bob, 15, 7, C_SLEEVE, C_SLEEVE_LT, 0x2E2519, 2);
    discShade(*b, 3 + lean, 55 + bob, 4, 5, C_ZSKIN, C_ZSKIN_LT, C_ZSKIN_SH);
    if (eating) {
        shadeRect(*b, 1 + lean, 33 + bob, 17, 7, C_SLEEVE, C_SLEEVE_LT, 0x2E2519, 2);
        discShade(*b, 2 + lean, 38 + bob, 4, 5, C_ZSKIN, C_ZSKIN_LT, C_ZSKIN_SH);
    }

    /* head, jaw, ear */
    discShade(*b, 20 + lean, 22, 12, 14, C_ZSKIN, C_ZSKIN_LT, C_ZSKIN_SH);
    discShade(*b, 16 + lean, 28, 7, 6, C_ZSKIN, C_ZSKIN_LT, C_ZSKIN_SH);
    disc(*b, 32 + lean, 23, 3, 4, C_ZSKIN);
    pxb(*b, 32 + lean, 23, C_ZSKIN_SH);
    /* hair: irregular tufts under a heavy brow */
    disc(*b, 20 + lean, 13, 11, 6, C_ZHAIR);
    disc(*b, 14 + lean, 15, 5, 4, C_ZHAIR);
    disc(*b, 26 + lean, 14, 5, 4, C_ZHAIR);
    pxb(*b, 13 + lean, 18, C_ZHAIR);
    pxb(*b, 28 + lean, 18, C_ZHAIR);
    fillRect(*b, 11 + lean, 15, 19, 3, C_ZHAIR);
    /* elite headgear is silhouette-first so it remains readable at game scale */
    if (kind == ZK_CONE) {
        for (int y = 4; y < 18; ++y) {
            const int half = (y - 4) / 2;
            fillRect(*b, 20 - half, y, half * 2 + 1, 1,
                     (y < 7) ? 0xF6A53A : 0xD66B24);
        }
        fillRect(*b, 14, 17, 14, 3, 0x9C431D);
        fillRect(*b, 18, 7, 2, 5, 0xFFD080);
    } else if (kind == ZK_BUCKET) {
        fillRect(*b, 12, 5, 18, 12, 0x9BA9AA);
        fillRect(*b, 10, 15, 22, 3, 0x59686D);
        fillRect(*b, 14, 7, 13, 2, 0xDDE5DF);
        fillRect(*b, 13, 18, 2, 5, 0x747D7C);
    }
    /* eyes: white, pupil aimed the way it walks, a glint pixel */
    disc(*b, 15 + lean, 21, 3, 4, C_ZEYE);
    disc(*b, 24 + lean, 21, 3, 4, C_ZEYE);
    pxb(*b, 14 + lean, 21, C_ZMOUTH);
    pxb(*b, 23 + lean, 21, C_ZMOUTH);
    pxb(*b, 14 + lean, 20, C_ZEYE);
    pxb(*b, 23 + lean, 20, C_ZEYE);
    /* mouth: a line with teeth, or an open chomp with a tongue */
    if (eating) {
        disc(*b, 14 + lean, 29, 5, 5, C_ZMOUTH);
        pxb(*b, 12 + lean, 27, C_WHITE);
        pxb(*b, 16 + lean, 26, C_WHITE);
        disc(*b, 14 + lean, 31, 3, 2, 0x4A2A2A);
    } else {
        fillRect(*b, 12 + lean, 30, 12, 2, C_ZMOUTH);
        pxb(*b, 14 + lean, 29, C_WHITE);
        pxb(*b, 22 + lean, 29, C_WHITE);
    }
    celOutline(*b, 0x22331A, 1);
    celOutline(*b, 0x0C1408, 1);
    if (chilled) {
        /* icy shards on the face and coat; added after the outline so the
           status remains visible even on the dark silhouette. */
        fillRect(*b, 10 + lean, 24, 2, 9, 0x70DFFF);
        fillRect(*b, 8 + lean, 27, 6, 2, 0xB8F3FF);
        fillRect(*b, 22, 42 + bob, 2, 10, 0x70DFFF);
        fillRect(*b, 19, 46 + bob, 8, 2, 0xB8F3FF);
    }
    return b;
}

/* ------------------------------------------------------------- seed packets */

WBitmap *Atlas::bakeSeed(int i)           /* SEED_W x SEED_H */
{
    WBitmap *b = newMasked(SEED_W, SEED_H);
    /* the paper packet: a graded body with a light top edge */
    shadeRect(*b, 2, 2, SEED_W - 4, SEED_H - 4, C_SEEDBG, C_SEED_LT, C_SEED_LO, 6);
    fillRect(*b, 2, 2, SEED_W - 4, 3, blend(C_SEED_LO, 0xFFFFFF, 0.30f));

    /* the packet's picture: the plant, shaded, on a lighter field */
    fillRect(*b, 8, 10, SEED_W - 16, 42, blend(C_SEEDBG, 0xFFFFFF, 0.22f));
    const int kind = i + 1;
    if (kind == PL_PEASHOOTER || kind == PL_SNOWPEA) {
        const unsigned long head = kind == PL_SNOWPEA ? C_ICE : C_PEASH;
        const unsigned long light = kind == PL_SNOWPEA ? C_ICE_LT : C_PEASH_LT;
        const unsigned long dark = kind == PL_SNOWPEA ? C_ICE_DK : C_PEASH_DK;
        discShade(*b, 27, 31, 13, 13, head, light, dark);
        fillRect(*b, 38, 24, 12, 12, dark);
        fillRect(*b, 38, 24, 12, 3, head);
        disc(*b, 48, 30, 5, 8, dark);
        disc(*b, 48, 30, 3, 6, 0x243C18);
        disc(*b, 21, 27, 3, 4, C_WHITE); pxb(*b, 22, 28, C_NEARBLACK);
        disc(*b, 33, 26, 3, 4, C_WHITE); pxb(*b, 34, 27, C_NEARBLACK);
    } else if (kind == PL_SUNFLOWER) {
        static const int sx[8] = { 14, 10, 0, -10, -14, -10, 0, 10 };
        static const int sy[8] = { 0, 11, 15, 11, 0, -11, -15, -11 };
        for (int k = 0; k < 8; ++k)
            discShade(*b, 27 + sx[k], 28 + sy[k], 6, 5,
                      (k & 1) ? C_PETAL : C_PETAL_DK, C_PETAL_LT, C_PETAL_DK);
        disc(*b, 27, 28, 8, 8, C_CENTER);
        disc(*b, 27, 28, 5, 5, C_CENTER_L);
        pxb(*b, 27, 28, C_SEED_DK);
    } else if (kind == PL_CHERRY) {
        fillRect(*b, 27, 16, 2, 15, C_STEM_DK);
        discShade(*b, 18, 36, 11, 12, 0xD8333B, 0xFF7771, 0x8F1827);
        discShade(*b, 39, 36, 11, 12, 0xD8333B, 0xFF7771, 0x8F1827);
        disc(*b, 14, 31, 2, 3, C_WHITE);
        disc(*b, 35, 31, 2, 3, C_WHITE);
        fillRect(*b, 24, 22, 8, 2, C_STEM);
    } else if (kind == PL_WALLNUT) {
        discShade(*b, 28, 31, 16, 17, C_WNUT, C_WNUT_LT, C_WNUT_DK);
        disc(*b, 22, 30, 3, 4, C_WHITE); pxb(*b, 23, 31, C_NEARBLACK);
        disc(*b, 35, 30, 3, 4, C_WHITE); pxb(*b, 34, 31, C_NEARBLACK);
        fillRect(*b, 24, 39, 9, 2, C_NEARBLACK);
    }
    celOutline(*b, 0x5E4212, 1);
    celOutline(*b, 0x2E1F08, 1);

    /* the cost, from the baked ASCII digits, on a dark lozenge badge */
    int n = Lawn::PlantCost(kind);
    char tmp[12], buf[12];
    int t = 0, k = 0;
    if (n <= 0) tmp[t++] = '0';
    while (n > 0) { tmp[t++] = (char)('0' + n % 10); n /= 10; }
    while (t > 0) buf[k++] = tmp[--t];
    buf[k] = 0;
    int tw = 0, w[12];
    for (int j = 0; j < k; ++j) { w[j] = m_asciiW[buf[j] - ASCII_FIRST]; tw += w[j]; }
    const int by = 54;
    roundRect(*b, (SEED_W - 48) / 2, by, 48, 18, 5, C_BADGE);
    fillRect(*b, (SEED_W - 48) / 2 + 3, by + 2, 42, 2,
             blend(C_BADGE, 0xFFFFFF, 0.22f));
    int dx = (SEED_W - tw) / 2, dy = by;
    for (int j = 0; j < k; ++j) {
        stampGlyph(*b, m_ascii[buf[j] - ASCII_FIRST], dx, dy, C_GOLD_HI);
        dx += w[j];
    }
    return b;
}

/* ------------------------------------------------------------------- plaques */

WBitmap *Atlas::bakePlaque(int which)
{
    const int w = (which == 0) ? 520 : 460;
    const int h = (which == 0) ? 96 : 88;
    unsigned long lo, hi, edge;
    if (which == 1)      { lo = C_WIN;  hi = C_WIN_HI;  edge = C_WIN_LO;  }
    else if (which == 2) { lo = C_LOSE; hi = C_LOSE_HI; edge = C_LOSE_LO; }
    else                 { lo = C_GOLD; hi = C_GOLD_HI; edge = C_GOLD_LO; }

    WBitmap *b = newMasked(w, h);
    fillRect(*b, 2, 2, w - 4, h - 4, lo);
    fillRect(*b, 9, 9, w - 18, h - 18, hi);
    fillRect(*b, 9, 9, w - 18, 8, C_WHITE);
    fillRect(*b, 9, h - 17, w - 18, 8, edge);
    /* an inner sheen line and a foot shade, so the plaque reads as beveled */
    fillRect(*b, 14, 14, w - 28, 1, blend(hi, 0xFFFFFF, 0.40f));
    fillRect(*b, 14, 21, w - 28, 2, blend(hi, 0xFFFFFF, 0.18f));
    fillRect(*b, 14, h - 20, w - 28, 1, blend(edge, 0x000000, 0.30f));
    /* corner rivets */
    disc(*b, 17, 17, 2, 2, blend(C_WHITE, lo, 0.15f));
    disc(*b, w - 18, 17, 2, 2, blend(C_WHITE, lo, 0.15f));
    disc(*b, 17, h - 18, 2, 2, blend(C_WHITE, lo, 0.15f));
    disc(*b, w - 18, h - 18, 2, 2, blend(C_WHITE, lo, 0.15f));
    celOutline(*b, edge, 3);
    return b;
}

/* -------------------------------------------------------------- GDI glyphs */

/* A baked label: an ink drop-shadow pass at (+1,+1) then the colour pass at
   the origin, both FONT_NOALPHA (which thresholds coverage instead of trying
   to blend into the magenta field -- the magenta would fringe otherwise). */
static void bakeGlyph(WBitmap *&out, int &outW, WFont &font,
                      const char *s, unsigned long color)
{
    int w = font.GetLength(s);
    if (w < 1) w = 1;
    const int h = font.GetHeight();
    out = new WBitmap();
    out->Create(w + 2, h + 2);
    out->Clear(WPixel((DWORD)MASKKEY));
    out->SetPos(1, 1);
    font.Draw(*out, s, WPixel((DWORD)0x14200F), FONT_NOALPHA);
    out->SetPos(0, 0);
    font.Draw(*out, s, WPixel((DWORD)color), FONT_NOALPHA);
    out->SetPos(0, 0);
    outW = w + 2;
}

bool Atlas::bakeUi()
{
    /* "微软雅黑" in GBK */
    static const char YAHEI[] = "\xCE\xA2\xC8\xED\xD1\xC5\xBA\xDA";

    HFONT hf = CreateFontA(18, 0, 0, 0, FW_BOLD, 0, 0, 0, GB2312_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, YAHEI);
    if (!hf) { m_ok = false; return false; }
    WFont fLabel;
    fLabel.Create(hf, 16, 1024);
    DeleteObject(hf);

    hf = CreateFontA(46, 0, 0, 0, FW_BOLD, 0, 0, 0, GB2312_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, YAHEI);
    if (!hf) { m_ok = false; return false; }
    WFont fBig;
    fBig.Create(hf, 16, 1024);
    DeleteObject(hf);

    for (int i = 0; i < PVZ_LABEL_COUNT; ++i)
        bakeGlyph(m_label[i], m_labelW[i], (i == PVZ_LBL_TITLE) ? fBig : fLabel,
                  PVZ_LABEL[i], 0xF2EEE0);

    /* the printable ASCII range, in the label face */
    for (int c = ASCII_FIRST; c <= ASCII_LAST; ++c) {
        char one[2];
        one[0] = (char)c;
        one[1] = 0;
        bakeGlyph(m_ascii[c - ASCII_FIRST], m_asciiW[c - ASCII_FIRST],
                  fLabel, one, 0xF2EEE0);
    }
    m_ok = true;
    return true;
}

/* ------------------------------------------------------------- label access */

WBitmap *Atlas::Label(int id) const
{
    if (id < 0 || id >= PVZ_LABEL_COUNT) return 0;
    return m_label[id];
}

int Atlas::LabelW(int id) const
{
    if (id < 0 || id >= PVZ_LABEL_COUNT) return 0;
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

} /* namespace pvz */
