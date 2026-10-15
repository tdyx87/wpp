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
    /* top bar / wood */
    C_WOOD_HI  = 0xC89B6A, C_WOOD     = 0x8A5E33, C_WOOD_LO = 0x6B4423,
    C_WOOD_LN  = 0x4E3016,
    /* house */
    C_WALL = 0xE8DCC0, C_WALL_SH = 0xC2B48E, C_ROOF = 0xC4453A,
    C_ROOF_SH = 0x8E2C22, C_DOOR = 0x6D4C41, C_WINDOW = 0x9AD9E8, C_BRICK = 0xB8A784,
    /* peashooter */
    C_PEASH = 0x6BBB46, C_PEASH_DK = 0x3F8B2E, C_STEM = 0x4E9C39, C_LEAF = 0x57A83C,
    /* sunflower */
    C_PETAL = 0xF7D13B, C_PETAL_DK = 0xF2B824, C_CENTER = 0x8A5524, C_CENTER_L = 0xA9702F,
    /* wall-nut */
    C_WNUT = 0xC08B3E, C_WNUT_LT = 0xDCAA5C,
    /* zombie */
    C_ZSKIN = 0xB0BE78, C_ZSKIN_SH = 0x8E9A5C, C_ZCOAT = 0x5A4A38, C_ZCOAT_DK = 0x3E3226,
    C_ZHAIR = 0x2E2620, C_ZEYE = 0xEDEADF, C_ZMOUTH = 0x2A1A1A,
    /* props */
    C_PEA = 0x7FC24A, C_PEA_LT = 0xA6DA6A,
    C_SUN1 = 0xFFE24A, C_SUN2 = 0xF6C02A,
    C_SHADOW = 0x242424,
    /* ui */
    C_SEEDBG = 0xE9C77B, C_SEED_LO = 0xB08A48,
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

/* ------------------------------------------------------------------- atlas */

Atlas::Atlas() : m_ok(false)
{
    m_bg = m_house = m_pea = m_shadow = m_sun = 0;
    for (int i = 0; i < PL_COUNT; ++i) m_plant[i] = 0;
    for (int f = 0; f < ZOMBIE_FRAMES; ++f)
        for (int e = 0; e < ZOMBIE_EATS; ++e) m_zombie[f][e] = 0;
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
    delete m_pea;     m_pea = 0;
    delete m_shadow;  m_shadow = 0;
    delete m_sun;     m_sun = 0;
    for (int i = 0; i < PL_COUNT; ++i) { delete m_plant[i]; m_plant[i] = 0; }
    for (int f = 0; f < ZOMBIE_FRAMES; ++f)
        for (int e = 0; e < ZOMBIE_EATS; ++e) { delete m_zombie[f][e]; m_zombie[f][e] = 0; }
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

WBitmap *Atlas::Zombie(int frame, int eating) const
{
    if (frame < 0) frame = 0;
    if (frame >= ZOMBIE_FRAMES) frame = ZOMBIE_FRAMES - 1;
    if (eating) frame = frame;                 /* eat reuses a walk frame */
    return m_zombie[frame][eating ? 1 : 0];
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

    m_bg     = bakeBackground();
    m_house  = bakeHouse();
    m_plant[PL_PEASHOOTER] = bakePeashooter();
    m_plant[PL_SUNFLOWER]  = bakeSunflower();
    m_plant[PL_WALLNUT]    = bakeWallnut();
    for (int f = 0; f < ZOMBIE_FRAMES; ++f)
        for (int e = 0; e < ZOMBIE_EATS; ++e)
            m_zombie[f][e] = bakeZombie(f, e);
    m_pea    = bakePea();
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

    /* the wooden seed bar */
    fillRect(*b, 0, 0, FIELD_W, TOP_H - 4, C_WOOD);
    fillRect(*b, 0, 0, FIELD_W, 6, C_WOOD_HI);
    fillRect(*b, 0, TOP_H - 8, FIELD_W, 4, C_WOOD_LO);
    fillRect(*b, 0, TOP_H - 4, FIELD_W, 4, C_WOOD_LN);
    for (int x = 40; x < FIELD_W; x += 80)
        fillRect(*b, x, 6, 2, TOP_H - 14, C_WOOD_LO);

    /* the lawn: two greens alternating by row */
    for (int r = 0; r < ROWS; ++r) {
        const unsigned long base = (r & 1) ? C_GRASS_B : C_GRASS_A;
        fillRect(*b, 0, LAWN_Y0 + r * ROW_H, FIELD_W, ROW_H, base);
    }
    /* speckle texture */
    for (int y = LAWN_Y0; y < LAWN_Y0 + ROWS * ROW_H; ++y)
        for (int x = 0; x < FIELD_W; ++x) {
            const unsigned long s = LawnSpeckle(x, y);
            if (s) (*b)[y][x] = mk(s);
        }
    /* row + column guides make the cells legible for placement */
    for (int r = 1; r < ROWS; ++r)
        fillRect(*b, 0, LAWN_Y0 + r * ROW_H, FIELD_W, 1, C_GRASS_LN);
    for (int c = 1; c < COLS; ++c)
        fillRect(*b, GRID_X0 + c * COL_W - 1, LAWN_Y0, 1, ROWS * ROW_H, C_GRASS_LN);
    fillRect(*b, 0, FIELD_H - 3, FIELD_W, 3, C_GRASS_LN);
    return b;
}

WBitmap *Atlas::bakeHouse()
{
    WBitmap *b = new WBitmap();
    b->Create(HOUSE_W, FIELD_H);
    fillRect(*b, 0, 0, HOUSE_W, FIELD_H, C_WALL);
    fillRect(*b, HOUSE_W - 8, 0, 8, FIELD_H, C_WALL_SH);
    for (int y = 16; y < FIELD_H; y += 24)
        fillRect(*b, 0, y, HOUSE_W - 8, 1, C_BRICK);
    for (int y = 0; y < 52; ++y)
        fillRect(*b, 0, y, HOUSE_W, 1, C_ROOF);
    fillRect(*b, 0, 52, HOUSE_W, 6, C_ROOF_SH);
    /* one window and a door for character */
    fillRect(*b, 14, 150, 34, 30, C_WINDOW);
    fillRect(*b, 14, 150, 34, 3, C_DOOR);
    fillRect(*b, 14, 177, 34, 3, C_DOOR);
    fillRect(*b, 18, 380, 30, 130, C_DOOR);
    disc(*b, 43, 445, 3, 3, C_GOLD);
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

WBitmap *Atlas::bakePeashooter()          /* 64 x 80 */
{
    WBitmap *b = newMasked(64, 80);
    disc(*b, 14, 66, 16, 8, C_LEAF);
    disc(*b, 50, 66, 16, 8, C_LEAF);
    fillRect(*b, 26, 44, 7, 36, C_STEM);
    disc(*b, 29, 36, 22, 22, C_PEASH);          /* the head */
    disc(*b, 33, 42, 15, 11, C_PEASH_DK);
    fillRect(*b, 42, 18, 16, 22, C_PEASH_DK);   /* the snout */
    disc(*b, 58, 29, 7, 12, C_PEASH_DK);
    disc(*b, 59, 29, 4, 8, 0x243C18);           /* the barrel opening */
    disc(*b, 20, 27, 5, 6, C_WHITE);  disc(*b, 21, 28, 2, 3, C_NEARBLACK);
    disc(*b, 33, 25, 5, 6, C_WHITE);  disc(*b, 32, 26, 2, 3, C_NEARBLACK);
    celOutline(*b, 0x1E4D14, 2);
    return b;
}

WBitmap *Atlas::bakeSunflower()           /* 60 x 76 */
{
    static const int px[12] = { 21, 18, 10, 0, -10, -18, -21, -18, -10, 0, 10, 18 };
    static const int py[12] = {  0, 10, 16, 21, 16,  10,   0, -10, -16, -21, -16, -10 };
    WBitmap *b = newMasked(60, 76);
    for (int i = 0; i < 12; ++i)
        disc(*b, 30 + px[i], 28 + py[i], 8, 7, (i & 1) ? C_PETAL : C_PETAL_DK);
    disc(*b, 30, 28, 12, 12, C_CENTER);
    disc(*b, 30, 28, 9, 9, C_CENTER_L);
    fillRect(*b, 27, 40, 6, 36, C_STEM);
    disc(*b, 13, 64, 15, 8, C_LEAF);
    disc(*b, 47, 64, 15, 8, C_LEAF);
    disc(*b, 24, 26, 3, 4, C_WHITE); disc(*b, 25, 27, 1, 2, C_NEARBLACK);
    disc(*b, 36, 26, 3, 4, C_WHITE); disc(*b, 35, 27, 1, 2, C_NEARBLACK);
    fillRect(*b, 24, 33, 12, 2, C_NEARBLACK);   /* a small smile */
    celOutline(*b, 0x6B4A12, 2);
    return b;
}

WBitmap *Atlas::bakeWallnut()             /* 58 x 66 */
{
    WBitmap *b = newMasked(58, 66);
    disc(*b, 29, 36, 24, 28, C_WNUT);
    disc(*b, 24, 26, 15, 15, C_WNUT_LT);
    disc(*b, 22, 32, 4, 5, C_WHITE); disc(*b, 23, 33, 2, 2, C_NEARBLACK);
    disc(*b, 36, 32, 4, 5, C_WHITE); disc(*b, 35, 33, 2, 2, C_NEARBLACK);
    fillRect(*b, 24, 44, 10, 2, C_NEARBLACK);
    celOutline(*b, 0x5E4212, 2);
    return b;
}

/* ------------------------------------------------------------------- props */

WBitmap *Atlas::bakePea()
{
    WBitmap *b = newMasked(PEA_W, PEA_H);
    disc(*b, 6, 6, 5, 5, C_PEA);
    disc(*b, 4, 4, 2, 2, C_PEA_LT);
    celOutline(*b, 0x356B18, 1);
    return b;
}

/*  A subtractive shadow: a full-black field (black adds nothing on BLIT_
 *  SUBTRACT) with a dark ellipse where the shadow is.  WBox cannot subtract
 *  at all (the slot is NULL), so this has to be a bitmap. */
WBitmap *Atlas::bakeShadow()
{
    WBitmap *b = newMasked(44, 18);
    fillRect(*b, 0, 0, 44, 18, 0x000000);
    disc(*b, 22, 9, 20, 8, C_SHADOW);
    disc(*b, 22, 10, 13, 4, 0x161616);
    return b;
}

WBitmap *Atlas::bakeSun()
{
    static const int rx[8] = { 10, 7, 0, -7, -10, -7, 0, 7 };
    static const int ry[8] = {  0, 7, 10, 7,  0, -7, -10, -7 };
    WBitmap *b = newMasked(24, 24);
    for (int i = 0; i < 8; ++i)
        disc(*b, 12 + rx[i], 12 + ry[i], 4, 4, C_SUN2);
    disc(*b, 12, 12, 8, 8, C_SUN1);
    disc(*b, 9, 9, 3, 3, C_WHITE);
    celOutline(*b, 0xE0A000, 1);
    return b;
}

/* ---------------------------------------------------------------- zombies
   40 x 88, feet on the bottom row.  A zombie walks LEFT (towards the house on
   the far left), so its face, arms and snout all point left.  Two walk frames
   swap the legs; the eating frame hunches forward and opens the mouth. */
WBitmap *Atlas::bakeZombie(int frame, int eating)  /* 40 x 88 */
{
    WBitmap *b = newMasked(ZOMBIE_W, ZOMBIE_H);
    const int lean = eating ? 2 : 0;
    const int legA = (frame & 1) ? 14 : 12;
    const int legB = (frame & 1) ? 25 : 27;

    fillRect(*b, legA, 60, 9, 28, C_ZCOAT_DK);
    fillRect(*b, legB, 60, 9, 28, C_ZCOAT_DK);
    fillRect(*b, legA - 3, 84, 15, 4, 0x241C14);
    fillRect(*b, legB - 3, 84, 15, 4, 0x241C14);

    fillRect(*b, 12 + lean, 38, 22, 26, C_ZCOAT);          /* the coat */
    fillRect(*b, 12 + lean, 60, 22, 4, C_ZCOAT_DK);        /* torn hem */
    fillRect(*b, 12 + lean, 38, 22, 4, C_ZSKIN_SH);        /* collar */

    fillRect(*b, 2 + lean, 40, 15, 7, C_ZSKIN);            /* arms forward */
    disc(*b, 3 + lean, 44, 4, 5, C_ZSKIN);
    fillRect(*b, 2 + lean, 50, 15, 7, C_ZSKIN);
    disc(*b, 3 + lean, 54, 4, 5, C_ZSKIN);
    if (eating) fillRect(*b, 1 + lean, 34, 16, 7, C_ZSKIN);

    disc(*b, 20 + lean, 22, 12, 14, C_ZSKIN);              /* head */
    disc(*b, 16 + lean, 28, 7, 6, C_ZSKIN_SH);             /* jaw */
    disc(*b, 20 + lean, 13, 11, 6, C_ZHAIR);               /* hair */
    fillRect(*b, 11 + lean, 15, 19, 3, C_ZHAIR);           /* heavy brow */
    disc(*b, 15 + lean, 21, 3, 4, C_ZEYE);
    disc(*b, 24 + lean, 21, 3, 4, C_ZEYE);
    if (eating) {
        disc(*b, 14 + lean, 29, 5, 5, C_ZMOUTH);           /* open chomp */
        pxb(*b, 12 + lean, 27, C_WHITE);
    } else {
        fillRect(*b, 12 + lean, 30, 12, 2, C_ZMOUTH);
    }
    celOutline(*b, 0x22331A, 2);
    return b;
}

/* ------------------------------------------------------------- seed packets */

WBitmap *Atlas::bakeSeed(int i)           /* SEED_W x SEED_H */
{
    WBitmap *b = newMasked(SEED_W, SEED_H);
    fillRect(*b, 2, 2, SEED_W - 4, SEED_H - 4, C_SEEDBG);
    fillRect(*b, 2, 2, SEED_W - 4, 6, C_SEED_LO);

    const int kind = i + 1;
    if (kind == PL_PEASHOOTER) {
        disc(*b, 27, 34, 14, 14, C_PEASH);
        fillRect(*b, 39, 27, 12, 13, C_PEASH_DK);
        disc(*b, 49, 33, 5, 8, C_PEASH_DK);
        disc(*b, 21, 30, 3, 4, C_WHITE);
        disc(*b, 33, 29, 3, 4, C_WHITE);
    } else if (kind == PL_SUNFLOWER) {
        static const int sx[8] = { 14, 10, 0, -10, -14, -10, 0, 10 };
        static const int sy[8] = { 0, 11, 15, 11, 0, -11, -15, -11 };
        for (int k = 0; k < 8; ++k)
            disc(*b, 27 + sx[k], 31 + sy[k], 6, 5, (k & 1) ? C_PETAL : C_PETAL_DK);
        disc(*b, 27, 31, 8, 8, C_CENTER);
    } else {
        disc(*b, 28, 34, 17, 20, C_WNUT);
        disc(*b, 24, 27, 10, 10, C_WNUT_LT);
        disc(*b, 22, 33, 3, 4, C_WHITE);
        disc(*b, 35, 33, 3, 4, C_WHITE);
    }
    celOutline(*b, 0x5E4212, 2);

    /* cost, from the baked ASCII digits */
    int n = Lawn::PlantCost(kind);
    char tmp[12], buf[12];
    int t = 0, k = 0;
    if (n <= 0) tmp[t++] = '0';
    while (n > 0) { tmp[t++] = (char)('0' + n % 10); n /= 10; }
    while (t > 0) buf[k++] = tmp[--t];
    buf[k] = 0;
    int tw = 0, w[12];
    for (int j = 0; j < k; ++j) { w[j] = m_asciiW[buf[j] - ASCII_FIRST]; tw += w[j]; }
    int dx = (SEED_W - tw) / 2, dy = 52;
    for (int j = 0; j < k; ++j) {
        stampGlyph(*b, m_ascii[buf[j] - ASCII_FIRST], dx, dy, C_NEARBLACK);
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
