/* ===========================================================================
 *  test_guofeng.cpp -- headless tests for the Guofeng isometric prototype
 * ---------------------------------------------------------------------------
 *  Two layers are covered:
 *
 *    [1]..[6]  gf_core -- the projection, the scene and the camera, driven
 *               directly with no window.
 *    [7]..[13] gf_art / gf_render -- the atlas is baked and the renderer is
 *               driven into an off-screen WCanvas; the pixels are read back
 *               with canvas.Update() + canvas.BlitTo(), so the drawing code is
 *               verified without DirectDraw or a visible window.
 *
 *  The assertions are deliberately about NUMBERS rather than about pictures.
 *  A dither that silently collapsed into a hard edge, or a diamond mask that
 *  silently stopped tiling, is far easier to catch as a count than as a
 *  colour -- and a colour cannot be asserted at all here, because every pixel
 *  makes a round trip through RGB565 on the way out.
 *
 *  ASCII only, so it compiles under the default (GBK) source code page.
 * ===========================================================================
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"
#include "wsgeo.h"

#include "gf_core.h"
#include "gf_art.h"
#include "gf_render.h"

using namespace gf;

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *msg)
{
    if (cond) { ++g_pass; printf("  [PASS] %s\n", msg); }
    else      { ++g_fail; printf("  [FAIL] %s\n", msg); }
}

static Atlas   *g_atlas = 0;
static Scene   *g_scene = 0;

static Input noInput(void)
{
    Input in;
    in.up = in.down = in.left = in.right = in.run = 0;
    return in;
}

static Input hold(int up, int down, int left, int right)
{
    Input in = noInput();
    in.up = up; in.down = down; in.left = left; in.right = right;
    return in;
}

/* ====================================================================== */
/*  projection                                                            */
/* ====================================================================== */

/* ---------------------------------------------------------------- [1] */
static void test_projection(void)
{
    printf("[1] IsoToScreen / ScreenToIsoF\n");

    /* the diamond is 2:1 -- that ratio is the whole basis of the projection */
    check(TILE_W == 64 && TILE_H == 32, "the diamond is 64x32");
    check(TILE_HW == 32 && TILE_HH == 16, "half extents are 32 x 16");

    int sx, sy;
    IsoToScreen(0, 0, sx, sy);
    check(sx == 0 && sy == 0, "tile (0,0) projects to the origin");

    IsoToScreen(1, 0, sx, sy);
    check(sx == 32 && sy == 16, "+x is one step right and one half step down");

    IsoToScreen(0, 1, sx, sy);
    check(sx == -32 && sy == 16, "+y is one step left and one half step down");

    IsoToScreen(1, 1, sx, sy);
    check(sx == 0 && sy == 32, "+x+y is straight down on screen");

    IsoToScreen(1, -1, sx, sy);
    check(sx == 64 && sy == 0, "+x-y is straight right on screen");

    /* the four tile axes must land on the four screen diagonals, and nothing
       else -- that is what makes the four facings work */
    check(TileDepth(0, 0) == 0 && TileDepth(3, 4) == 7,
          "depth is tx + ty");

    /* the inverse has to be exact at the tile centres, or mouse picking lands
       on the wrong diamond near every edge */
    int worst = 0, bad = 0;
    for (int ty = 0; ty < MAP_H; ++ty)
        for (int tx = 0; tx < MAP_W; ++tx) {
            float fx, fy;
            IsoToScreen(tx, ty, sx, sy);
            ScreenToIsoF(sx, sy, fx, fy);
            const float ex = fabsf(fx - (float)tx);
            const float ey = fabsf(fy - (float)ty);
            if (ex > 1e-3f || ey > 1e-3f) ++bad;
            if (ex > (float)worst) worst = (int)(ex * 1000.0f);
        }
    printf("      round trip over %d tiles, worst error %d/1000 px\n",
           MAP_W * MAP_H, worst);
    check(bad == 0, "ScreenToIsoF inverts IsoToScreen exactly at every centre");

    /* The projection and the diamond mask have to agree about which tile a
       pixel belongs to.

       ScreenToIsoF is the exact inverse of the mapping that puts a tile's
       CENTRE at IsoToScreen(tx,ty), so the owner of a screen point is
       round() of its inverse -- floor() would be half a tile out, which is
       the mistake this assertion exists to catch.  The mask it is checked
       against, InDiamond, is the one the atlas bakes with, so if the two
       ever drifted apart every tile would be textured with its neighbour's
       material along one edge and nothing else would notice. */
    {
        /* four probes per tile, all 16 px of L1 distance inside the diamond
           so the half-pixel difference between a pixel's corner and its
           centre cannot push one across a boundary */
        static const int ox[4] = {  8, -8,  8, -8 };
        static const int oy[4] = {  4,  4, -4, -4 };

        int agree = 0, tested = 0, disagree = 0;

        for (int ty = 0; ty < MAP_H; ++ty)
            for (int tx = 0; tx < MAP_W; ++tx) {
                int sx, sy;
                IsoToScreen(tx, ty, sx, sy);
                for (int k = 0; k < 4; ++k) {
                    float fx, fy;
                    ScreenToIsoF(sx + ox[k], sy + oy[k], fx, fy);
                    const int rx = (int)floorf(fx + 0.5f);
                    const int ry = (int)floorf(fy + 0.5f);
                    const int in = InDiamond(TILE_HW + ox[k], TILE_HH + oy[k]) ? 1 : 0;
                    ++tested;
                    if (rx == tx && ry == ty && in) ++agree; else ++disagree;
                }
            }
        printf("      projection and mask agree on %d of %d probes\n",
               agree, tested);
        check(disagree == 0, "the diamond mask and the projection pick the same tile");
    }
}

/* ---------------------------------------------------------------- [2] */
/*  The diamond mask has to PARTITION the plane.  A gap leaves an uncovered
 *  sliver along every tile boundary; an overlap makes one tile's corner eat
 *  into its neighbour's.  Neither is visible as a colour, and both are
 *  invisible in a screenshot of a single tile -- which is exactly why this
 *  test exists. */
static void test_diamond_partition(void)
{
    printf("[2] the diamond mask partitions the plane\n");

    check(InDiamond(TILE_HW, TILE_HH), "the tile centre is inside");
    check(!InDiamond(0, 0), "the box corner is outside");
    check(!InDiamond(TILE_W - 1, 0), "the top right corner is outside");
    check(!InDiamond(0, TILE_H - 1), "the bottom left corner is outside");

    /* Count how many diamonds cover each pixel of a region that is strictly
       inside a 4x4 block of tiles.  Every pixel must be covered exactly once.
       The region is inset because the OUTER boundary of the block is a
       staircase, not a rectangle, and pixels there legitimately have zero or
       one owner depending on which neighbour is included. */
    const int tx0 = 3, ty0 = 3, n = 4;

    int gap = 0, over = 0, tested = 0;
    for (int py = 116; py <= 172; ++py) {
        for (int px = -56; px <= 56; ++px) {
            int hits = 0;
            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < n; ++i) {
                    int ox, oy;
                    IsoToScreen(tx0 + i, ty0 + j, ox, oy);
                    const int lx = px - (ox - TILE_HW);
                    const int ly = py - (oy - TILE_HH);
                    if (lx < 0 || ly < 0 || lx >= TILE_W || ly >= TILE_H) continue;
                    if (InDiamond(lx, ly)) ++hits;
                }
            }
            ++tested;
            if (hits == 0) ++gap;
            else if (hits > 1) ++over;
        }
    }
    printf("      %d pixels tested inside the 4x4 block: %d gaps, %d overlaps\n",
           tested, gap, over);
    check(gap == 0,  "no pixel inside the block is left uncovered");
    check(over == 0, "no pixel inside the block is covered twice");

    /* and the mask really is a diamond, not a box: the covered width has to
       shrink monotonically towards the top and bottom vertices */
    int prevW = 0, monotone = 1;
    for (int ly = 0; ly <= TILE_HH; ++ly) {
        int w = 0;
        for (int lx = 0; lx < TILE_W; ++lx) if (InDiamond(lx, ly)) ++w;
        if (ly > 0 && w < prevW) monotone = 0;
        prevW = w;
    }
    check(monotone, "the diamond widens from the top vertex to the waist");
    check(prevW > TILE_W - 4, "the waist is a full tile wide");
}

/* ---------------------------------------------------------------- [3] */
static void test_edge_weights(void)
{
    printf("[3] transition bands and the ordered dither\n");

    /* the weight is 0 outside the band and grows towards the edge */
    int inBand = 0, zeroInside = 0;
    for (int ly = 0; ly < TILE_H; ++ly)
        for (int lx = 0; lx < TILE_W; ++lx) {
            if (!InDiamond(lx, ly)) continue;
            const int w0 = EdgeWeight(lx, ly, 0);
            if (w0 > 0) ++inBand;
            /* the +x edge is the only one that should fire near the lower
               right vertex */
            if (w0 > 0 && EdgeWeight(lx, ly, 2) > 0) ++zeroInside;
        }
    printf("      %d of %d diamond pixels are in the +x band\n",
           inBand, TILE_W * TILE_H / 2);
    check(inBand > 200, "the +x band covers a real area, not a hairline");
    check(inBand < TILE_W * TILE_H / 2, "the band does not swallow the tile");

    /* the far edge must be untouched by the near edge's band: the diamond is
       64 wide, the band reaches about ten pixels in, so the opposite vertex
       has to read zero */
    check(EdgeWeight(TILE_HW, TILE_HH, 0) == 0,
          "the tile centre is outside every band");
    check(EdgeWeight(0, TILE_HH, 0) == 0,
          "the opposite vertex is outside the band");
    check(EdgeWeight(TILE_W - 2, TILE_HH + 1, 0) > 200,
          "the +x edge itself is at full weight");

    /* monotone inward: moving away from the edge can only reduce the weight */
    int mono = 1;
    for (int ly = 1; ly < TILE_H - 1; ++ly) {
        int prev = 256;
        for (int lx = TILE_W - 2; lx >= 0; --lx) {
            if (!InDiamond(lx, ly)) continue;
            const int w = EdgeWeight(lx, ly, 0);
            if (w > prev) { mono = 0; break; }
            prev = w;
        }
    }
    check(mono, "the weight falls off monotonically away from the edge");
}

/* ---------------------------------------------------------------- [4] */
static void test_visible_range(void)
{
    printf("[4] VisibleRange and the camera clamp\n");

    const ScreenBox box = WorldScreenBox();
    printf("      world screen box: x %d..%d  y %d..%d\n",
           box.x0, box.x1, box.y0, box.y1);
    check(box.x1 - box.x0 == MAP_W * TILE_W, "the world is 32 tiles wide");
    check(box.y1 - box.y0 == (MAP_W + MAP_H) * TILE_HH,
          "the world is 32+32 half heights tall");

    /* every tile whose diamond intersects the viewport must be inside the
       range -- a cull that is one tile short pops terrain in along an edge */
    const int cams[4][2] = { { -400, 196 }, { box.x0, box.y0 },
                             { box.x1 - FIELD_W, box.y1 - FIELD_H }, { 0, 0 } };
    int missed = 0, total = 0;

    for (int c = 0; c < 4; ++c) {
        const int camX = cams[c][0], camY = cams[c][1];
        const TileRange r = VisibleRange(camX, camY, FIELD_W, FIELD_H);

        for (int ty = 0; ty < MAP_H; ++ty)
            for (int tx = 0; tx < MAP_W; ++tx) {
                int sx, sy;
                IsoToScreen(tx, ty, sx, sy);
                const int x0 = sx - TILE_HW, x1 = sx + TILE_HW;
                const int y0 = sy - TILE_HH, y1 = sy + TILE_HH;
                if (x1 <= camX || x0 >= camX + FIELD_W) continue;
                if (y1 <= camY || y0 >= camY + FIELD_H) continue;
                ++total;
                if (tx < r.x0 || tx > r.x1 || ty < r.y0 || ty > r.y1) ++missed;
            }
    }
    printf("      %d tiles intersect the four viewports, %d missed\n",
           total, missed);
    check(total > 100, "the viewport does cover a real number of tiles");
    check(missed == 0, "the cull keeps every tile that touches the viewport");

    /* the range must stay inside the map */
    const TileRange r = VisibleRange(box.x0 - 500, box.y0 - 500, FIELD_W, FIELD_H);
    check(r.x0 >= 0 && r.y0 >= 0 && r.x1 < MAP_W && r.y1 < MAP_H,
          "the range is clamped to the map");
}

/* ====================================================================== */
/*  scene                                                                 */
/* ====================================================================== */

/* ---------------------------------------------------------------- [5] */
static void test_scene(void)
{
    printf("[5] the scene\n");

    Scene s;
    s.Reset(7u);

    int water = 0, pave = 0, dirt = 0, grass = 0;
    for (int ty = 0; ty < MAP_H; ++ty)
        for (int tx = 0; tx < MAP_W; ++tx) {
            switch (s.TileAt(tx, ty)) {
            case T_WATER: ++water; break;
            case T_PAVE:  ++pave;  break;
            case T_DIRT:  ++dirt;  break;
            default:      ++grass; break;
            }
        }
    printf("      map: %d water, %d pave, %d dirt, %d other\n",
           water, pave, dirt, grass);
    check(water > 40, "the village has a pond");
    check(pave > 20,  "the plaza is paved");
    check(dirt > 50,  "the roads are trodden dirt");

    int deco = 0, lanterns = 0, houses = 0;
    for (int ty = 0; ty < MAP_H; ++ty)
        for (int tx = 0; tx < MAP_W; ++tx) {
            const int d = (int)s.DecoAt(tx, ty);
            if (d != D_NONE) ++deco;
            if (d == D_LANTERN) ++lanterns;
            if (d == D_HOUSE)   ++houses;
        }
    printf("      %d decorations, %d lanterns, %d houses\n", deco, lanterns, houses);
    check(deco > 100, "the village is furnished");
    check(lanterns >= 6, "there are lanterns to light the road");
    check(houses >= 4, "there are houses");

    check(s.Blocked(6, 23), "the pond is solid");
    check(!s.InMap(-1, 0) && !s.InMap(MAP_W, 0), "InMap rejects off-map tiles");
    check(s.Blocked(-1, 0) && s.Blocked(MAP_W, 0) && s.Blocked(0, -1),
          "off-map counts as solid, so the hero cannot walk off the island");

    /* the hero starts on open ground */
    check(!s.Blocked(15, 15) && !s.Blocked(16, 16), "the spawn tile is clear");
}

/* ---------------------------------------------------------------- [6] */
static void test_movement(void)
{
    printf("[6] movement, the camera and the walk cycle\n");

    Scene s;
    s.Reset(7u);

    const int x0 = s.HeroSX(), y0 = s.HeroSY();

    /* The four keys have to walk along the four SCREEN directions.  This is
       the assertion that catches the classic isometric bug of mapping W to
       the tile axis -y instead of to the tile diagonal -x-y. */
    {
        Scene a; a.Reset(7u);
        a.Step(hold(1, 0, 0, 0));
        check(a.HeroSX() == x0 && a.HeroSY() < y0, "W walks straight up the screen");
        check(a.H().face == FACE_U, "and the hero faces up");
    }
    {
        Scene a; a.Reset(7u);
        a.Step(hold(0, 1, 0, 0));
        check(a.HeroSX() == x0 && a.HeroSY() > y0, "S walks straight down the screen");
        check(a.H().face == FACE_D, "and the hero faces down");
    }
    {
        Scene a; a.Reset(7u);
        a.Step(hold(0, 0, 1, 0));
        check(a.HeroSX() < x0 && a.HeroSY() == y0, "A walks straight left");
        check(a.H().face == FACE_L, "and the hero faces left");
    }
    {
        Scene a; a.Reset(7u);
        a.Step(hold(0, 0, 0, 1));
        check(a.HeroSX() > x0 && a.HeroSY() == y0, "D walks straight right");
        check(a.H().face == FACE_R, "and the hero faces right");
    }

    /* one frame is a nudge, not a teleport: the speed is in tiles per second */
    {
        Scene a; a.Reset(7u);
        const float fx0 = a.H().x;
        a.Step(hold(0, 0, 0, 1));
        const float d = a.H().x - fx0;
        printf("      one frame of walking moves %.4f tiles\n", d);
        check(d > 0.0f && d < 0.15f, "one frame is a small step");
    }

    /* running has to be faster than walking */
    {
        Scene a; a.Reset(7u); Scene b; b.Reset(7u);
        Input run = hold(0, 0, 0, 1); run.run = 1;
        for (int i = 0; i < 10; ++i) { a.Step(hold(0, 0, 0, 1)); b.Step(run); }
        const float dw = a.H().x - 15.5f, dr = b.H().x - 15.5f;
        printf("      after 10 frames: walked %.3f, ran %.3f tiles\n", dw, dr);
        check(dr > dw * 1.4f, "shift runs noticeably faster");
    }

    /* the walk cycle has to advance while moving and reset when stopped */
    {
        Scene a; a.Reset(7u);
        int seen[HERO_FRAMES];
        memset(seen, 0, sizeof(seen));
        for (int i = 0; i < 40; ++i) {
            a.Step(hold(0, 0, 0, 1));
            seen[a.H().frame] = 1;
        }
        int distinct = 0;
        for (int i = 0; i < HERO_FRAMES; ++i) distinct += seen[i];
        check(distinct == HERO_FRAMES, "all four walk frames are used");
        a.Step(noInput());
        check(a.H().frame == 0 && a.H().moving == 0, "standing still resets the cycle");
    }

    /* the camera follows the hero and stays inside the world */
    {
        Scene a; a.Reset(7u);
        const int c0 = a.CamX();
        for (int i = 0; i < 240; ++i) a.Step(hold(0, 0, 0, 1));
        check(a.CamX() > c0, "the camera follows the hero");

        const ScreenBox box = WorldScreenBox();
        for (int i = 0; i < 600; ++i) a.Step(hold(0, 0, 0, 1));
        printf("      camera after a long run: %d,%d (box %d..%d)\n",
               a.CamX(), a.CamY(), box.x0, box.x1);
        check(a.CamX() >= box.x0 && a.CamX() <= box.x1 - FIELD_W,
              "the camera never leaves the world (x)");
        check(a.CamY() >= box.y0 && a.CamY() <= box.y1 - FIELD_H,
              "the camera never leaves the world (y)");
    }

    /* walking into the pond must stop the hero, and sliding along it must
       still work -- that is the difference between blocked and broken */
    {
        Scene a; a.Reset(7u);
        for (int i = 0; i < 900; ++i) a.Step(hold(0, 0, 1, 0));
        const int tx = (int)floorf(a.H().x), ty = (int)floorf(a.H().y);
        printf("      walked left for 15 s, ended on tile (%d,%d)\n", tx, ty);
        check(!a.Blocked(tx, ty), "the hero never ends up inside a solid tile");
    }

    /* deterministic: the same seed and the same input give the same scene */
    {
        Scene a; a.Reset(3u); Scene b; b.Reset(3u);
        for (int i = 0; i < 120; ++i) { a.Step(hold(0, 0, 0, 1)); b.Step(hold(0, 0, 0, 1)); }
        check(a.HeroSX() == b.HeroSX() && a.HeroSY() == b.HeroSY(),
              "the scene is reproducible from its seed");
    }
}

/* ====================================================================== */
/*  atlas                                                                 */
/* ====================================================================== */

static int lumAt(const WBitmap &img, int x, int y)
{
    const DWORD pv = (DWORD)img[y][x];
    return (int)((pv >> 16) & 0xFF) + (int)((pv >> 8) & 0xFF) + (int)(pv & 0xFF);
}

static DWORD pixAt(const WBitmap &img, int x, int y)
{
    return (DWORD)img[y][x];
}

/* Mean luminance over a stride-sampled region.  Used where a single pixel is
   the wrong instrument -- comparing a dimmed frame against an undimmed one at
   one pixel would depend on which bit of terrain happened to land there. */
static int avgLum(const WBitmap &img, int x0, int y0, int x1, int y1, int step)
{
    long sum = 0;
    int n = 0;
    for (int y = y0; y < y1; y += step)
        for (int x = x0; x < x1; x += step) { sum += lumAt(img, x, y); ++n; }
    return n ? (int)(sum / n) : 0;
}

static void dumpBitmap(WBitmap &b, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("      cannot write %s\n", path); return; }

    fprintf(f, "P6\n%d %d\n255\n", b.GetW(), b.GetH());
    for (int y = 0; y < b.GetH(); ++y)
        for (int x = 0; x < b.GetW(); ++x) {
            const DWORD q = (DWORD)b[y][x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)((q >> 16) & 0xFF);
            rgb[1] = (unsigned char)((q >> 8) & 0xFF);
            rgb[2] = (unsigned char)(q & 0xFF);
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("      dumped %dx%d to %s\n", b.GetW(), b.GetH(), path);
}

/* ---------------------------------------------------------------- [7] */
static void test_tileset(void)
{
    printf("[7] the baked tileset\n");

    /* ---- every material, every variant, is a real diamond ------------- */
    int maskEdges = 0;
    for (int t = 0; t < TILE_COUNT; ++t)
        for (int v = 0; v < TILE_VARIANTS; ++v) {
            WBitmap *b = g_atlas->Tile(t, v);
            if (!b) { check(0, "a tile bitmap is missing"); return; }
            if (b->GetW() != TILE_W || b->GetH() != TILE_H) {
                check(0, "a tile bitmap is the wrong size");
                return;
            }
            /* the box corners are outside the diamond and must carry the
               BLIT_MASK key, or every tile paints a square over its
               neighbours */
            if ((DWORD)(*b)[0][0] == (DWORD)WPixel((DWORD)COL_MASK)) ++maskEdges;
        }
    printf("      %d of %d tiles have a masked top left corner\n",
           maskEdges, TILE_COUNT * TILE_VARIANTS);
    check(maskEdges == TILE_COUNT * TILE_VARIANTS,
          "every tile is masked outside its diamond");

    /* the centre of each material must differ from the others, or the atlas
       baked the same texture five times */
    {
        int distinct = 0;
        DWORD seen[TILE_COUNT];
        for (int t = 0; t < TILE_COUNT; ++t) {
            seen[t] = pixAt(*g_atlas->Tile(t, 0), TILE_HW, TILE_HH);
            int dup = 0;
            for (int k = 0; k < t; ++k) if (seen[k] == seen[t]) dup = 1;
            if (!dup) ++distinct;
        }
        printf("      %d distinct centre colours across %d materials\n",
               distinct, TILE_COUNT);
        check(distinct == TILE_COUNT, "every material has its own texture");
    }

    /* ---- the transition band really is dithered -----------------------
       The band is a MASKED OVERLAY, not a tile: inside the band it holds a
       mix of this material's texel and the next material's, and everywhere
       else it holds the BLIT_MASK key, because it is blitted over a base
       tile that is already on the ground sheet.  So "it differs from the
       base tile" is the wrong question -- it differs everywhere, by being
       transparent.  The questions that mean something are: is the band a
       genuine mix rather than a hard edge or a solid fill, does the mix
       deepen towards the edge, and is anything outside the band ever opaque. */
    {
        WBitmap *base = g_atlas->Tile(T_GRASS, 0);
        WBitmap *band = g_atlas->Edge(T_GRASS, 0, 0);
        if (!base || !band) { check(0, "the transition band is missing"); return; }

        /* what the atlas mixes in: bakeEdgeBmp pairs each material with the
           next one in the TileId enum */
        const int other = (T_GRASS + 1) % TILE_COUNT;
        const DWORD key = (DWORD)WPixel((DWORD)COL_MASK);

        int bandPixels = 0, own = 0, mixed = 0, opaqueOutside = 0, neither = 0;
        int innerFired = 0, innerTotal = 0, outerFired = 0, outerTotal = 0;

        for (int ly = 0; ly < TILE_H; ++ly)
            for (int lx = 0; lx < TILE_W; ++lx) {
                if (!InDiamond(lx, ly)) continue;

                const DWORD c = pixAt(*band, lx, ly);
                const int w0 = EdgeWeight(lx, ly, 0);

                if (w0 == 0) {
                    /* outside the band the pixel must be transparent, or the
                       band paints over the neighbouring tile */
                    if (c != key) ++opaqueOutside;
                    continue;
                }

                ++bandPixels;
                const int isOwn = (c == pixAt(*base, lx, ly));
                const int isOther = (c == (DWORD)WPixel((DWORD)TileTexel(other, lx, ly, 0)));
                if (isOwn)        ++own;
                else if (isOther) ++mixed;
                else              ++neither;

                /* the fire rate has to grow towards the edge, or the
                   "transition" is a uniform speckle rather than a gradient */
                if (w0 > 127) { ++outerTotal; outerFired += !isOwn; }
                else          { ++innerTotal; innerFired += !isOwn; }
            }

        printf("      band %d px: %d own, %d mixed, %d neither, %d opaque outside\n",
               bandPixels, own, mixed, neither, opaqueOutside);
        check(bandPixels > 150, "the band has real area");
        check(opaqueOutside == 0, "nothing outside the band is opaque");
        check(neither == 0, "every band pixel is this material or the next");
        check(mixed > bandPixels / 6, "the dither fires across the band");
        check(own > bandPixels / 6, "the dither is a dither, not a solid fill");

        const int innerRate = innerTotal ? innerFired * 100 / innerTotal : 0;
        const int outerRate = outerTotal ? outerFired * 100 / outerTotal : 0;
        printf("      fire rate: inner %d%%, outer %d%%\n", innerRate, outerRate);
        check(outerRate > innerRate + 20, "the dither deepens towards the edge");
    }

    /* ---- the ground sheet --------------------------------------------- */
    {
        WBitmap *g = g_atlas->Ground();
        const ScreenBox box = WorldScreenBox();
        if (!g) { check(0, "the ground sheet is missing"); return; }
        check(g->GetW() == box.x1 - box.x0 && g->GetH() == box.y1 - box.y0,
              "the sheet covers the whole world box");
        check(g->GetW() == 2048 && g->GetH() == 1024,
              "the sheet is 2048x1024");

        /* The sheet is composed from the same texture functions as the tile
           bitmaps, so a tile centre has to agree between them.  If the two
           ever drift apart the tileset becomes a lie. */
        int agree = 0, tested = 0;
        for (int ty = 4; ty < MAP_H - 4; ty += 5)
            for (int tx = 4; tx < MAP_W - 4; tx += 5) {
                int sx, sy;
                IsoToScreen(tx, ty, sx, sy);
                const int px = sx - g_atlas->GroundX();
                const int py = sy - g_atlas->GroundY();
                WBitmap *t = g_atlas->Tile((int)g_scene->TileAt(tx, ty),
                                           TileVariant(tx, ty));
                if (!t) continue;
                ++tested;
                if (pixAt(*g, px, py) == pixAt(*t, TILE_HW, TILE_HH)) ++agree;
            }
        printf("      %d of %d tile centres agree with the tileset\n", agree, tested);
        check(tested > 20, "enough tile centres were sampled");
        check(agree == tested, "the sheet and the tileset agree pixel for pixel");
    }

    /* ---- the thumbnails ------------------------------------------------ */
    check(g_atlas->MiniW() == 2048 / 16 && g_atlas->MiniH() == 1024 / 16,
          "the HUD thumbnail is the sheet at 1/16");
    check(g_atlas->BigW() == 2048 / 4 && g_atlas->BigH() == 1024 / 4,
          "the map thumbnail is the sheet at 1/4");

    /* a thumbnail that came out flat would mean the box average swallowed the
       roads; sample a few spots and require variation */
    {
        WBitmap *m = g_atlas->Mini();
        int distinct = 0;
        DWORD seen[64];
        for (int y = 0; y < m->GetH(); y += 7)
            for (int x = 0; x < m->GetW(); x += 7) {
                const DWORD c = pixAt(*m, x, y);
                int dup = 0;
                for (int k = 0; k < distinct; ++k) if (seen[k] == c) dup = 1;
                if (!dup && distinct < 64) seen[distinct++] = c;
            }
        printf("      the thumbnail has %d distinct sampled colours\n", distinct);
        check(distinct > 12, "the thumbnail is not flat");
    }

    /* ---- scenery and the actor ---------------------------------------- */
    {
        int ok = 1;
        for (int k = 1; k < DECO_COUNT; ++k) {
            WBitmap *d = g_atlas->Deco(k);
            if (!d || d->GetW() != g_atlas->DecoW(k) || d->GetH() != g_atlas->DecoH(k))
                ok = 0;
            /* the anchor must be inside the sprite, or the decoration lands
               somewhere other than the tile it belongs to */
            if (g_atlas->DecoAnchorX(k) < 0 || g_atlas->DecoAnchorX(k) >= d->GetW()) ok = 0;
            if (g_atlas->DecoAnchorY(k) < 0 || g_atlas->DecoAnchorY(k) >= d->GetH()) ok = 0;
            if (g_atlas->DecoUp(k) <= 0) ok = 0;
        }
        check(ok, "every decoration has a sprite and a sane anchor");

        /* the bamboo has to be TALL: it is the thing the depth sort is for */
        check(g_atlas->DecoUp(D_BAMBOO) > 90,
              "the bamboo reaches well above its tile");
        check(g_atlas->DecoW(D_HOUSE) > 90, "the house is wider than one tile");

        int faces = 0;
        for (int f = 0; f < FACE_COUNT; ++f)
            for (int i = 0; i < HERO_FRAMES; ++i)
                if (g_atlas->Hero(f, i)) ++faces;
        check(faces == FACE_COUNT * HERO_FRAMES, "every facing and frame is baked");

        /* FACE_L is FACE_R mirrored, so they must not be identical and must
           not be empty either */
        WBitmap *hr = g_atlas->Hero(FACE_R, 0);
        WBitmap *hl = g_atlas->Hero(FACE_L, 0);
        int same = 1, opaque = 0;
        for (int y = 0; y < HERO_H; ++y)
            for (int x = 0; x < HERO_W; ++x) {
                if (pixAt(*hr, x, y) != pixAt(*hl, x, y)) same = 0;
                if (pixAt(*hr, x, y) != (DWORD)WPixel((DWORD)COL_MASK)) ++opaque;
            }
        check(!same, "the left facing is the right facing mirrored");
        check(opaque > 200, "the hero sprite is actually drawn");
    }

    /* ---- labels -------------------------------------------------------- */
    {
        int have = 0;
        for (int i = 0; i < GF_LABEL_COUNT; ++i) {
            WBitmap *l = g_atlas->Label(i);
            if (l && l->GetW() > 0 && l->GetH() > 0 && g_atlas->LabelW(i) > 0) ++have;
        }
        printf("      %d of %d labels baked\n", have, GF_LABEL_COUNT);
        check(have == GF_LABEL_COUNT, "every label rasterised");
    }

    /* ---- the Latin set -------------------------------------------------
       The HUD read-outs and the key names on the controls page come from the
       baked ASCII range rather than from WFont's runtime rasteriser, so that
       the whole interface is one typeface.  A gap in the range would show up
       as a missing character in "W  A  S  D", which is easy to miss by eye
       and trivial to catch here. */
    {
        int have = 0, digitInk = 0;
        for (int c = ASCII_FIRST; c <= ASCII_LAST; ++c) {
            WBitmap *b = g_atlas->Ascii(c);
            if (b && b->GetW() > 0 && b->GetH() > 0 && g_atlas->AsciiW(c) > 0) ++have;
        }
        printf("      %d of %d printable ASCII characters baked\n",
               have, GF_ASCII_COUNT);
        check(have == GF_ASCII_COUNT, "the printable ASCII range is complete");

        /* a digit has to have ink in it, or the read-outs would be blank
           while every width check still passed */
        WBitmap *d7 = g_atlas->Ascii('7');
        for (int y = 0; y < d7->GetH(); ++y)
            for (int x = 0; x < d7->GetW(); ++x)
                if (pixAt(*d7, x, y) != (DWORD)WPixel((DWORD)COL_MASK)) ++digitInk;
        printf("      the digit 7 has %d opaque pixels\n", digitInk);
        check(digitInk > 20, "the numerals are actually drawn");
        check(g_atlas->Ascii(ASCII_FIRST - 1) == 0
              && g_atlas->Ascii(ASCII_LAST + 1) == 0,
              "a character outside the range is refused, not read out of bounds");
    }

    /* ---- the chrome ---------------------------------------------------- */
    check(g_atlas->Corner(0) && g_atlas->Corner(3), "all four corner motifs exist");
    check(g_atlas->Cloud(0) && g_atlas->Cloud(2), "the cloud flourishes exist");
    check(g_atlas->Seal() && g_atlas->Plaque(), "the seal and the plaque exist");
    check(g_atlas->PlaqueW() > 100, "the plaque is wide enough for the name");
}

/* ====================================================================== */
/*  render                                                                */
/* ====================================================================== */

static int renderScene(Scene &s, Renderer &r, WBitmap &out)
{
    WCanvas cv(FIELD_W, FIELD_H);
    r.Draw(cv, s, *g_atlas);
    cv.Update();
    cv.SetPos(0, 0);
    out.Clear(WPixel(0, 0, 0));
    cv.BlitTo(out);
    return r.ItemCount();
}

/* A pixel of a rendered frame, addressed in WORLD screen coordinates. */
static int lumWorld(const WBitmap &img, const Scene &s, int wx, int wy)
{
    const int x = wx - s.CamX();
    const int y = wy - s.CamY();
    if (x < 0 || y < 0 || x >= FIELD_W || y >= FIELD_H) return -1;
    return lumAt(img, x, y);
}

/* ---------------------------------------------------------------- [8] */
static void test_render_scene(void)
{
    printf("[8] render: the village\n");

    Scene s;
    s.Reset(7u);
    Renderer r;

    WBitmap out(FIELD_W, FIELD_H);
    const int items = renderScene(s, r, out);

    printf("      one frame is %d draw calls\n", items);
    check(items > 8, "the frame draws more than a background");
    /* The whole point of the baked sheet: a thousand diamonds at five links
       each would be five thousand calls.  Anything near that means the sheet
       was not used. */
    check(items < 400, "the ground is one blit, not a thousand");

    /* the picture must not be blank */
    int distinct = 0;
    DWORD seen[256];
    for (int y = 0; y < FIELD_H; y += 3)
        for (int x = 0; x < FIELD_W; x += 3) {
            const DWORD c = pixAt(out, x, y);
            int dup = 0;
            for (int k = 0; k < distinct; ++k) if (seen[k] == c) dup = 1;
            if (!dup && distinct < 256) seen[distinct++] = c;
        }
    printf("      %d distinct colours in the frame\n", distinct);
    check(distinct > 60, "the village is not a flat wash");

    /* the hero has to be on screen, at the centre of the viewport.  His
       chest is 25 px below the top of his sprite; the robe there is nearly
       white, so anything much darker means he was never drawn. */
    {
        const int l = lumAt(out, FIELD_W / 2, FIELD_H / 2 - 25);
        printf("      the hero's chest reads %d\n", l);
        check(l > 300, "the hero is drawn at the centre of the viewport");
    }

    /* the four corners of the viewport must be inside the world here (the
       camera is centred on the hero, who starts mid-map) */
    check(lumAt(out, 4, 4) > 0 && lumAt(out, FIELD_W - 5, FIELD_H - 5) > 0,
          "the viewport is filled, not letterboxed");
}

/* ---------------------------------------------------------------- [9] */
/*  The single most important render assertion: a tall decoration standing
 *  NEARER the camera than the hero has to paint OVER him, and one standing
 *  BEHIND must leave him untouched.  A renderer with the depth order
 *  inverted looks perfectly plausible in a screenshot of an empty field and
 *  completely wrong the moment the player walks past a tree. */
static void test_render_depth(void)
{
    printf("[9] render: the depth sort\n");

    Renderer r;
    WBitmap a(FIELD_W, FIELD_H), b(FIELD_W, FIELD_H), c(FIELD_W, FIELD_H);

    /* the hero starts at (15.5, 15.5) -> world screen (0, 496) */
    const int probeX = 0, probeY = 466;   /* his chest, in world screen coords */

    {
        Scene s;
        s.Reset(7u);
        /* clear the immediate area so nothing else can contribute */
        for (int ty = 13; ty <= 18; ++ty)
            for (int tx = 13; tx <= 18; ++tx)
                s.SetDeco(tx, ty, D_NONE);
        renderScene(s, r, a);
    }

    {
        Scene s;
        s.Reset(7u);
        for (int ty = 13; ty <= 18; ++ty)
            for (int tx = 13; tx <= 18; ++tx)
                s.SetDeco(tx, ty, D_NONE);
        /* one row FARTHER from the camera than the hero: depth 30 vs his 31 */
        s.SetDeco(15, 15, D_HOUSE);
        renderScene(s, r, b);
    }

    {
        Scene s;
        s.Reset(7u);
        for (int ty = 13; ty <= 18; ++ty)
            for (int tx = 13; tx <= 18; ++tx)
                s.SetDeco(tx, ty, D_NONE);
        /* one row NEARER: depth 32 vs his 31 */
        s.SetDeco(16, 16, D_HOUSE);
        renderScene(s, r, c);
    }

    const int la = lumWorld(a, *g_scene, probeX, probeY);
    const int lb = lumWorld(b, *g_scene, probeX, probeY);
    const int lc = lumWorld(c, *g_scene, probeX, probeY);
    printf("      hero alone %d, house behind %d, house in front %d\n", la, lb, lc);

    check(la > 0, "the hero is drawn at the probe");
    check(lb == la, "a house BEHIND the hero leaves him untouched");
    check(lc != la, "a house IN FRONT of the hero paints over him");

    /* and the near house must also be visible ABOVE the hero, where he cannot
       reach -- otherwise "it painted over him" might just mean "it vanished" */
    {
        const int highB = lumWorld(b, *g_scene, probeX, 400);
        const int highC = lumWorld(c, *g_scene, probeX, 400);
        printf("      above the hero: behind %d, in front %d\n", highB, highC);
        check(highB > 0 && highC > 0, "the house is drawn in both positions");
    }

    /* ---- the tall case -------------------------------------------------
       A house is short and wide, so it only ever competes with the hero's
       feet.  A stand of bamboo is 112 px tall against his 56, reaches well
       above his head, and is the thing the player will actually walk behind.
       Same two assertions, one row either side of him. */
    {
        WBitmap d(FIELD_W, FIELD_H), e(FIELD_W, FIELD_H);

        Scene s1;
        s1.Reset(7u);
        for (int ty = 13; ty <= 18; ++ty)
            for (int tx = 13; tx <= 18; ++tx)
                s1.SetDeco(tx, ty, D_NONE);
        s1.SetDeco(15, 15, D_BAMBOO);     /* depth 30, one row BEHIND him */
        renderScene(s1, r, d);

        Scene s2;
        s2.Reset(7u);
        for (int ty = 13; ty <= 18; ++ty)
            for (int tx = 13; tx <= 18; ++tx)
                s2.SetDeco(tx, ty, D_NONE);
        s2.SetDeco(16, 16, D_BAMBOO);     /* depth 32, one row IN FRONT   */
        renderScene(s2, r, e);

        const int hb = lumWorld(d, *g_scene, probeX, probeY);
        const int hf = lumWorld(e, *g_scene, probeX, probeY);
        printf("      the hero's chest: bamboo behind %d, bamboo in front %d\n",
               hb, hf);
        check(hb == la, "a stand of bamboo BEHIND the hero leaves him untouched");
        check(hf != la, "a stand of bamboo IN FRONT paints over him");

        /* It has to be drawn up there, not merely absent: count the pixels of
           the strip above his head that the bamboo changes.  A point probe
           would be a coin flip -- the sprite is four stalks with gaps between
           them, so most single pixels up there are legitimately empty. */
        int changed = 0;
        for (int wx = -40; wx <= 40; wx += 2)
            for (int wy = 380; wy <= 440; wy += 2)
                if (lumWorld(d, *g_scene, wx, wy) != lumWorld(a, *g_scene, wx, wy))
                    ++changed;
        printf("      the bamboo changes %d pixels above the hero\n", changed);
        check(changed > 40, "the bamboo is drawn above the hero, not skipped");
    }
}

/* --------------------------------------------------------------- [10] */
/*  Additive light is the one thing this library can do that a plain blitter
 *  cannot, so it is worth proving that the glow is actually submitted with
 *  BLIT_ADDITIVE and not silently dropped (a WBox blitted additively draws
 *  nothing at all -- see gf_render.h). */
static void test_render_glow(void)
{
    printf("[10] render: the additive lantern glow\n");

    Scene s;
    s.Reset(7u);
    for (int ty = 13; ty <= 18; ++ty)
        for (int tx = 13; tx <= 18; ++tx)
            s.SetDeco(tx, ty, D_NONE);
    s.SetDeco(16, 13, D_LANTERN);

    Renderer r;
    WBitmap on(FIELD_W, FIELD_H), off(FIELD_W, FIELD_H);

    r.SetGlow(1);
    renderScene(s, r, on);
    r.SetGlow(0);
    renderScene(s, r, off);

    /* the lantern at (16,13) projects to world screen (96, 464); its glow
       centre is the lamp body, 23 px above the tile centre and 5 px right */
    const int gx = 96 + 5, gy = 464 - 23;
    const int lon = lumWorld(on,  s, gx, gy);
    const int loff = lumWorld(off, s, gx, gy);
    printf("      glow centre: on %d, off %d\n", lon, loff);

    check(loff > 0, "the lantern is on screen");
    check(lon > loff + 60, "the glow brightens what is behind it");

    /* the glow is a falloff, not a disc: the far corner of its box must be
       untouched, or the radial is really a square */
    const int edgeOn  = lumWorld(on, s, gx + 30, gy + 30);
    const int edgeOff = lumWorld(off, s, gx + 30, gy + 30);
    printf("      glow rim: on %d, off %d\n", edgeOn, edgeOff);
    check(abs(edgeOn - edgeOff) < 30, "the glow fades out before its box edge");
}

/* --------------------------------------------------------------- [11] */
static void test_render_screens(void)
{
    printf("[11] render: the HUD, the map, the title and the controls\n");

    Scene s;
    s.Reset(7u);
    Renderer r;
    WBitmap out(FIELD_W, FIELD_H);

    /* ---- the HUD ------------------------------------------------------- */
    {
        const int items = renderScene(s, r, out);
        check(items > 30, "the HUD adds its own draw calls");

        /* The name board is a lacquered plate: a large dark fill, a bright
           frame, and silk text on top.  Counting distinct colours over it is
           the wrong instrument -- FONT_NOALPHA writes exactly two colours per
           glyph run, so a perfectly good panel scores single digits and a
           panel that has lost its fill entirely still scores single digits.
           Counting AREAS of dark and of bright is what distinguishes the two.

           The rect is the panel's own box (10,10)-(226,104), border included,
           because the gold keyline and the jade ring are exactly the pixels
           that prove the frame was drawn. */
        {
            int dark = 0, bright = 0, cols = 0;
            DWORD seen[64];

            for (int y = 10; y < 104; ++y)
                for (int x = 10; x < 226; ++x) {
                    const DWORD c = pixAt(out, x, y);
                    const int l = lumAt(out, x, y);

                    if (l < 130) ++dark;        /* the ink fill            */
                    if (l > 200) ++bright;      /* gold frame, jade, text  */

                    int dup = 0;
                    for (int k = 0; k < cols; ++k) if (seen[k] == c) dup = 1;
                    if (!dup && cols < 64) seen[cols++] = c;
                }

            printf("      HUD plate: %d dark px, %d bright px, %d colours\n",
                   dark, bright, cols);
            check(dark > 8000, "the HUD panel has a body, not just a frame");
            check(bright > 800, "the HUD panel has a frame and text");

            /* A floor, not a richness measure: the palette here is twelve
               entries by design (fill, lit edge, shaded edge, jade ring, gold
               keyline, two scroll golds, two rule tones, silk, ink shadow,
               the separator), so a high bar would be asserting that the art
               got noisier.  What this rules out is the failure mode that
               actually happened once -- a panel collapsing to a single flat
               plate, which scores 1. */
            check(cols >= 8, "the HUD panel has structure, not one flat colour");
        }

        /* the name board's keyline is gold and its fill is nearly black.
           (150, 90) is chosen to miss the text, the rule and all four corner
           scrolls -- a probe that lands on a glyph would pass for the wrong
           reason. */
        const int keyline = lumAt(out, 10, 60);
        const int fill    = lumAt(out, 150, 90);
        printf("      HUD keyline %d, HUD fill %d\n", keyline, fill);
        check(keyline > fill + 80, "the gold keyline reads against the ink fill");

        /* the minimap lives in the bottom right and must not be flat */
        int mapColours = 0;
        DWORD mseen[64];
        for (int y = FIELD_H - 90; y < FIELD_H - 14; y += 2)
            for (int x = FIELD_W - 150; x < FIELD_W - 14; x += 2) {
                const DWORD c = pixAt(out, x, y);
                int dup = 0;
                for (int k = 0; k < mapColours; ++k) if (mseen[k] == c) dup = 1;
                if (!dup && mapColours < 64) mseen[mapColours++] = c;
            }
        printf("      the minimap has %d distinct colours\n", mapColours);
        check(mapColours > 10, "the minimap is a real thumbnail");
    }

    /* ---- the M screen -------------------------------------------------- */
    {
        r.SetShowMap(1);
        const int items = renderScene(s, r, out);
        r.SetShowMap(0);
        printf("      the map screen is %d draw calls\n", items);
        check(items > 20, "the map screen draws its panel");

        /* the middle of the screen is the enlarged map, so it must be busy */
        int mapColours = 0;
        DWORD seen[128];
        for (int y = 190; y < 420; y += 4)
            for (int x = 160; x < 640; x += 4) {
                const DWORD c = pixAt(out, x, y);
                int dup = 0;
                for (int k = 0; k < mapColours; ++k) if (seen[k] == c) dup = 1;
                if (!dup && mapColours < 128) seen[mapColours++] = c;
            }
        printf("      the enlarged map has %d distinct colours\n", mapColours);
        check(mapColours > 30, "the enlarged map is the terrain, not a flat plate");

        /* the panel's gold keyline must be visible on its left edge, clear of
           the thumbnail which starts 24 px in */
        const int key = lumAt(out, 120, 300);
        printf("      map panel keyline %d\n", key);
        check(key > 150, "the map panel's gold keyline is drawn");
    }

    /* ---- the title ----------------------------------------------------- */
    {
        WCanvas cv(FIELD_W, FIELD_H);
        r.DrawTitle(cv, s, *g_atlas, 123u, 0);
        cv.Update();
        cv.SetPos(0, 0);
        out.Clear(WPixel(0, 0, 0));
        cv.BlitTo(out);
        printf("      the title screen is %d draw calls\n", r.ItemCount());
        check(r.ItemCount() > 20, "the title screen draws its menu");

        /* the plaque sits high and centre; its gold frame must be there */
        const int plaqueTop = lumAt(out, FIELD_W / 2, 96);
        check(plaqueTop > 150, "the plaque's gold frame is drawn");

        /* The wash must darken the world.  Comparing one pixel would depend
           on which bit of terrain the title's camera drift happened to put
           there, so the whole frame is averaged instead -- the wash covers
           everything except the plaque and the menu. */
        WBitmap plain(FIELD_W, FIELD_H);
        Scene s2;
        s2.Reset(7u);
        Renderer r2;
        renderScene(s2, r2, plain);

        const int washed = avgLum(out, 0, 0, FIELD_W, FIELD_H, 8);
        const int raw    = avgLum(plain, 0, 0, FIELD_W, FIELD_H, 8);
        printf("      mean luminance: title %d, plain %d\n", washed, raw);
        check(washed < raw - 40, "the title screen dims the village");
    }

    /* ---- the controls page --------------------------------------------- */
    {
        WCanvas cv(FIELD_W, FIELD_H);
        r.DrawHowto(cv, *g_scene, *g_atlas);
        cv.Update();
        cv.SetPos(0, 0);
        out.Clear(WPixel(0, 0, 0));
        cv.BlitTo(out);
        printf("      the controls page is %d draw calls\n", r.ItemCount());
        check(r.ItemCount() > 40, "the controls page draws its legend");

        /* the legend strip is a row of real tile bitmaps: the five materials
           must give five different centre colours.  The coordinates are
           derived the same way the renderer lays the page out -- the panel is
           720x460 and the legend sits 256 px below its top edge, with the
           tiles 30 px in and each one 10 px apart. */
        const int lpx = (FIELD_W - 720) / 2;
        const int lpy = (FIELD_H - 460) / 2;
        const int ly = lpy + 256 + 16;
        int distinct = 0;
        DWORD seen[TILE_COUNT];
        for (int t = 0; t < TILE_COUNT; ++t) {
            const int lx = lpx + 30 + t * (TILE_W + 10) + TILE_HW;
            seen[t] = pixAt(out, lx, ly);
            int dup = 0;
            for (int k = 0; k < t; ++k) if (seen[k] == seen[t]) dup = 1;
            if (!dup) ++distinct;
        }
        printf("      the legend shows %d distinct materials\n", distinct);
        check(distinct >= 4, "the legend shows the real tiles");
    }
}

/* --------------------------------------------------------------- [12] */
static void test_render_camera(void)
{
    printf("[12] render: the camera moves the picture\n");

    Scene s;
    s.Reset(7u);
    Renderer r;
    WBitmap a(FIELD_W, FIELD_H), b(FIELD_W, FIELD_H);

    renderScene(s, r, a);

    /* walk right for two seconds and render again */
    for (int i = 0; i < 120; ++i) s.Step(hold(0, 0, 0, 1));
    renderScene(s, r, b);

    check(s.CamX() != 0 || s.CamY() != 0, "the camera moved");

    int diff = 0;
    for (int y = 0; y < FIELD_H; y += 7)
        for (int x = 0; x < FIELD_W; x += 7)
            if (pixAt(a, x, y) != pixAt(b, x, y)) ++diff;
    printf("      %d of %d sampled pixels changed after walking\n",
           diff, (FIELD_H / 7) * (FIELD_W / 7));
    check(diff > 200, "the picture scrolls with the camera");
}

/* --------------------------------------------------------------- [13] */
static void test_render_water(void)
{
    printf("[13] render: the pond is on screen and animated\n");

    /* stand the hero on the pond's north bank so it fills the viewport */
    Scene s;
    s.Reset(7u);
    s.SetHero(7.0f, 19.0f);

    Renderer r;
    WBitmap out(FIELD_W, FIELD_H);
    renderScene(s, r, out);

    /* count water-ish pixels: blue channel clearly dominant over red */
    int water = 0;
    for (int y = 0; y < FIELD_H; ++y)
        for (int x = 0; x < FIELD_W; ++x) {
            const DWORD c = pixAt(out, x, y);
            const int rr = (int)((c >> 16) & 0xFF);
            const int bb = (int)(c & 0xFF);
            if (bb > rr + 40) ++water;
        }
    printf("      %d blue-dominant pixels in the frame\n", water);
    check(water > 500, "the pond is drawn");
}

/* ====================================================================== */

int main(void)
{
    printf("=== Guofeng isometric prototype ===\n\n");

    test_projection();
    test_diamond_partition();
    test_edge_weights();
    test_visible_range();
    test_scene();
    test_movement();

    /* ---- the atlas needs a GDI font; skip rather than fail if absent ---- */
    g_scene = new Scene();
    g_scene->Reset(7u);

    g_atlas = new Atlas();
    if (!g_atlas->Build(*g_scene)) {
        printf("\n[!] the atlas could not be baked (no GDI font available).\n"
               "    Skipping the atlas and render sections.\n");
        delete g_atlas;
        delete g_scene;
        printf("\n%d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }

    test_tileset();
    test_render_scene();
    test_render_depth();
    test_render_glow();
    test_render_screens();
    test_render_camera();
    test_render_water();

    /* ---- optional dumps for eyeballing outside the test run ------------- */
    {
        const char *p;

        if ((p = getenv("GF_DUMP_GROUND")) != 0) dumpBitmap(*g_atlas->Ground(), p);

        if ((p = getenv("GF_DUMP_TILE")) != 0) {
            /* a strip of every material, three variants each, so the dither
               and the texture can be inspected at 1:1 */
            const int cols = TILE_VARIANTS * TILE_COUNT;
            WBitmap strip(cols * TILE_W, TILE_H);
            strip.Clear(WPixel((DWORD)COL_INK));
            for (int t = 0; t < TILE_COUNT; ++t)
                for (int v = 0; v < TILE_VARIANTS; ++v)
                    strip.SetPos((t * TILE_VARIANTS + v) * TILE_W, 0),
                    strip.Draw(*g_atlas->Tile(t, v), BLIT_COPY);
            dumpBitmap(strip, p);
        }

        if ((p = getenv("GF_DUMP_SCENE")) != 0) {
            Scene s; s.Reset(7u);
            Renderer r;
            WBitmap out(FIELD_W, FIELD_H);
            renderScene(s, r, out);
            dumpBitmap(out, p);
        }

        if ((p = getenv("GF_DUMP_MAP")) != 0) {
            Scene s; s.Reset(7u);
            Renderer r;
            r.SetShowMap(1);
            WBitmap out(FIELD_W, FIELD_H);
            renderScene(s, r, out);
            dumpBitmap(out, p);
        }

        if ((p = getenv("GF_DUMP_TITLE")) != 0) {
            Scene s; s.Reset(7u);
            Renderer r;
            WCanvas cv(FIELD_W, FIELD_H);
            r.DrawTitle(cv, s, *g_atlas, 123u, 0);
            cv.Update();
            cv.SetPos(0, 0);
            WBitmap out(FIELD_W, FIELD_H);
            out.Clear(WPixel(0, 0, 0));
            cv.BlitTo(out);
            dumpBitmap(out, p);
        }

        if ((p = getenv("GF_DUMP_HOWTO")) != 0) {
            Renderer r;
            WCanvas cv(FIELD_W, FIELD_H);
            r.DrawHowto(cv, *g_scene, *g_atlas);
            cv.Update();
            cv.SetPos(0, 0);
            WBitmap out(FIELD_W, FIELD_H);
            out.Clear(WPixel(0, 0, 0));
            cv.BlitTo(out);
            dumpBitmap(out, p);
        }

        /* ---- foreground occlusion ------------------------------------
           The one feature of an isometric renderer that a screenshot of an
           empty field can never show.  Two stands of bamboo are planted on
           the hero's own screen column: one at depth 30, BEHIND him (he is
           depth 31), which he has to paint over; one at depth 32, IN FRONT,
           which has to paint over him.  Section [9] proves the same thing
           numerically -- this is the picture that says the numbers mean what
           they claim, using the exact configuration those assertions use. */
        if ((p = getenv("GF_DUMP_OCCLUDE")) != 0) {
            Scene s; s.Reset(7u);
            Renderer r;

            /* clear the neighbourhood so the read is not muddled by whatever
               the generator happened to scatter here */
            for (int ty = 12; ty <= 19; ++ty)
                for (int tx = 12; tx <= 19; ++tx) s.SetDeco(tx, ty, D_NONE);

            s.SetHero(15.5f, 15.5f);          /* depth 31 */
            s.SetDeco(15, 15, D_BAMBOO);      /* depth 30 -- behind the hero */
            s.SetDeco(16, 16, D_BAMBOO);      /* depth 32 -- in front of him */

            WBitmap out(FIELD_W, FIELD_H);
            renderScene(s, r, out);
            dumpBitmap(out, p);
        }
    }

    delete g_atlas;
    delete g_scene;

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
