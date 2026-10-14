/* ===========================================================================
 *  gf_render.cpp -- Guofeng : renderer
 *  See gf_render.h for the draw-order contract, which is the whole reason
 *  this file reads the way it does.
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

#include "gf_render.h"
#include "gf_art.h"

namespace gf {

/* ------------------------------------------------------------------ chrome */
/*  Ink and lacquer rather than the blue-grey of the other prototype: a warm
 *  near-black panel, a dim gold keyline and a jade inner one. */
static const unsigned long UI_FILL = 0x16201F;
static const unsigned long UI_LIT  = 0x3E5C50;
static const unsigned long UI_SHD  = 0x080E0E;

/* The hero sprite is anchored on his feet, six pixels above the box bottom so
 * the hem has somewhere to land. */
static const int HERO_FOOT = HERO_H - 6;

/* Thumbnail scales, matching Atlas::Build(). */
static const int MINI_SHIFT = 16;       /* 1/16 -> 128x64 for the HUD       */
static const int BIG_SHIFT  = 4;        /* 1/4  -> 512x256 for the M screen */

/* ------------------------------------------------------------------ basics */

Renderer::Renderer() : m_count(0), m_last(0), m_showMap(0), m_glow(1), m_dim(0)
{
}

Renderer::~Renderer()
{
    delete m_dim;
}

void Renderer::Reset()
{
    m_count = 0;
}

void Renderer::blit(WCanvas &cv, WBitmap &b, int x, int y, int style)
{
    cv.SetPos(x, y);
    cv.Draw(b, (DWORD)style);
    ++m_count;
}

int Renderer::textW(const Atlas &a, int id) const
{
    return a.LabelW(id);
}

void Renderer::drawStr(WCanvas &cv, const Atlas &a, const char *s, int x, int y)
{
    /* Character by character out of the baked atlas rather than through
       WCanvas::Draw(const char *) -- see Atlas::Ascii() for why the two must
       not be mixed. */
    for (const char *p = s; *p; ++p) {
        WBitmap *b = a.Ascii((unsigned char)*p);
        if (!b) continue;
        blit(cv, *b, x, y, BLIT_MASK);
        x += b->GetW();
    }
}

int Renderer::drawNum(WCanvas &cv, const Atlas &a, int value, int x, int y)
{
    /* Every read-out in this prototype is a coordinate or a count, so a
       negative would mean something has gone wrong rather than something to
       print.  Clamping keeps the string to characters the atlas holds. */
    if (value < 0) value = 0;

    char buf[16];
    sprintf(buf, "%d", value);

    const int start = x;
    drawStr(cv, a, buf, x, y);
    for (const char *p = buf; *p; ++p) {
        WBitmap *b = a.Ascii((unsigned char)*p);
        if (b) x += b->GetW();
    }
    return x - start;
}

void Renderer::drawText(WCanvas &cv, const Atlas &a, int id, int x, int y)
{
    WBitmap *b = a.Label(id);
    if (b) blit(cv, *b, x, y, BLIT_MASK);
}

void Renderer::drawTextC(WCanvas &cv, const Atlas &a, int id, int cx, int y)
{
    WBitmap *b = a.Label(id);
    if (b) blit(cv, *b, cx - b->GetW() / 2, y, BLIT_MASK);
}

void Renderer::drawPanel(WCanvas &cv, const Atlas &a, int x, int y, int w, int h,
                         int ornate)
{
    if (w < 12 || h < 12) return;

    /* Submitted first -> painted last -> on top.  A corner scroll overlaps
       the keyline, so it has to come after it. */
    if (ornate) {
        blit(cv, *a.Corner(0), x + 2,       y + 2,       BLIT_MASK);
        blit(cv, *a.Corner(1), x + w - 22,  y + 2,       BLIT_MASK);
        blit(cv, *a.Corner(2), x + 2,       y + h - 22,  BLIT_MASK);
        blit(cv, *a.Corner(3), x + w - 22,  y + h - 22,  BLIT_MASK);
    }

    /* The lit and shaded inner edges sit INSIDE the fill, so they have to be
       submitted before it. */
    cv.SetPos(x + 1, y + 1);
    cv.Draw(WBox(w - 2, 1, WPixel((DWORD)UI_LIT)), BLIT_COPY); ++m_count;
    cv.SetPos(x + 1, y + h - 2);
    cv.Draw(WBox(w - 2, 1, WPixel((DWORD)UI_SHD)), BLIT_COPY); ++m_count;

    /* the jade inner ring */
    cv.SetPos(x + 2, y + 2);          cv.Draw(WBox(w - 4, 1, WPixel((DWORD)COL_JADE_DIM)), BLIT_COPY); ++m_count;
    cv.SetPos(x + 2, y + h - 3);      cv.Draw(WBox(w - 4, 1, WPixel((DWORD)COL_JADE_DIM)), BLIT_COPY); ++m_count;
    cv.SetPos(x + 2, y + 2);          cv.Draw(WBox(1, h - 4, WPixel((DWORD)COL_JADE_DIM)), BLIT_COPY); ++m_count;
    cv.SetPos(x + w - 3, y + 2);      cv.Draw(WBox(1, h - 4, WPixel((DWORD)COL_JADE_DIM)), BLIT_COPY); ++m_count;

    /* the gold outer keyline */
    cv.SetPos(x, y);                  cv.Draw(WBox(w, 1, WPixel((DWORD)COL_GOLD_DIM)), BLIT_COPY); ++m_count;
    cv.SetPos(x, y + h - 1);          cv.Draw(WBox(w, 1, WPixel((DWORD)COL_GOLD_DIM)), BLIT_COPY); ++m_count;
    cv.SetPos(x, y);                  cv.Draw(WBox(1, h, WPixel((DWORD)COL_GOLD_DIM)), BLIT_COPY); ++m_count;
    cv.SetPos(x + w - 1, y);          cv.Draw(WBox(1, h, WPixel((DWORD)COL_GOLD_DIM)), BLIT_COPY); ++m_count;

    /* the body fill, LAST: the bottom layer of this group */
    cv.SetPos(x + 1, y + 1);
    cv.Draw(WBox(w - 2, h - 2, WPixel((DWORD)UI_FILL)), BLIT_COPY); ++m_count;
}

void Renderer::drawShadow(WCanvas &cv, int x, int y, int w, int h, int off)
{
    /* Submitted after its panel, so it lands underneath: the overlap vanishes
       and only the offset band is left showing. */
    cv.SetPos(x + off, y + off);
    cv.Draw(WBox(w, h, WPixel(0x05080A)), BLIT_COPY); ++m_count;
}

void Renderer::drawRule(WCanvas &cv, const Atlas &a, int x, int y, int w)
{
    /* the cloud flourish first, so it lands on top of the line it sits on */
    WBitmap *c = a.Cloud(1);
    if (c && w > 60) blit(cv, *c, x + w / 2 - c->GetW() / 2, y - c->GetH() / 2, BLIT_MASK);

    cv.SetPos(x, y);      cv.Draw(WBox(w, 1, WPixel((DWORD)COL_GOLD_DIM)), BLIT_COPY); ++m_count;
    cv.SetPos(x + 4, y + 1); cv.Draw(WBox(w - 8, 1, WPixel(0x2A3A34)), BLIT_COPY); ++m_count;
}

void Renderer::drawButton(WCanvas &cv, const Atlas &a, int x, int y, int w, int h,
                          int id, int live)
{
    /* The label is submitted FIRST so it ends up on top of the plate. */
    drawTextC(cv, a, id, x + w / 2, y + (h - 20) / 2);

    cv.SetPos(x, y);          cv.Draw(WBox(w, 1, WPixel((DWORD)(live ? COL_GOLD_LIT : COL_GOLD_DIM))), BLIT_COPY); ++m_count;
    cv.SetPos(x, y + h - 1);  cv.Draw(WBox(w, 1, WPixel((DWORD)COL_GOLD_DIM)), BLIT_COPY); ++m_count;
    cv.SetPos(x, y);          cv.Draw(WBox(1, h, WPixel((DWORD)COL_GOLD_DIM)), BLIT_COPY); ++m_count;
    cv.SetPos(x + w - 1, y);  cv.Draw(WBox(1, h, WPixel((DWORD)COL_GOLD_DIM)), BLIT_COPY); ++m_count;

    /* the plate, last, so it goes underneath the label */
    cv.SetPos(x + 1, y + 1);
    cv.Draw(WBox(w - 2, h - 2, WPixel((DWORD)(live ? 0x2A3E34 : 0x18211F))), BLIT_COPY); ++m_count;

    if (live) {
        /* a lit inner edge only on the selected item -- the cheapest way to
           make a menu selection readable without an arrow */
        cv.SetPos(x + 1, y + 1);
        cv.Draw(WBox(w - 2, 1, WPixel((DWORD)COL_JADE)), BLIT_COPY); ++m_count;
    }
}

/* ------------------------------------------------------------------- wash */

void Renderer::ensureDim()
{
    if (m_dim) return;

    m_dim = new WBitmap();
    m_dim->Create(FIELD_W, FIELD_H);

    /* A flat sheet.  Deepening is done by blitting it again, which is why a
       single value is enough here. */
    for (int y = 0; y < FIELD_H; ++y)
        for (int x = 0; x < FIELD_W; ++x)
            (*m_dim)[y][x] = WPixel((DWORD)0x1A2028);
}

void Renderer::drawDim(WCanvas &cv, int passes)
{
    ensureDim();
    for (int i = 0; i < passes; ++i)
        blit(cv, *m_dim, 0, 0, BLIT_SUBTRACT);
}

/* ------------------------------------------------------------------ world */

void Renderer::drawVignette(WCanvas &cv, const Atlas &a)
{
    WBitmap *v = a.Vignette();
    if (v) blit(cv, *v, 0, 0, BLIT_SUBTRACT);
}

/*  Every decoration and the actor, submitted NEAREST FIRST.
 *
 *  There is no sort here on purpose.  Depth is tx + ty, so walking the
 *  anti-diagonals from the far corner to the near one IS the sort -- O(63)
 *  buckets over a 32x32 map, no array, no qsort, and the order is exactly the
 *  one a stable sort would produce anyway. */
void Renderer::drawEntities(WCanvas &cv, const Scene &s, const Atlas &a,
                            int camX, int camY)
{
    const Hero &h = s.H();

    /* the hero's own depth, fractional: standing between two tiles he is
       nearer than the integer row he is standing on */
    int heroDepth = (int)floorf(h.x + h.y);
    if (heroDepth < 0) heroDepth = 0;
    if (heroDepth > MAP_W + MAP_H - 2) heroDepth = MAP_W + MAP_H - 2;

    const int hx = s.HeroSX() - camX;
    const int hy = s.HeroSY() - camY;

    /* the lantern flicker, shared by every glow in the frame */
    const int glowFrame = (int)((s.Tick() / 6u) % (unsigned)GLOW_FRAMES);

    for (int d = MAP_W + MAP_H - 2; d >= 0; --d) {
        /* The hero goes first in his bucket: within one depth his fractional
           position puts him at least as near as anything on the row. */
        if (d == heroDepth) {
            WBitmap *sh = a.Shadow(0);
            WBitmap *sp = a.Hero(h.face, h.frame);
            if (sp) blit(cv, *sp, hx - HERO_W / 2, hy - HERO_FOOT, BLIT_MASK);
            /* the shadow is submitted AFTER the sprite so it lands UNDER it */
            if (sh) blit(cv, *sh, hx - sh->GetW() / 2, hy - sh->GetH() / 2 + 1,
                         BLIT_SUBTRACT);
        }

        for (int ty = 0; ty < MAP_H; ++ty) {
            const int tx = d - ty;
            if (tx < 0 || tx >= MAP_W) continue;

            const int kind = (int)s.DecoAt(tx, ty);
            if (kind == D_NONE) continue;

            WBitmap *sp = a.Deco(kind);
            if (!sp) continue;

            int sx, sy;
            IsoToScreen(tx, ty, sx, sy);
            sx -= camX;
            sy -= camY;

            const int ox = sx - a.DecoAnchorX(kind);
            const int oy = sy - a.DecoAnchorY(kind);

            /* cull on the sprite box, not the tile: a bamboo reaches 112 px
               above its tile and a house is 112 wide, so a tile-level test
               pops them in and out along the screen edge */
            if (ox + a.DecoW(kind) <= 0 || ox >= FIELD_W) continue;
            if (oy + a.DecoH(kind) <= 0 || oy >= FIELD_H) continue;

            /* the lantern's glow is submitted FIRST so it lands on TOP of the
               sprite: additive light is in the air in front of the lamp, and
               putting it behind would just tint the post */
            if (kind == D_LANTERN && m_glow) {
                WBitmap *gl = a.Glow(glowFrame);
                if (gl) blit(cv, *gl, sx + 5 - gl->GetW() / 2, sy - 23 - gl->GetH() / 2,
                             BLIT_ADDITIVE);
            }

            blit(cv, *sp, ox, oy, BLIT_MASK);

            const int tier = (kind == D_HOUSE) ? 2 : 1;
            WBitmap *sh = a.Shadow(tier);
            if (sh) blit(cv, *sh, sx - sh->GetW() / 2, sy - sh->GetH() / 2 + 2,
                         BLIT_SUBTRACT);
        }
    }
}

void Renderer::drawScene(WCanvas &cv, const Scene &s, const Atlas &a,
                         int camX, int camY)
{
    /* Submitted before the world so it lands on top of it: the vignette is
       meant to darken the picture, not to be hidden by it. */
    drawVignette(cv, a);

    drawEntities(cv, s, a, camX, camY);

    /* One blit for the entire terrain.  See gf_art.h: a thousand diamonds at
       five links each would be five thousand draw calls a frame. */
    WBitmap *g = a.Ground();
    if (g) blit(cv, *g, a.GroundX() - camX, a.GroundY() - camY, BLIT_COPY);
}

/* --------------------------------------------------------------------- HUD */

void Renderer::drawMinimap(WCanvas &cv, const Scene &s, const Atlas &a,
                           int x, int y)
{
    WBitmap *m = a.Mini();
    if (!m) return;

    const int mw = a.MiniW(), mh = a.MiniH();

    /* the markers first: submitted before the thumbnail so they end up over
       it rather than under it */
    const int hpx = x + 12 + (s.HeroSX() - a.GroundX()) / MINI_SHIFT;
    const int hpy = y + 12 + (s.HeroSY() - a.GroundY()) / MINI_SHIFT;
    cv.SetPos(hpx - 2, hpy - 2);
    cv.Draw(WBox(5, 5, WPixel((DWORD)COL_GOLD_LIT)), BLIT_COPY); ++m_count;
    cv.SetPos(hpx, hpy);
    cv.Draw(WBox(1, 1, WPixel((DWORD)COL_CINNABAR)), BLIT_COPY); ++m_count;

    const int bx = x + 12 + (s.CamX() - a.GroundX()) / MINI_SHIFT;
    const int by = y + 12 + (s.CamY() - a.GroundY()) / MINI_SHIFT;
    const int bw = FIELD_W / MINI_SHIFT, bh = FIELD_H / MINI_SHIFT;
    cv.SetPos(bx, by);              cv.Draw(WBox(bw, 1, WPixel((DWORD)COL_SILK)), BLIT_COPY); ++m_count;
    cv.SetPos(bx, by + bh - 1);     cv.Draw(WBox(bw, 1, WPixel((DWORD)COL_SILK)), BLIT_COPY); ++m_count;
    cv.SetPos(bx, by);              cv.Draw(WBox(1, bh, WPixel((DWORD)COL_SILK)), BLIT_COPY); ++m_count;
    cv.SetPos(bx + bw - 1, by);     cv.Draw(WBox(1, bh, WPixel((DWORD)COL_SILK)), BLIT_COPY); ++m_count;

    blit(cv, *m, x + 12, y + 12, BLIT_COPY);

    /* The panel is submitted last of all.  Its fill goes underneath the
       thumbnail, which is already down -- the interior is opaque, so the fill
       is only ever visible in the 12 px margin. */
    drawPanel(cv, a, x, y, mw + 24, mh + 24, 0);
}

void Renderer::drawHud(WCanvas &cv, const Scene &s, const Atlas &a)
{
    const Hero &h = s.H();

    /* ---- top left: the name board and the read-outs --------------------- */
    {
        const int px = 10, py = 10, pw = 216, ph = 94;

        drawText(cv, a, GF_LBL_TITLE, px + 14, py + 10);
        drawRule(cv, a, px + 12, py + 36, pw - 24);

        drawText(cv, a, GF_LBL_POS, px + 14, py + 46);
        {
            /* Both coordinates come from the baked numerals, with a jade dot
               standing in for the comma -- the built-in ASCII font would set
               these two numbers in a different typeface from the label they
               sit beside. */
            const int nx = px + 14 + textW(a, GF_LBL_POS) + 8;
            const int used = drawNum(cv, a, (int)floorf(h.x), nx, py + 46);

            cv.SetPos(nx + used + 1, py + 46 + 7);
            cv.Draw(WBox(1, 1, WPixel((DWORD)COL_JADE)), BLIT_COPY); ++m_count;

            drawNum(cv, a, (int)floorf(h.y), nx + used + 4, py + 46);
        }

        drawText(cv, a, GF_LBL_DRAWS, px + 14, py + 68);
        drawNum(cv, a, m_last, px + 14 + textW(a, GF_LBL_DRAWS) + 8, py + 68);

        /* The panel goes down BEFORE its shadow.  Submitted first is painted
           last, so the panel ends up over the shadow's overlap and only the
           three pixel offset band is left showing -- which is the whole point
           of the offset.  The other way round the shadow is painted on top of
           the panel's fill and the whole plate goes black. */
        drawPanel(cv, a, px, py, pw, ph, 1);
        drawShadow(cv, px, py, pw, ph, 3);
    }

    /* ---- the seal, top right ------------------------------------------- */
    {
        WBitmap *seal = a.Seal();
        if (seal) blit(cv, *seal, FIELD_W - 14 - seal->GetW(), 12, BLIT_COPY);
    }

    /* ---- bottom right: the minimap ------------------------------------- */
    drawMinimap(cv, s, a, FIELD_W - 12 - (a.MiniW() + 24), FIELD_H - 12 - (a.MiniH() + 24));

    /* ---- bottom left: the controls ------------------------------------- */
    {
        const int pw = 540, ph = 38;
        const int px = 10, py = FIELD_H - 12 - ph;

        drawText(cv, a, GF_LBL_HINT, px + 14, py + 10);
        drawPanel(cv, a, px, py, pw, ph, 0);
        drawShadow(cv, px, py, pw, ph, 3);
    }
}

/* --------------------------------------------------------------- M screen */

void Renderer::drawBigMap(WCanvas &cv, const Scene &s, const Atlas &a)
{
    WBitmap *m = a.Big();
    if (!m) return;

    const int mw = a.BigW(), mh = a.BigH();
    const int pw = mw + 48, ph = mh + 80;
    const int px = (FIELD_W - pw) / 2, py = (FIELD_H - ph) / 2;
    const int mx = px + 24, my = py + 48;

    /* ---- markers, submitted first so they land on top of the map -------- */
    {
        const int hpx = mx + (s.HeroSX() - a.GroundX()) / BIG_SHIFT;
        const int hpy = my + (s.HeroSY() - a.GroundY()) / BIG_SHIFT;

        cv.SetPos(hpx - 3, hpy - 3);
        cv.Draw(WBox(7, 7, WPixel((DWORD)COL_CINNABAR)), BLIT_COPY); ++m_count;
        cv.SetPos(hpx - 1, hpy - 1);
        cv.Draw(WBox(3, 3, WPixel((DWORD)COL_GOLD_LIT)), BLIT_COPY); ++m_count;

        const int bx = mx + (s.CamX() - a.GroundX()) / BIG_SHIFT;
        const int by = my + (s.CamY() - a.GroundY()) / BIG_SHIFT;
        const int bw = FIELD_W / BIG_SHIFT, bh = FIELD_H / BIG_SHIFT;
        cv.SetPos(bx, by);              cv.Draw(WBox(bw, 1, WPixel((DWORD)COL_SILK)), BLIT_COPY); ++m_count;
        cv.SetPos(bx, by + bh - 1);     cv.Draw(WBox(bw, 1, WPixel((DWORD)COL_SILK)), BLIT_COPY); ++m_count;
        cv.SetPos(bx, by);              cv.Draw(WBox(1, bh, WPixel((DWORD)COL_SILK)), BLIT_COPY); ++m_count;
        cv.SetPos(bx + bw - 1, by);     cv.Draw(WBox(1, bh, WPixel((DWORD)COL_SILK)), BLIT_COPY); ++m_count;
    }

    blit(cv, *m, mx, my, BLIT_COPY);

    /* ---- the panel's contents, then the panel -------------------------- */
    drawText(cv, a, GF_LBL_MAP, px + 24, py + 14);
    drawText(cv, a, GF_LBL_BACK, px + 24, my + mh + 12);
    drawText(cv, a, GF_LBL_HERO, px + pw - 24 - textW(a, GF_LBL_HERO), my + mh + 12);

    {
        const int hpx = mx + (s.HeroSX() - a.GroundX()) / BIG_SHIFT;
        const int hpy = my + (s.HeroSY() - a.GroundY()) / BIG_SHIFT;
        cv.SetPos(hpx + 8, hpy - 8);
        cv.Draw(WBox(10, 3, WPixel((DWORD)COL_CINNABAR)), BLIT_COPY); ++m_count;
        cv.SetPos(hpx + 10, hpy - 5);
        cv.Draw(WBox(3, 6, WPixel((DWORD)COL_CINNABAR)), BLIT_COPY); ++m_count;
    }

    drawPanel(cv, a, px, py, pw, ph, 1);
    drawShadow(cv, px, py, pw, ph, 4);
}

/* ----------------------------------------------------------- title screen */

void Renderer::drawLegend(WCanvas &cv, const Atlas &a, int x, int y)
{
    /* The real tile bitmaps, side by side.  Not a description of the tileset
       -- the tileset. */
    static const int ids[TILE_COUNT] = {
        GF_LBL_GRASS, GF_LBL_PATH, GF_LBL_PAVE, GF_LBL_STONE, GF_LBL_WATER
    };
    static const int tiles[TILE_COUNT] = {
        T_GRASS, T_DIRT, T_PAVE, T_STONE, T_WATER
    };

    for (int i = 0; i < TILE_COUNT; ++i) {
        const int tx = x + i * (TILE_W + 10);
        WBitmap *t = a.Tile(tiles[i], 0);
        if (t) blit(cv, *t, tx, y, BLIT_MASK);
        drawTextC(cv, a, ids[i], tx + TILE_W / 2, y + TILE_H + 4);
    }
}

void Renderer::drawTitleCard(WCanvas &cv, const Atlas &a, unsigned tick, int sel)
{
    /* ---- the plaque ----------------------------------------------------- */
    {
        WBitmap *p = a.Plaque();
        if (p) blit(cv, *p, (FIELD_W - p->GetW()) / 2, 92, BLIT_MASK);
    }

    /* ---- the menu ------------------------------------------------------- */
    {
        static const int items[3] = { GF_LBL_PRESS, GF_LBL_HOWTO, GF_LBL_QUIT };
        const int bw = 240, bh = 44, gap = 14;
        const int bx = (FIELD_W - bw) / 2;
        int by = 268;

        for (int i = 0; i < 3; ++i) {
            drawButton(cv, a, bx, by, bw, bh, items[i], i == sel);
            by += bh + gap;
        }
    }

    /* ---- the seal and the credits line ---------------------------------- */
    {
        WBitmap *seal = a.Seal();
        if (seal) blit(cv, *seal, FIELD_W - 26 - seal->GetW(), FIELD_H - 26 - seal->GetH(), BLIT_COPY);
        drawTextC(cv, a, GF_LBL_SUBTITLE, FIELD_W / 2, FIELD_H - 44);
    }

    /* The wash goes down LAST inside this group, i.e. directly above the
       world: the plaque and the menu are submitted before it and so paint
       over it.  Two passes rather than three -- three takes the grass below
       its own value in the darker channels and the village stops reading as a
       place, which is the opposite of what a title screen wants. */
    drawDim(cv, 2);
}

/* --------------------------------------------------------------- screens */

void Renderer::Draw(WCanvas &cv, const Scene &s, const Atlas &a)
{
    m_count = 0;

    if (m_showMap) {
        /* Panel first, wash second, world last: paint order is world -> wash
           -> panel.  The HUD is skipped entirely -- it would show through
           around the panel and the read-outs are meaningless with the map
           up. */
        drawBigMap(cv, s, a);
        drawDim(cv, 3);
    } else {
        drawHud(cv, s, a);
    }

    drawScene(cv, s, a, s.CamX(), s.CamY());

    /* Remembered for the HUD's read-out on the NEXT frame -- see LastCount(). */
    m_last = m_count;
}

void Renderer::DrawTitle(WCanvas &cv, const Scene &s, const Atlas &a,
                         unsigned tick, int sel)
{
    m_count = 0;

    /* A slow drift across the village, so the title screen shows the thing
       the prototype is actually about rather than a static card. */
    const int driftX = (int)(sinf((float)tick * 0.0075f) * 54.0f);
    const int driftY = (int)(cosf((float)tick * 0.0043f) * 26.0f);

    drawTitleCard(cv, a, tick, sel);
    drawScene(cv, s, a, s.CamX() + driftX, s.CamY() + driftY);
}

void Renderer::DrawHowto(WCanvas &cv, const Scene &s, const Atlas &a)
{
    m_count = 0;

    /* Sized to the content: the last row of names ends at py+392, so a 520 px
       panel would leave the bottom fifth of the page empty, which reads as an
       unfinished screen rather than as generous margins. */
    const int pw = 720, ph = 460;
    const int px = (FIELD_W - pw) / 2, py = (FIELD_H - ph) / 2;

    drawText(cv, a, GF_LBL_HOWTO, px + 26, py + 18);
    drawRule(cv, a, px + 24, py + 48, pw - 48);

    /* the controls: a label on the left, the keys on the right */
    {
        static const struct { int id; const char *keys; } rows[5] = {
            { GF_LBL_MOVE,  "W  A  S  D" },
            { GF_LBL_RUN,   "SHIFT"      },
            { GF_LBL_MAP,   "M"          },
            { GF_LBL_LIGHT, "SPACE"      },
            { GF_LBL_QUIT,  "ESC"        }
        };
        for (int i = 0; i < 5; ++i) {
            const int y = py + 64 + i * 28;
            drawText(cv, a, rows[i].id, px + 30, y);
            drawStr(cv, a, rows[i].keys, px + 180, y + 2);
        }
    }

    drawRule(cv, a, px + 24, py + 216, pw - 48);

    drawText(cv, a, GF_LBL_LEGEND, px + 26, py + 228);
    drawLegend(cv, a, px + 30, py + 256);

    /* what the prototype is demonstrating */
    {
        static const int ids[4] = { GF_LBL_ISO, GF_LBL_DEPTH, GF_LBL_BLEND, GF_LBL_LIGHT };
        for (int i = 0; i < 4; ++i)
            drawText(cv, a, ids[i], px + 30 + i * 172, py + 340);
    }

    drawText(cv, a, GF_LBL_BAMBOO, px + 30, py + 372);
    drawText(cv, a, GF_LBL_PINE,   px + 90, py + 372);
    drawText(cv, a, GF_LBL_ROCK,   px + 150, py + 372);
    drawText(cv, a, GF_LBL_LANTERN,px + 210, py + 372);
    drawText(cv, a, GF_LBL_HOUSE,  px + 270, py + 372);
    drawText(cv, a, GF_LBL_WELL,   px + 330, py + 372);
    drawText(cv, a, GF_LBL_POND,   px + 390, py + 372);
    drawText(cv, a, GF_LBL_VILLAGE,px + 450, py + 372);

    drawTextC(cv, a, GF_LBL_PRESS, px + pw / 2, py + ph - 40);

    drawPanel(cv, a, px, py, pw, ph, 1);
    drawShadow(cv, px, py, pw, ph, 4);

    /* The wash and then the world, LAST -- submitted last is painted first,
       so the stack comes out world, wash, panel.  Putting the wash first
       instead would paint it over the panel and every plate on the page would
       go black, which is exactly what it looked like before this line moved. */
    drawDim(cv, 2);
    drawScene(cv, s, a, s.CamX(), s.CamY());
}

} /* namespace gf */
