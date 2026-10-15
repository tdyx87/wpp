/* ===========================================================================
 *  pvz_render.cpp -- Lawn Defense : renderer
 *  See pvz_render.h for the draw-order contract, which is the whole reason
 *  this file reads the way it does: it submits from the top layer down, so
 *  that the last thing submitted (the lawn background) ends up at the bottom.
 * ===========================================================================
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"
#include "wsgeo.h"        /* WBox -- the flat filled rectangle */

#include "pvz_render.h"
#include "pvz_art.h"

namespace pvz {

/* ------------------------------------------------------------------ colours */
static const unsigned long UI_NUM   = 0xF2EEE0;   /* numbers, milk-white       */
static const unsigned long UI_SEL   = 0xFFE76A;   /* selected-seed ring         */
static const unsigned long UI_PANEL = 0x241C14;   /* howto panel body           */
static const unsigned long UI_PANEL_E = 0x5E4212; /* howto panel edge           */
static const unsigned long UI_WOOD    = 0x8A5E33; /* the sun-bank fill          */
static const unsigned long UI_WOOD_HI = 0xC89B6A; /* the sun-bank top light     */
static const unsigned long UI_GOLD    = 0xB8893A; /* the end-plaque frame       */

Renderer::Renderer() : m_count(0), m_dim(0) {}
Renderer::~Renderer() { delete m_dim; }

void Renderer::Reset() { m_count = 0; }

/* ------------------------------------------------------------------ basics */
/*  Position lives on the CANVAS, not the atlas bitmap, so a shared sprite can
 *  be drawn at several positions in one frame without fighting itself. */
void Renderer::blit(WCanvas &cv, WBitmap &b, int x, int y, int style)
{
    cv.SetPos(x, y);
    cv.Draw(b, (DWORD)style);
    ++m_count;
}

void Renderer::box(WCanvas &cv, int x, int y, int w, int h, unsigned long col, int style)
{
    if (w <= 0 || h <= 0) return;
    WBox b(w, h, WPixel((DWORD)col));
    cv.SetPos(x, y);
    cv.Draw(b, (DWORD)style);
    ++m_count;
}

void Renderer::drawText(WCanvas &cv, const Atlas &a, int id, int x, int y)
{
    WBitmap *g = a.Label(id);
    if (g) blit(cv, *g, x, y, BLIT_MASK);
}

void Renderer::drawTextC(WCanvas &cv, const Atlas &a, int id, int cx, int y)
{
    const int w = a.LabelW(id);
    drawText(cv, a, id, cx - w / 2, y);
}

void Renderer::drawStr(WCanvas &cv, const Atlas &a, const char *s, int x, int y)
{
    int cx = x;
    for (const char *p = s; *p; ++p) {
        WBitmap *g = a.Ascii((int)(unsigned char)*p);
        if (g) { blit(cv, *g, cx, y, BLIT_MASK); cx += a.AsciiW((int)(unsigned char)*p); }
        else    cx += 10;
    }
}

int Renderer::drawNum(WCanvas &cv, const Atlas &a, int value, int x, int y)
{
    char b[16];
    int k = 0;
    if (value < 0) value = 0;
    if (value == 0) b[k++] = '0';
    else { int v = value; while (v > 0) { b[k++] = (char)('0' + v % 10); v /= 10; } }
    int cx = x;
    for (int i = k - 1; i >= 0; --i) {
        WBitmap *g = a.Ascii(b[i]);
        const int gw = a.AsciiW(b[i]);
        if (g) blit(cv, *g, cx, y, BLIT_MASK);
        cx += gw;
    }
    return cx - x;
}

static int measureNum(const Atlas &a, int value)
{
    char b[16];
    int k = 0;
    if (value <= 0) b[k++] = '0';
    else { int v = value; while (v > 0) { b[k++] = (char)('0' + v % 10); v /= 10; } }
    int w = 0;
    for (int i = k - 1; i >= 0; --i) w += a.AsciiW(b[i]);
    return w;
}

/* --------------------------------------------------------------- the dim */
void Renderer::ensureDim()
{
    if (!m_dim) {
        m_dim = new WBitmap();
        m_dim->Create(FIELD_W, FIELD_H);
        m_dim->Clear(WPixel((DWORD)0x2E2E2E));   /* subtracts a flat grey */
    }
}

void Renderer::drawDim(WCanvas &cv, int passes)
{
    if (!m_dim) return;
    for (int i = 0; i < passes; ++i) blit(cv, *m_dim, 0, 0, BLIT_SUBTRACT);
}

/* --------------------------------------------------------------- the scene */

/* A tiny integer sine, one full turn over 48 steps, +/-64.  Used for the
   drifting motes and the menu pulse; deterministic and allocation-free. */
static int wobble(int phase, int amp)
{
    static const int tab[48] = {
        0,  17, 33, 47, 59, 64, 59, 47, 33, 17, 0, -17,
        -33, -47, -59, -64, -59, -47, -33, -17, 0, 17,
        33, 47, 59, 64, 59, 47, 33, 17, 0, -17,
        -33, -47, -59, -64, -59, -47, -33, -17, 0, 17,
        33, 47, 59, 64, 59, 47
    };
    return tab[phase & 47] * amp / 64;
}

void Renderer::drawScene(WCanvas &cv, const Lawn &l, const Atlas &a)
{
    /* splats -- the baked pea stain, fresh, spread then fading */
    for (int i = 0; i < l.SplatCount(); ++i) {
        const Splat &s = l.SplatAt(i);
        WBitmap *g = a.Splat(s.life > 10 ? 0 : (s.life > 5 ? 1 : 2));
        if (g) blit(cv, *g, s.cx - 9, s.cy - 9, BLIT_MASK);
    }
    /* peas -- in front of the zombies */
    for (int i = 0; i < l.PeaCount(); ++i) {
        const Pea &p = l.PeaAt(i);
        if (a.Pea()) blit(cv, *a.Pea(), (int)p.x - PEA_W / 2, FeetY(p.row) - 34, BLIT_MASK);
    }
    /* zombie sprites */
    for (int i = 0; i < l.ZombieCount(); ++i) {
        const Zombie &z = l.ZombieAt(i);
        WBitmap *g = a.Zombie((z.anim / 8) & 1, z.eating);
        if (g) blit(cv, *g, (int)z.x - ZOMBIE_W / 2, FeetY(z.row) - ZOMBIE_H + 2, BLIT_MASK);
    }
    /* their shadows (subtract), under the sprites */
    if (a.Shadow())
        for (int i = 0; i < l.ZombieCount(); ++i) {
            const Zombie &z = l.ZombieAt(i);
            blit(cv, *a.Shadow(), (int)z.x - 23, FeetY(z.row) - 6, BLIT_SUBTRACT);
        }
    /* plant sprites; the two growing kinds breathe one pixel off-beat */
    const int beat = (l.Tick() >> 5) & 1;
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c) {
            const int pl = l.PlantAt(c, r);
            if (pl == PL_NONE) continue;
            WBitmap *g = a.PlantHp(pl, l.PlantHP(c, r));
            if (g) {
                const int bob = (pl != PL_WALLNUT) ? beat : 0;
                blit(cv, *g, CellCX(c) - Lawn::PlantW(pl) / 2,
                     FeetY(r) - Lawn::PlantH(pl) - bob, BLIT_MASK);
            }
        }
    /* their shadows */
    if (a.Shadow())
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c)
                if (l.PlantAt(c, r) != PL_NONE)
                    blit(cv, *a.Shadow(), CellCX(c) - 23, FeetY(r) - 8, BLIT_SUBTRACT);
    /* the house */
    if (a.House()) blit(cv, *a.House(), 0, 0, BLIT_MASK);
    /* the lawn -- one blit, submitted last so it is the bottom layer */
    if (a.Background()) blit(cv, *a.Background(), 0, 0, BLIT_COPY);
}

void Renderer::drawHud(WCanvas &cv, const Lawn &l, const Atlas &a)
{
    /* sun bank: contents first (they sit on top), the wooden plaque, and the
       dark rim submitted last so it lands at the bottom */
    if (a.Sun()) blit(cv, *a.Sun(), 14, 22, BLIT_MASK);
    drawNum(cv, a, l.Sun(), 46, 28);
    box(cv, 10, 14, 100, 42, UI_WOOD, BLIT_COPY);
    box(cv, 10, 14, 100, 3, UI_WOOD_HI, BLIT_COPY);
    box(cv, 8, 12, 104, 46, 0x241C14, BLIT_COPY);

    /* seed packets, with a two-layer ring under the selected one */
    for (int i = 0; i < PL_COUNT - 1; ++i) {
        WBitmap *pk = a.Seed(i);
        if (!pk) continue;
        const int sx = SEED_X0 + i * (SEED_W + SEED_GAP);
        const int sy = SEED_Y;
        blit(cv, *pk, sx, sy, BLIT_MASK);          /* packet on top ... */
        if (i == l.Sel() - 1) {                    /* ... rings underneath */
            box(cv, sx - 5, sy - 5, SEED_W + 10, SEED_H + 10, 0x241C14, BLIT_COPY);
            box(cv, sx - 2, sy - 2, SEED_W + 4, SEED_H + 4, UI_SEL, BLIT_COPY);
        }
    }

    /* wave read-out, top-right: a wooden chip, text on top of it */
    const int tw = a.LabelW(PVZ_LBL_LEFT);
    const int nw = measureNum(a, l.ZombiesLeft());
    const int right = FIELD_W - 12;
    drawText(cv, a, PVZ_LBL_LEFT, right - nw - tw - 6, 24);
    drawNum(cv, a, l.ZombiesLeft(), right - nw, 24);
    box(cv, right - tw - nw - 12, 22, tw + nw + 10, 22, UI_WOOD, BLIT_COPY);
    box(cv, right - tw - nw - 15, 19, tw + nw + 16, 28, 0x241C14, BLIT_COPY);
}

void Renderer::drawEnd(WCanvas &cv, const Lawn &l, const Atlas &a)
{
    const int win = (l.Phase() == PHASE_WIN);
    WBitmap *pq = a.Plaque(win ? 1 : 2);
    const int pw = pq ? pq->GetW() : 460;
    const int ph = pq ? pq->GetH() : 88;
    const int px = (FIELD_W - pw) / 2, py = 196;
    /* text on top, the plaque behind it, then two frames under the plaque:
       the boxes come last so they land at the bottom of the stack */
    drawTextC(cv, a, win ? PVZ_LBL_WIN : PVZ_LBL_LOSE, FIELD_W / 2, py + 18);
    drawTextC(cv, a, PVZ_LBL_RESTART, FIELD_W / 2, py + ph + 24);
    if (pq) blit(cv, *pq, px, py, BLIT_MASK);
    box(cv, px - 4, py - 4, pw + 8, ph + 8, UI_GOLD, BLIT_COPY);
    box(cv, px - 7, py - 7, pw + 14, ph + 14, 0x241C14, BLIT_COPY);
    ensureDim();
    drawDim(cv, 3);
}

/* --------------------------------------------------------------- the frame */

void Renderer::Draw(WCanvas &cv, const Lawn &l, const Atlas &a)
{
    m_count = 0;
    if (l.Phase() != PHASE_PLAY) drawEnd(cv, l, a);   /* top: the modal */
    drawHud(cv, l, a);                                 /* the seed bar */
    drawScene(cv, l, a);                               /* the world */
}

/* --------------------------------------------------------------- the title */

void Renderer::DrawTitle(WCanvas &cv, const Atlas &a, unsigned tick, int sel)
{
    m_count = 0;
    WBitmap *pq = a.Plaque(0);
    const int pw = pq ? pq->GetW() : 520;
    const int ph = pq ? pq->GetH() : 96;
    const int px = (FIELD_W - pw) / 2, py = 132;

    /* ---- on top: the menu, the title and the subtitle ---- */
    drawTextC(cv, a, PVZ_LBL_TITLE,   FIELD_W / 2, py + 12);
    drawTextC(cv, a, PVZ_LBL_SUBTITLE, FIELD_W / 2, py + ph - 36);
    const int my0 = py + ph + 38;
    const int ids[2] = { PVZ_LBL_START, PVZ_LBL_HOWTO };
    for (int i = 0; i < 2; ++i) {
        const int lw = a.LabelW(ids[i]);
        const int cx = FIELD_W / 2;
        drawText(cv, a, ids[i], cx - lw / 2, my0 + i * 36);
        if (i == sel && a.Sun())
            blit(cv, *a.Sun(), cx - lw / 2 - 32, my0 + i * 36 - 3, BLIT_MASK);
        if (i == sel) {                              /* a breathing underline */
            const int full = lw + 24;
            const int wid  = full - (wobble((int)(tick / 3), 8) + 8);
            box(cv, cx - wid / 2, my0 + i * 36 + 26, wid, 3, UI_SEL, BLIT_COPY);
        }
    }
    drawTextC(cv, a, PVZ_LBL_PRESS, FIELD_W / 2, FIELD_H - 34);

    /* ---- the weather: one drifting cloud, a second one slower ---- */
    if (a.Cloud()) {
        const int cw = a.Cloud()->GetW();
        int x1 = (int)(tick * 0.35f) % (FIELD_W + cw * 2) - cw;
        int x2 = (int)(tick * 0.18f) % (FIELD_W + cw * 2) - cw;
        if (x1 < -cw) x1 += FIELD_W + cw * 2;
        if (x2 < -cw) x2 += FIELD_W + cw * 2;
        blit(cv, *a.Cloud(), x2, 92, BLIT_MASK);
        blit(cv, *a.Cloud(), x1, 44, BLIT_MASK);
    }
    /* ---- lazy motes drifting up the screen ---- */
    if (a.Mote())
        for (int i = 0; i < 6; ++i) {
            const int span = 460;
            const int yy = 560 - (int)((tick / 2 + i * 90) % (unsigned)span);
            const int xx = (110 + i * 118 + wobble((int)(tick / 4 + i * 7), 26)) %
                           (FIELD_W - 40) + 20;
            if (yy > 8 && yy < 560) blit(cv, *a.Mote(), xx, yy, BLIT_MASK);
        }

    /* the plaque behind the text, then the dim above the world actors */
    if (pq) blit(cv, *pq, px, py, BLIT_MASK);
    ensureDim();
    drawDim(cv, 2);

    /* a peashooter, a sunflower and a zombie standing on the lawn */
    if (a.Plant(PL_PEASHOOTER))
        blit(cv, *a.Plant(PL_PEASHOOTER), CellCX(2) - Lawn::PlantW(PL_PEASHOOTER) / 2,
             FeetY(1) - Lawn::PlantH(PL_PEASHOOTER), BLIT_MASK);
    if (a.Plant(PL_SUNFLOWER))
        blit(cv, *a.Plant(PL_SUNFLOWER), CellCX(1) - Lawn::PlantW(PL_SUNFLOWER) / 2,
             FeetY(3) - Lawn::PlantH(PL_SUNFLOWER), BLIT_MASK);
    if (a.Zombie(0, 0))
        blit(cv, *a.Zombie(0, 0), 690, FeetY(2) - ZOMBIE_H + 2, BLIT_MASK);

    if (a.TitleBg()) blit(cv, *a.TitleBg(), 0, 0, BLIT_COPY);
}

/* --------------------------------------------------------------- the howto */

void Renderer::DrawHowto(WCanvas &cv, const Atlas &a)
{
    m_count = 0;
    /* ---- on top: the text and the seed-packet legend ---- */
    drawTextC(cv, a, PVZ_LBL_HOWTO, FIELD_W / 2, 100);
    const int tips[3] = { PVZ_LBL_TIP1, PVZ_LBL_TIP2, PVZ_LBL_TIP3 };
    for (int i = 0; i < 3; ++i) drawText(cv, a, tips[i], 96, 154 + i * 42);
    const int names[3] = { PVZ_LBL_PEASH, PVZ_LBL_SUNFL, PVZ_LBL_WALLN };
    for (int i = 0; i < 3; ++i) {
        WBitmap *pk = a.Seed(i);
        const int cx = 150 + i * 200;
        if (pk) blit(cv, *pk, cx - SEED_W / 2, 300, BLIT_MASK);
        drawTextC(cv, a, names[i], cx, 390);
    }
    drawTextC(cv, a, PVZ_LBL_ZOMB, 150 + 2 * 200, 268);
    drawText(cv, a, PVZ_LBL_BACK, 600, FIELD_H - 116);

    /* the dark panel behind all of it, with a darker frame as its edge */
    box(cv, 60, 88, FIELD_W - 120, FIELD_H - 150, UI_PANEL, BLIT_COPY);
    box(cv, 56, 84, FIELD_W - 112, FIELD_H - 142, UI_PANEL_E, BLIT_COPY);
    ensureDim();
    drawDim(cv, 2);
    if (a.TitleBg()) blit(cv, *a.TitleBg(), 0, 0, BLIT_COPY);
}

} /* namespace pvz */
