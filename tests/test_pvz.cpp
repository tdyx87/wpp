/* ===========================================================================
 *  test_pvz.cpp -- headless tests for the Lawn Defense prototype
 * ---------------------------------------------------------------------------
 *  Two layers:
 *    [A] pvz_art  -- the atlas bakes; determinism; masks; cel outlines.
 *    [B..G] pvz_core -- grid/geometry, sun & planting, the peashooter firing
 *                    cone, a pea killing a zombie, a zombie eating a plant,
 *                    a zombie breaching the house (lose), a full defence
 *                    winning the level, and run-to-run determinism.  Driven
 *                    headless, no window.
 *    [H] pvz_render -- the renderer is driven into an off-screen WCanvas and
 *                    the pixels are read back with Update()+BlitTo().
 *
 *  Assertions are about NUMBERS -- counts, extents, channel dominance -- not
 *  exact colours, because every pixel takes a round trip through RGB565.  The
 *  render/art sections are skipped (not failed) if the GDI face is missing.
 *
 *  ASCII only.
 * ===========================================================================
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"
#include "wsgeo.h"

#include "pvz_core.h"
#include "pvz_art.h"
#include "pvz_render.h"

using namespace pvz;

static int g_pass = 0;
static int g_fail = 0;
static void check(int cond, const char *msg)
{
    if (cond) { ++g_pass; printf("  [PASS] %s\n", msg); }
    else      { ++g_fail; printf("  [FAIL] %s\n", msg); }
}

/* RGB565 channel readback */
static int CHr(WORD c) { return (c >> 11) & 0x1f; }
static int CHg(WORD c) { return (c >> 5) & 0x3f; }
static int CHb(WORD c) { return c & 0x1f; }
static int isGreenish(WORD c) { return CHg(c) > CHr(c) && CHg(c) > CHb(c); }
static int isBlueish(WORD c) { return CHb(c) > CHr(c) && CHg(c) >= CHb(c) - 2; }

/* Readback: a single RGB565 pixel. */
static WORD PX(WBitmap &b, int x, int y) { return b[y][x].color; }

static int countRect(WBitmap &b, int x0, int y0, int w, int h, int (*pred)(WORD))
{
    const int bw = b.GetW(), bh = b.GetH();
    int n = 0;
    for (int y = y0; y < y0 + h; ++y) {
        if (y < 0 || y >= bh) continue;
        for (int x = x0; x < x0 + w; ++x) {
            if (x < 0 || x >= bw) continue;
            if (pred(PX(b, x, y))) ++n;
        }
    }
    return n;
}

/* How many pixels two same-sized sprites disagree on. */
static int spriteDiff(WBitmap *a, WBitmap *b)
{
    if (!a || !b) return -1;
    const int w = a->GetW() < b->GetW() ? a->GetW() : b->GetW();
    const int h = a->GetH() < b->GetH() ? a->GetH() : b->GetH();
    int d = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if ((*a)[y][x].color != (*b)[y][x].color) ++d;
    return d;
}

/* In RGB565 the green channel has an extra bit, so "brown" and "gold" both
   read as green-dominant by channel dominance.  Match the exact baked colour
   with a small tolerance instead: the seed bar and the title plaque are drawn
   crisp (above the dim), so their fills are near-exact. */
static int isNear(WORD a, WORD b, int tol)
{
    int dr = CHr(a) - CHr(b); if (dr < 0) dr = -dr;
    int dg = CHg(a) - CHg(b); if (dg < 0) dg = -dg;
    int db = CHb(a) - CHb(b); if (db < 0) db = -db;
    return dr <= tol && dg <= tol && db <= tol;
}

static Atlas   *g_atlas = 0;
static WCanvas *g_canvas = 0;
static Renderer *g_render = 0;

/* Render a frame with Renderer::Draw and copy the composited canvas out. */
static void shot(const Lawn &l, WBitmap &out)
{
    g_render->Draw(*g_canvas, l, *g_atlas);
    g_canvas->Update();
    out.Create(FIELD_W, FIELD_H);
    g_canvas->BlitTo(out);
    g_canvas->NextFrame();
}

/* ====================================================================== */
/*  [A]  atlas                                                            */
/* ====================================================================== */
static void test_atlas(void)
{
    printf("[A] Atlas / art\n");
    check(g_atlas && g_atlas->Ok(), "the atlas builds (GDI face present)");
    if (!g_atlas || !g_atlas->Ok()) return;

    for (int k = PL_PEASHOOTER; k < PL_COUNT; ++k)
        check(g_atlas->Plant(k) != 0, "plant sprite baked");

    check(g_atlas->Zombie(0, 0) && g_atlas->Zombie(1, 1) &&
          g_atlas->Zombie(0, 0, ZK_CONE) && g_atlas->Zombie(0, 0, ZK_BUCKET, 1),
          "zombie frames and armor variants baked");
    check(g_atlas->Pea() && g_atlas->IcePea() && g_atlas->Sun() &&
          g_atlas->Blast(0) && g_atlas->Blast(3) && g_atlas->Shadow(),
          "props and blast frames baked");
    for (int i = 0; i < PL_COUNT - 1; ++i)
        check(g_atlas->Seed(i) != 0, "seed packet baked");
    check(spriteDiff(g_atlas->Plant(PL_SNOWPEA), g_atlas->Plant(PL_PEASHOOTER)) > 100,
          "snow pea has a distinct ice palette");
    check(g_atlas->Plant(PL_CHERRY) != 0, "cherry bomb baked");
    for (int p = 0; p < 3; ++p)
        check(g_atlas->Plaque(p) != 0, "plaque baked");

    /* LawnSpeckle is deterministic and mostly transparent (leaves row base) */
    int reps = 1, nonz = 0;
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 200; ++x) {
            const unsigned long a = LawnSpeckle(x, y);
            if (a != LawnSpeckle(x, y)) reps = 0;
            if (a) ++nonz;
        }
    const int tot = 100 * 200;
    check(reps == 1, "LawnSpeckle is deterministic");
    check(nonz > tot / 50 && nonz < tot / 2, "LawnSpeckle has a speckle fraction");

    /* a plant sprite lives on the BLIT_MASK field: it must have mask surround
       AND the dark cel-outline colour somewhere */
    WBitmap *ps = g_atlas->Plant(PL_PEASHOOTER);
    if (ps) {
        int maskc = 0, outline = 0, content = 0;
        const WORD mask = WPixel((DWORD)0xFF00FF).color;
        const WORD edge = WPixel((DWORD)0x1E4D14).color;
        for (int y = 0; y < ps->GetH(); ++y)
            for (int x = 0; x < ps->GetW(); ++x) {
                const WORD c = PX(*ps, x, y);
                if (c == mask) ++maskc; else ++content;
                if (c == edge) ++outline;
            }
        check(maskc > 0, "plant sprite keeps the magenta mask surround");
        check(content > 200, "plant sprite has real content");
        check(outline > 20, "plant sprite has the dark cel outline");
    }

    /* the printable ASCII range 32..126 is fully baked */
    int asciiOk = 1;
    for (int c = ASCII_FIRST; c <= ASCII_LAST; ++c)
        if (!g_atlas->Ascii(c)) asciiOk = 0;
    check(asciiOk, "ASCII 32..126 fully baked");
    check(g_atlas->Ascii(31) == 0 && g_atlas->Ascii(127) == 0,
          "Ascii() refuses out-of-range codes");

    /* ---- the shaded-art upgrade ----------------------------------------
       Cel shading is the visible difference between the flat first pass and
       this one: every sprite must now carry many tones, not one fill. */
    if (ps) {
        const WORD base = WPixel((DWORD)0x6BBB46).color;   /* C_PEASH */
        const WORD mask = WPixel((DWORD)0xFF00FF).color;
        int shaded = 0;
        static WORD seen[96];
        int ns = 0;
        for (int y = 0; y < ps->GetH(); ++y)
            for (int x = 0; x < ps->GetW(); ++x) {
                const WORD c = PX(*ps, x, y);
                if (c == mask) continue;
                if (!isNear(c, base, 6)) ++shaded;
                int k = 0;
                for (; k < ns; ++k) if (seen[k] == c) break;
                if (k == ns && ns < 96) seen[ns++] = c;
            }
        check(shaded > 400, "the peashooter is cel-shaded, not flat");
        check(ns >= 8, "the peashooter carries at least 8 distinct tones");
    }

    /* the wall-nut is baked in three damage stages picked by hp */
    WBitmap *w0 = g_atlas->PlantHp(PL_WALLNUT, PLANT_HP_WALLNUT);
    WBitmap *w1 = g_atlas->PlantHp(PL_WALLNUT, PLANT_HP_WALLNUT * 2 / 3);
    WBitmap *w2 = g_atlas->PlantHp(PL_WALLNUT, 1);
    check(w0 && w1 && w2, "the wall-nut has three damage stages");
    check(w0->GetW() == w2->GetW() && w0->GetH() == w2->GetH(),
          "every stage shares the wall-nut's art box");
    check(spriteDiff(w0, w1) > 5 && spriteDiff(w1, w2) > 40,
          "each stage adds visible damage");
    check(g_atlas->PlantHp(PL_PEASHOOTER, 10) == g_atlas->Plant(PL_PEASHOOTER),
          "PlantHp forwards for the plants that do not crack");

    /* the splat is a three-frame stain, not a green square */
    int splatOk = (g_atlas->Splat(0) && g_atlas->Splat(1) && g_atlas->Splat(2));
    check(splatOk, "splat frames baked");
    check(spriteDiff(g_atlas->Splat(0), g_atlas->Splat(2)) > 20,
          "the splat evolves over its lifetime");
    check(spriteDiff(g_atlas->Splat(0), g_atlas->Splat(0)) == 0,
          "baking is deterministic");

    /* the zombie keeps its walk cycle and its chew */
    check(spriteDiff(g_atlas->Zombie(0, 0), g_atlas->Zombie(1, 0)) > 150,
          "the zombie walk frames are distinct");
    check(spriteDiff(g_atlas->Zombie(0, 0), g_atlas->Zombie(0, 1)) > 150,
          "the zombie chew frame is distinct");

    /* the title backdrop: sky overhead, the same lawn below */
    WBitmap *tb = g_atlas->TitleBg();
    check(tb && tb->GetW() == FIELD_W && tb->GetH() == FIELD_H,
          "the title backdrop is baked");
    if (tb) {
        const int sky = countRect(*tb, 0, 0, FIELD_W, 80, isBlueish);
        const int law = countRect(*tb, 0, FIELD_H - 120, FIELD_W, 120, isGreenish);
        check(sky > 40000, "the title backdrop opens on sky");
        check(law > 60000, "the title backdrop sits on the same lawn");
    }
    check(g_atlas->Cloud() && g_atlas->Mote(), "menu weather baked");
}

/* ====================================================================== */
/*  [B]  geometry                                                          */
/* ====================================================================== */
static void test_geometry(void)
{
    printf("[B] grid geometry\n");
    check(FIELD_W == 800 && FIELD_H == 600, "field is 800x600");
    check(ROWS == 5 && COLS == 9, "the lawn is 5 rows x 9 cols");
    check(CellCX(0) == GRID_X0 + COL_W / 2 && CellCX(8) < FIELD_W,
          "cell centres sit inside the grid");
    check(CellCY(0) > LAWN_Y0 && FeetY(ROWS - 1) < FIELD_H,
          "feet line stays inside the field for every row");

    Lawn l;
    l.Reset(7);
    int c = -1, r = -1;
    check(l.CellAt(CellCX(3), CellCY(2), c, r) && c == 3 && r == 2,
          "CellAt maps a cell centre back to (col,row)");
    check(!l.CellAt(4, FeetY(2), c, r), "the house column is not plantable");
    check(!l.CellAt(FIELD_W - 4, 10, c, r), "the seed bar is not plantable");

    /* SeedIdx walks the three packets along the top strip */
    check(l.SeedIdx(SEED_X0 + 3, SEED_Y + 3) == 0, "seed 0 is the first packet");
    check(l.SeedIdx(SEED_X0 + SEED_W + SEED_GAP + 3, SEED_Y + 3) == 1,
          "seed 1 is the second packet");
    check(l.SeedIdx(SEED_X0 + 2 * (SEED_W + SEED_GAP) + 3, SEED_Y + 3) == 2,
          "seed 2 is the third packet");
    check(l.SeedIdx(SEED_X0 - 5, SEED_Y + 3) == -1, "a gap between packets is no seed");
    check(l.SeedIdx(10, 200) == -1, "the lawn body is not a seed");

    check(Lawn::PlantCost(PL_PEASHOOTER) == COST_PEASHOOTER, "peashooter cost");
    check(Lawn::PlantCost(PL_SUNFLOWER) == COST_SUNFLOWER, "sunflower cost");
    check(Lawn::PlantCost(PL_WALLNUT) == COST_WALLNUT, "wall-nut cost");
    check(Lawn::PlantCost(PL_SNOWPEA) == COST_SNOWPEA &&
          Lawn::PlantCost(PL_CHERRY) == COST_CHERRY, "special plant costs");
    check(Lawn::WaveSize(1) == 3 && Lawn::WaveSize(5) == 9 &&
          Lawn::WaveSize(10) == 14, "ten waves have a rising threat curve");
    check(Lawn::ZombieHp(ZK_CONE) > Lawn::ZombieHp(ZK_NORMAL) &&
          Lawn::ZombieHp(ZK_BUCKET) > Lawn::ZombieHp(ZK_CONE), "zombie armor raises hp");
    check(Lawn::PlantW(PL_PEASHOOTER) > 0 && Lawn::PlantH(PL_PEASHOOTER) > 0,
          "plants state an art box for the renderer");
}

/* ====================================================================== */
/*  [C]  sun & planting                                                    */
/* ====================================================================== */
static void test_planting(void)
{
    printf("[C] sun and planting\n");
    Lawn l;
    l.Reset(7);
    l.SetAutoSpawn(0);
    check(l.Sun() == SUN_START, "the level starts with a peashooter's worth of sun");

    l.SetSel(PL_PEASHOOTER);
    check(l.TryPlant(0, 0), "a peashooter plants on an empty affordable cell");
    check(l.PlantAt(0, 0) == PL_PEASHOOTER, "the cell now holds a peashooter");
    check(l.PlantHP(0, 0) == PLANT_HP_PEASHOOTER, "it has full hp");
    check(l.Sun() == SUN_START - COST_PEASHOOTER, "sun paid for the plant");
    check(!l.TryPlant(0, 0), "an occupied cell refuses a second plant");

    /* too little sun fails and changes nothing */
    l.SetSel(PL_SUNFLOWER);                 /* costs 50, sun is now 0-ish */
    l.AddSun(49);                            /* one short of 50 */
    const bool planted = l.TryPlant(1, 1);
    check(!planted, "a plant you cannot afford is refused");
    check(l.PlantAt(1, 1) == PL_NONE, "the refused cell stays empty");
    check(l.Sun() == 49, "the refused plant spent nothing");

    l.SetSel(PL_WALLNUT);                    /* costs 100 */
    l.AddSun(100);
    check(l.TryPlant(8, 4), "a wall-nut plants in the far corner");
    check(l.PlantHP(8, 4) == PLANT_HP_WALLNUT, "the wall-nut has its bigger pool");
    check(Lawn::PlantH(PL_WALLNUT) < Lawn::PlantH(PL_PEASHOOTER),
          "the wall-nut is the shorter sprite");
}

/* ====================================================================== */
/*  [D]  the peashooter firing cone                                        */
/* ====================================================================== */
static void test_firing(void)
{
    printf("[D] peashooter firing\n");
    Lawn l;
    l.Reset(7);
    l.SetAutoSpawn(0);
    l.TestPlant(PL_PEASHOOTER, 0, 2);

    /* no zombie in the row -> no pea, however long we wait */
    l.ClearPeas();
    for (int i = 0; i < 300; ++i) l.Step();
    check(l.PeaCount() == 0, "a peashooter idles with no zombie in its row");

    /* a zombie appears to the RIGHT (ahead of it) -> it fires */
    l.SpawnOne(2);
    l.ClearPeas();
    int fired = 0;
    for (int i = 0; i < 30 && !fired; ++i) { l.Step(); if (l.PeaCount() > 0) fired = 1; }
    check(fired, "a zombie in the row makes the peashooter fire");

    /* a zombie in a DIFFERENT row must not make it fire */
    Lawn l2;
    l2.Reset(7);
    l2.SetAutoSpawn(0);
    l2.TestPlant(PL_PEASHOOTER, 0, 0);
    l2.SpawnOne(4);                          /* same row index != plant row */
    l2.ClearPeas();
    for (int i = 0; i < 60; ++i) l2.Step();
    check(l2.PeaCount() == 0, "a zombie in another row does not trigger a shot");
}

/* ====================================================================== */
/*  [E]  a pea kills a zombie                                              */
/* ====================================================================== */
static void test_pea_kill(void)
{
    printf("[E] pea kills zombie\n");
    Lawn l;
    l.Reset(7);
    l.SetAutoSpawn(0);
    l.TestPlant(PL_PEASHOOTER, 0, 2);
    check(l.SpawnOne(2) >= 0, "a scripted zombie spawned in row 2");
    check(l.ZombieAt(0).hp == ZOMBIE_HP, "the zombie starts at full hp");

    int minHp = ZOMBIE_HP, steps = 0;
    while (l.Alive() > 0 && steps < 6000) {
        l.Step();
        if (l.Alive() > 0 && l.ZombieAt(0).hp < minHp) minHp = l.ZombieAt(0).hp;
        ++steps;
    }
    check(minHp < ZOMBIE_HP, "peas damaged the zombie");
    check(l.Alive() == 0, "the zombie died to the peashooter");
    check(l.ZombiesLeft() == ZOMBIE_TOTAL - 1, "one zombie is counted as killed");
    check(l.Phase() == PHASE_PLAY, "a single kill is not a win (the level runs on)");
    printf("      killed after %d frames\n", steps);
}

/* ====================================================================== */
/*  [F]  a zombie eats a plant                                             */
/* ====================================================================== */
static void test_eat(void)
{
    printf("[F] zombie eats plant\n");
    Lawn l;
    l.Reset(7);
    l.SetAutoSpawn(0);
    const int cc = 5, rr = 3;
    l.TestPlant(PL_WALLNUT, cc, rr);
    l.SpawnOne(rr);

    int minHp = l.PlantHP(cc, rr), steps = 0;
    while (l.PlantAt(cc, rr) != PL_NONE && steps < 6000) {
        l.Step();
        if (l.PlantAt(cc, rr) != PL_NONE && l.PlantHP(cc, rr) < minHp)
            minHp = l.PlantHP(cc, rr);
        ++steps;
    }
    check(minHp < PLANT_HP_WALLNUT, "the zombie chewed the wall-nut");
    check(l.PlantAt(cc, rr) == PL_NONE, "the wall-nut was eaten through");
    check(steps > 400 && steps < 6000, "it took a plausible number of bites");
    printf("      eaten after %d frames\n", steps);
}

/* ====================================================================== */
/*  [G]  a breach loses the level                                         */
/* ====================================================================== */
static void test_lose(void)
{
    printf("[G] house breach loses\n");
    Lawn l;
    l.Reset(7);
    l.SetAutoSpawn(0);
    l.SpawnOne(0);                           /* nothing planted to stop it */
    int steps = 0;
    while (l.Phase() == PHASE_PLAY && steps < 8000) { l.Step(); ++steps; }
    check(l.Phase() == PHASE_LOSE, "an unopposed zombie reaching the house loses");
    printf("      breach at frame %d\n", steps);
}

/* ====================================================================== */
/*  [I] special plants, armored enemies and collectible sun                */
/* ====================================================================== */
static void test_depth(void)
{
    printf("[I] roster depth and drops\n");
    Lawn ice;
    ice.Reset(7);
    ice.SetAutoSpawn(0);
    ice.TestPlant(PL_SNOWPEA, 0, 0);
    ice.SpawnOne(0, ZK_CONE);
    for (int i = 0; i < 3000 && ice.Alive() > 0; ++i) ice.Step();
    check(ice.Kills() > 0, "snow peas can clear an armored zombie");
    check(ice.CollectedSun() == 0 && ice.SunDropCount() > 0,
          "a kill creates an uncollected sun drop");
    const SunDrop &drop = ice.SunDropAt(0);
    check(ice.TryCollectSun(drop.x, drop.y), "clicking a sun drop collects it");
    check(ice.CollectedSun() == SUN_ADD, "collected sun enters the bank");

    Lawn cherry;
    cherry.Reset(7);
    cherry.SetAutoSpawn(0);
    cherry.TestPlant(PL_CHERRY, 8, 2);
    cherry.SpawnOne(2, ZK_BUCKET);
    /* The rightmost cell puts the forced zombie inside the blast footprint
       immediately, so the fuse test stays short and deterministic. */
    for (int i = 0; i < CHERRY_FUSE + 3; ++i) cherry.Step();
    check(cherry.PlantAt(8, 2) == PL_NONE, "cherry bomb consumes itself after the fuse");
    check(cherry.Kills() == 1, "cherry bomb defeats the nearby armored zombie");
    check(cherry.BlastCount() > 0, "cherry bomb leaves an expanding blast");
}

/* ====================================================================== */
/*  [H1] determinism; [H2] a strong defence wins                          */
/* ====================================================================== */
static void test_determinism(void)
{
    printf("[H1] determinism\n");
    Lawn a, b;
    a.Reset(7);
    b.Reset(7);
    for (int i = 0; i < 500; ++i) { a.Step(); b.Step(); }
    check(a.ZombieCount() == b.ZombieCount() && a.Spawned() == b.Spawned() &&
          a.Sun() == b.Sun() && a.ZombiesLeft() == b.ZombiesLeft(),
          "two seed-7 runs stay identical");
    int sameRows = 1;
    for (int i = 0; i < a.ZombieCount() && i < b.ZombieCount(); ++i)
        if (a.ZombieAt(i).row != b.ZombieAt(i).row ||
            (int)a.ZombieAt(i).x != (int)b.ZombieAt(i).x) sameRows = 0;
    check(sameRows, "every zombie matches row and position");
}

static void test_win(void)
{
    printf("[H2] a strong defence wins\n");
    Lawn l;
    l.Reset(7);                              /* auto-spawn ON */
    l.AddSun(5000);
    for (int r = 0; r < ROWS; ++r) {
        for (int cc = 1; cc <= 5; ++cc)
            l.TestPlant((cc & 1) ? PL_SNOWPEA : PL_PEASHOOTER, cc, r);
        l.TestPlant(PL_WALLNUT, 7, r);
        l.TestPlant(PL_CHERRY, 6, r);
    }
    int steps = 0;
    while (l.Phase() == PHASE_PLAY && steps < 30000) { l.Step(); ++steps; }
    check(l.Phase() == PHASE_WIN, "four peashooters per row win the 12-zombie level");
    check(l.Spawned() == ZOMBIE_TOTAL, "all ten waves spawned");
    check(l.ZombiesLeft() == 0, "all waves were cleared");
    printf("      level won at frame %d\n", steps);
}

/* ====================================================================== */
/*  [R]  render read-back                                                  */
/* ====================================================================== */
static int isDark(WORD c)  { int m = CHr(c); if (CHg(c) > m) m = CHg(c); if (CHb(c) > m) m = CHb(c); return m < 18; }

static int countNear(WBitmap &b, int x0, int y0, int w, int h, WORD target, int tol)
{
    int n = 0;
    const int bw = b.GetW(), bh = b.GetH();
    for (int y = y0; y < y0 + h; ++y) {
        if (y < 0 || y >= bh) continue;
        for (int x = x0; x < x0 + w; ++x) {
            if (x < 0 || x >= bw) continue;
            if (isNear(PX(b, x, y), target, tol)) ++n;
        }
    }
    return n;
}

static void shotAtlas(WBitmap &out, int which)
{
    if (which == 0) g_render->DrawTitle(*g_canvas, *g_atlas, 0, 0);
    else            g_render->DrawHowto(*g_canvas, *g_atlas);
    g_canvas->Update();
    out.Create(FIELD_W, FIELD_H);
    g_canvas->BlitTo(out);
    g_canvas->NextFrame();
}

static int bboxDiff(WBitmap &a, WBitmap &b, int x0, int y0, int w, int h)
{
    int diff = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (PX(a, x0 + x, y0 + y) != PX(b, x0 + x, y0 + y)) ++diff;
    return diff;
}

static void test_render(void)
{
    printf("[R] render read-back\n");
    WBitmap empty;
    Lawn l0;
    l0.Reset(7);
    l0.SetAutoSpawn(0);
    shot(l0, empty);

    const int grass = countRect(empty, GRID_X0, LAWN_Y0, COLS * COL_W, ROWS * ROW_H, isGreenish);
    check(grass > 100000, "the lawn reads as green");
    const WORD wood = WPixel((DWORD)0x8A5E33).color;   /* the seed-bar fill */
    const int woodc = countNear(empty, 0, 0, FIELD_W, TOP_H - 4, wood, 4);
    check(woodc > (FIELD_W * (TOP_H - 4)) / 3, "the seed bar reads as flat wood");
    check(g_render->ItemCount() >= 3, "a frame issues draw calls");

    WBitmap plant;
    Lawn l2;
    l2.Reset(7);
    l2.SetAutoSpawn(0);
    l2.TestPlant(PL_PEASHOOTER, 1, 2);
    shot(l2, plant);
    const int pd = bboxDiff(empty, plant, CellCX(1) - Lawn::PlantW(PL_PEASHOOTER) / 2,
                            FeetY(2) - Lawn::PlantH(PL_PEASHOOTER),
                            Lawn::PlantW(PL_PEASHOOTER), Lawn::PlantH(PL_PEASHOOTER));
    check(pd > 150, "the planted peashooter paints over the lawn");

    WBitmap zomb;
    Lawn l3;
    l3.Reset(7);
    l3.SetAutoSpawn(0);
    l3.SpawnOne(0);
    shot(l3, zomb);
    const int zd = bboxDiff(empty, zomb, (int)l3.ZombieAt(0).x - ZOMBIE_W / 2,
                            FeetY(0) - ZOMBIE_H + 2, ZOMBIE_W, ZOMBIE_H);
    check(zd > 150, "a zombie paints over the lawn");

    WBitmap title;
    shotAtlas(title, 0);
    const WORD gold = WPixel((DWORD)0xF2D98A).color;   /* the title plaque fill */
    const int goldc = countNear(title, (FIELD_W - 520) / 2, 132, 520, 96, gold, 5);
    check(goldc > 3000, "the title gold plaque is present");
    const int skytop = countRect(title, 0, 0, FIELD_W, 60, isBlueish);
    check(skytop > 30000, "the title screen shows sky");

    WBitmap howto;
    shotAtlas(howto, 1);
    const int dark = countRect(howto, 60, 88, FIELD_W - 120, FIELD_H - 150, isDark);
    check(dark > (int)((FIELD_W - 120) * (FIELD_H - 150) * 8 / 10),
          "the howto dark panel covers the page");
}

/* ------------------------------------------------------------------ dumps */

static void writePPM(WBitmap &b, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const int w = b.GetW(), h = b.GetH();
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const WORD c = PX(b, x, y);
            fputc(CHr(c) * 255 / 31, f);
            fputc(CHg(c) * 255 / 63, f);
            fputc(CHb(c) * 255 / 31, f);
        }
    fclose(f);
}

/* -------------------------------------------------------------------- main */

int main(void)
{
    printf("pvz -- Lawn Defense headless tests\n");
    printf("===================================\n");

    g_atlas = new Atlas();
    g_atlas->Build();
    g_canvas = new WCanvas(FIELD_W, FIELD_H);
    g_canvas->Reset(60);
    g_render = new Renderer();

    test_atlas();
    test_geometry();
    test_planting();
    test_firing();
    test_pea_kill();
    test_eat();
    test_lose();
    test_depth();
    test_determinism();
    test_win();
    if (g_atlas->Ok()) test_render();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);

    /* PVZ_DUMP=1: write the screens to PPM for a human eye-ball. */
    if (g_atlas->Ok() && getenv("PVZ_DUMP")) {
        WBitmap o;
        Lawn d; d.Reset(7); d.SetAutoSpawn(0);
        shot(d, o); writePPM(o, "pvz_lawn.ppm");

        Lawn d2; d2.Reset(7); d2.SetAutoSpawn(0); d2.AddSun(9999);
        d2.SetSel(PL_PEASHOOTER); d2.TryPlant(1, 2); d2.TryPlant(2, 2);
        d2.SetSel(PL_SUNFLOWER);  d2.TryPlant(4, 3);
        d2.SetSel(PL_WALLNUT);    d2.TryPlant(7, 4);
        d2.SpawnOne(2); d2.SpawnOne(4);
        for (int i = 0; i < 150; ++i) d2.Step();
        shot(d2, o); writePPM(o, "pvz_play.ppm");

        WBitmap t; shotAtlas(t, 0); writePPM(t, "pvz_title.ppm");
        WBitmap hw; shotAtlas(hw, 1); writePPM(hw, "pvz_howto.ppm");

        Lawn d3; d3.Reset(7); d3.SetAutoSpawn(0); d3.SpawnOne(0);
        int guard = 0;
        while (d3.Phase() == PHASE_PLAY && guard < 8000) { d3.Step(); ++guard; }
        shot(d3, o); writePPM(o, "pvz_lose.ppm");
        printf("dumped pvz_{lawn,play,title,howto,lose}.ppm\n");
    }

    delete g_render;
    delete g_canvas;
    delete g_atlas;
    return g_fail ? 1 : 0;
}

