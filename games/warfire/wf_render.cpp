/* ===========================================================================
 *  wf_render.cpp -- Warfire : renderer
 *  See wf_render.h for the draw-order contract.
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

#include "wf_render.h"
#include "wf_art.h"

namespace wf {

/* ------------------------------------------------------------------ utils */

void Renderer::blit(WCanvas &cv, WBitmap &bmp, int sx, int sy, int style)
{
    cv.SetPos(sx, sy);
    cv.Draw(bmp, (DWORD)style);
    ++m_count;
}

/* The shadow is a black-ramped ellipse blitted with BLIT_SUBTRACT, so it only
   ever takes light away -- never adds a colour of its own. */
void Renderer::blitShadow(WCanvas &cv, const Atlas &atlas, int tier,
                          int cx, int footY)
{
    WBitmap *s = atlas.Shadow(tier);
    if (!s) return;
    blit(cv, *s, cx - s->GetW() / 2, footY - s->GetH() / 2, BLIT_SUBTRACT);
}

void Renderer::blitCentered(WCanvas &cv, WBitmap &bmp, int cx, int y, int style)
{
    blit(cv, bmp, cx - bmp.GetW() / 2, y, style);
}

/* ==========================================================================
 *  shared UI chrome
 *
 *  One visual language for all five screens, in one place so they cannot
 *  drift apart.  See wf_render.h for what each of these is for.
 * ========================================================================*/

/* Mix two 0xRRGGBB colours; t = 0 gives a, t = 255 gives b. */
static unsigned long mixColor(unsigned long a, unsigned long b, int t)
{
    if (t < 0) t = 0;
    if (t > 255) t = 255;
    int ar = (int)((a >> 16) & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)(a & 0xFF);
    int br = (int)((b >> 16) & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)(b & 0xFF);
    int r = (ar * (255 - t) + br * t) / 255;
    int g = (ag * (255 - t) + bg * t) / 255;
    int l = (ab * (255 - t) + bb * t) / 255;
    return (unsigned long)((r << 16) | (g << 8) | l);
}

/* One rectangle per row, lerping top -> bot, so a surface reads as a tube of
   light instead of a flat stripe of colour.  Returns the number of draws so
   the caller can keep its item count honest. */
static int vGradient(WCanvas &cv, int x, int y, int w, int h,
                     unsigned long top, unsigned long bot)
{
    if (w <= 0 || h <= 0) return 0;
    if (h == 1) {
        cv.SetPos(x, y);
        cv.Draw(WBox(w, 1, WPixel((DWORD)top)), BLIT_COPY);
        return 1;
    }
    for (int i = 0; i < h; ++i) {
        int t = (i * 255) / (h - 1);
        cv.SetPos(x, y + i);
        cv.Draw(WBox(w, 1, WPixel((DWORD)mixColor(top, bot, t))), BLIT_COPY);
    }
    return h;
}

/* A near-black keyline, a lit top inner edge and a shaded bottom inner edge
   over a flat fill.  WCanvas applies the link submitted FIRST last, i.e. on
   top, so the frame goes down before the fill and stays visible over it. */
/* A near-black keyline, a lit top inner edge and a shaded bottom inner edge
   over a flat body.  WCanvas applies the link submitted FIRST last -- i.e. on
   top -- so this is laid down back to front: frame ring, then the body.

   That inverted order is the whole trap here, and it bit twice:
     - the body has to go down LAST inside this function, or the frame would
       be the only thing that survives;
     - and the CALLER has to submit the panel's own contents BEFORE calling
       this, or the body paints straight over them.  Every HUD panel used to
       come out as a blank rectangle for exactly that reason.  */
void Renderer::drawBevel(WCanvas &cv, int x, int y, int w, int h,
                         unsigned long fill, unsigned long key,
                         unsigned long lit, unsigned long shade)
{
    if (w < 6 || h < 6) return;

    cv.SetPos(x, y);         cv.Draw(WBox(w, 1, WPixel((DWORD)key)), BLIT_COPY);
    cv.SetPos(x, y + h - 1); cv.Draw(WBox(w, 1, WPixel((DWORD)key)), BLIT_COPY);
    cv.SetPos(x, y);         cv.Draw(WBox(1, h, WPixel((DWORD)key)), BLIT_COPY);
    cv.SetPos(x + w - 1, y); cv.Draw(WBox(1, h, WPixel((DWORD)key)), BLIT_COPY);

    cv.SetPos(x + 1, y + 1);     cv.Draw(WBox(w - 2, 1, WPixel((DWORD)lit)),   BLIT_COPY);
    cv.SetPos(x + 1, y + h - 2); cv.Draw(WBox(w - 2, 1, WPixel((DWORD)shade)), BLIT_COPY);
    m_count += 6;

    /* Body, submitted last so it ends up under the panel's contents: one
       lighter band across the top third, then the flat rest.  Two rects
       rather than one per row -- the HUD keeps five of these on screen every
       frame and a per-row gradient here would roughly double its fill work
       for a difference nobody can see at one pixel. */
    int ty = y + 2;
    int bh = h - 4;
    int band = bh / 3;
    if (band > 0) {
        cv.SetPos(x + 1, ty);
        cv.Draw(WBox(w - 2, band, WPixel((DWORD)mixColor(fill, lit, 26))), BLIT_COPY);
        ++m_count;
        ty += band;
        bh -= band;
    }
    if (bh > 0) {
        cv.SetPos(x + 1, ty);
        cv.Draw(WBox(w - 2, bh, WPixel((DWORD)fill)), BLIT_COPY);
        ++m_count;
    }
}

/* A gradient tube with quarter ticks and a lit top row.  The trough is
   submitted last so it ends up underneath the fill. */
void Renderer::drawBarEx(WCanvas &cv, int x, int y, int w, int h, int fillW,
                         unsigned long top, unsigned long bot, int ticks)
{
    if (w < 4 || h < 4) return;
    if (fillW < 0) fillW = 0;
    if (fillW > w - 2) fillW = w - 2;

    cv.SetPos(x, y);         cv.Draw(WBox(w, 1, WPixel((DWORD)COL_PANEL_KEY)), BLIT_COPY);
    cv.SetPos(x, y + h - 1); cv.Draw(WBox(w, 1, WPixel((DWORD)COL_PANEL_KEY)), BLIT_COPY);
    cv.SetPos(x, y);         cv.Draw(WBox(1, h, WPixel((DWORD)COL_PANEL_KEY)), BLIT_COPY);
    cv.SetPos(x + w - 1, y); cv.Draw(WBox(1, h, WPixel((DWORD)COL_PANEL_KEY)), BLIT_COPY);
    m_count += 4;

    /* ticks sit above both the fill and the trough, so they read across the
       whole bar; subtracting a near-black leaves the trough unchanged */
    for (int i = 1; i < ticks; ++i) {
        int tx = x + 1 + ((w - 2) * i) / ticks;
        cv.SetPos(tx, y + 1);
        cv.Draw(WBox(1, h - 2, WPixel((DWORD)0x141A22)), BLIT_SUBTRACT);
        ++m_count;
    }

    if (fillW > 0) {
        m_count += vGradient(cv, x + 1, y + 1, fillW, h - 2, top, bot);
        /* a single lit row along the very top of the fill */
        cv.SetPos(x + 1, y + 1);
        cv.Draw(WBox(fillW, 1, WPixel((DWORD)mixColor(top, 0xFFFFFF, 80))), BLIT_COPY);
        ++m_count;
    }

    m_count += vGradient(cv, x + 1, y + 1, w - 2, h - 2,
                         (unsigned long)COL_TROUGH_TOP,
                         (unsigned long)COL_TROUGH_BOT);
}

void Renderer::drawRule(WCanvas &cv, int x, int y, int w, unsigned long col)
{
    cv.SetPos(x, y);
    cv.Draw(WBox(w, 1, WPixel((DWORD)col)), BLIT_COPY);
    ++m_count;
}

/* The "->" glyph next to a before/after pair.  There is no ASCII font in the
   atlas, but the labels ARE font-rendered, so a label carrying the arrow is
   the cheapest way to get the glyph on screen. */
void Renderer::drawArrow(WCanvas &cv, const Atlas &atlas, int x, int y)
{
    blit(cv, *atlas.Label(WF_LBL_ARROW), x, y, BLIT_MASK);
}

/* A coin glyph followed by the amount, laid out left to right.  Callers that
   want it right-aligned compute the width from Coin()->GetW() + 6 +
   numWidth(). */
void Renderer::drawCoin(WCanvas &cv, const Atlas &atlas, int value,
                        int x, int y, int big)
{
    int set = big ? DIGIT_BIG : DIGIT_SMALL;
    WBitmap *c = atlas.Coin();
    int cw = c ? c->GetW() : 0;
    int ch = c ? c->GetH() : 0;
    int dh = atlas.DigitH(set);

    if (c) blit(cv, *c, x, y + (dh - ch) / 2, BLIT_MASK);
    drawNumber(cv, atlas, set, value, x + cw + 6, y, 0);
}

/* Stack pips: one filled dot per level taken, hollow dots for the rest.
   Reads faster than a bare digit and matches how the forge shows its own
   upgrade count. */
void Renderer::drawPips(WCanvas &cv, int cx, int y, int filled, int total,
                        unsigned long col)
{
    if (total < 1) total = 1;
    if (filled > total) filled = total;

    const int D = 7, GAP = 5;
    int x = cx - (total * D + (total - 1) * GAP) / 2;

    for (int i = 0; i < total; ++i) {
        if (i < filled) {
            cv.SetPos(x, y);
            cv.Draw(WBox(D, D, WPixel((DWORD)col)), BLIT_COPY);
            ++m_count;
        } else {
            /* hollow: four 1 px edges */
            cv.SetPos(x, y);         cv.Draw(WBox(D, 1, WPixel((DWORD)col)), BLIT_COPY);
            cv.SetPos(x, y + D - 1); cv.Draw(WBox(D, 1, WPixel((DWORD)col)), BLIT_COPY);
            cv.SetPos(x, y);         cv.Draw(WBox(1, D, WPixel((DWORD)col)), BLIT_COPY);
            cv.SetPos(x + D - 1, y); cv.Draw(WBox(1, D, WPixel((DWORD)col)), BLIT_COPY);
            m_count += 4;
        }
        x += D + GAP;
    }
}

/* One page tab.  The active one gets the gold keyline, a brighter fill and a
   gold foot, the way a browser marks the page you are on. */
/* One page tab.  The active one gets the gold keyline, a brighter fill and a
   gold foot, the way a browser marks the page you are on.  The foot is
   submitted before the frame -- the frame's body is its bottom layer. */
void Renderer::drawTab(WCanvas &cv, const Atlas &atlas, int x, int y, int w, int h,
                       int labelId, int live)
{
    WBitmap *lb = atlas.Label(labelId);
    blitCentered(cv, *lb, x + w / 2, y + (h - lb->GetH()) / 2, BLIT_MASK);

    if (live) {
        cv.SetPos(x + 4, y + h - 6);
        cv.Draw(WBox(w - 8, 3, WPixel((DWORD)COL_BANNER)), BLIT_COPY);
        ++m_count;
    }

    drawBevel(cv, x, y, w, h,
              live ? (unsigned long)0x1E2A3E : (unsigned long)COL_PANEL_BG,
              live ? (unsigned long)COL_BANNER : (unsigned long)COL_PANEL_KEY,
              live ? (unsigned long)0x5A6E92 : (unsigned long)COL_PANEL_LIT,
              (unsigned long)COL_PANEL_SHADE);
}

/* A cooldown that fills from the bottom: cool cyan while charging, gold the
   moment it is ready.  No numbers -- the state IS the information, so the
   player can read both skills out of the corner of their eye. */
void Renderer::drawCool(WCanvas &cv, const Atlas &atlas, int x, int y, int w, int h,
                        int labelId, int cd, int cdMax)
{
    if (cdMax < 1) cdMax = 1;
    if (cd < 0) cd = 0;

    int ready = (cd <= 0);
    int span  = h - 2;
    int fill  = ready ? span : span - (span * cd) / cdMax;
    if (fill < 0) fill = 0;

    WBitmap *lb = atlas.Label(labelId);
    blit(cv, *lb, x + (w - lb->GetW()) / 2, y - lb->GetH() - 2, BLIT_MASK);

    cv.SetPos(x, y);         cv.Draw(WBox(w, 1, WPixel((DWORD)COL_PANEL_KEY)), BLIT_COPY);
    cv.SetPos(x, y + h - 1); cv.Draw(WBox(w, 1, WPixel((DWORD)COL_PANEL_KEY)), BLIT_COPY);
    cv.SetPos(x, y);         cv.Draw(WBox(1, h, WPixel((DWORD)COL_PANEL_KEY)), BLIT_COPY);
    cv.SetPos(x + w - 1, y); cv.Draw(WBox(1, h, WPixel((DWORD)COL_PANEL_KEY)), BLIT_COPY);
    cv.SetPos(x + 1, y + 1);     cv.Draw(WBox(w - 2, 1, WPixel((DWORD)COL_PANEL_LIT)),   BLIT_COPY);
    cv.SetPos(x + 1, y + h - 2); cv.Draw(WBox(w - 2, 1, WPixel((DWORD)COL_PANEL_SHADE)), BLIT_COPY);
    m_count += 6;

    if (fill > 0)
        m_count += vGradient(cv, x + 1, y + 1 + (span - fill), w - 2, fill,
                             ready ? 0xFFE888u : (unsigned long)COL_CYAN,
                             ready ? 0xC08A10u : (unsigned long)COL_CYAN_DIM);

    m_count += vGradient(cv, x + 1, y + 1, w - 2, h - 2,
                         (unsigned long)COL_TROUGH_TOP,
                         (unsigned long)COL_TROUGH_BOT);
}

/* The card frame shared by the perk cards and the forge bench.  The accent
   strip is submitted BEFORE the bevel: the bevel's fill is its bottom layer,
   so anything drawn after it would be hidden underneath. */
void Renderer::drawCardBevel(WCanvas &cv, int x, int y, int w, int h,
                             int live, int affordable, unsigned long accent)
{
    if (affordable) {
        cv.SetPos(x + 2, y + 2);
        cv.Draw(WBox(w - 4, 3, WPixel((DWORD)accent)), BLIT_COPY);
        ++m_count;
    }

    unsigned long key, fill, lit;
    if (!affordable) {
        key  = 0x6A2A2A;
        fill = 0x1A1216;
        lit  = 0x4A2A2A;
    } else if (live) {
        key  = accent;
        fill = 0x1B2436;
        lit  = mixColor(accent, 0xFFFFFF, 70);
    } else {
        key  = (unsigned long)COL_PANEL_KEY;
        fill = (unsigned long)COL_PANEL_BG;
        lit  = (unsigned long)COL_PANEL_LIT;
    }
    drawBevel(cv, x, y, w, h, fill, key, lit, (unsigned long)COL_PANEL_SHADE);
}

/* One offset rectangle of near-black, submitted after its panel.  The overlap
   is hidden under the panel and only the offset band survives, which reads as
   depth -- and keeps a dark panel from melting into the dimmed background. */
void Renderer::drawShadow(WCanvas &cv, int x, int y, int w, int h, int off)
{
    if (off <= 0 || w <= 0 || h <= 0) return;
    cv.SetPos(x + off, y + off);
    cv.Draw(WBox(w, h, WPixel((DWORD)0x05070C)), BLIT_COPY);
    ++m_count;
}

/* "攻击 34 -> 39" centred on cx.  The "after" number is gold, so the gain is
   what the eye lands on; the "before" is dim, because it is only context. */
int Renderer::drawStatRow(WCanvas &cv, const Atlas &atlas, const World &w,
                          int perk, int cx, int y)
{
    int before = 0, after = 0, unit = 0;
    if (perk < 0 || perk >= PERK_COUNT) return 0;
    if (!w.PerkPreview(perk, before, after, unit)) return 0;

    WBitmap *nm  = atlas.Label(WF_LBL_STAT0 + perk);
    WBitmap *arr = atlas.Label(WF_LBL_ARROW);
    WBitmap *pct = atlas.Label(WF_LBL_PCT);

    int pw = (unit == 1) ? pct->GetW() : 0;
    int nb = numWidth(atlas, DIGIT_SMALL, before);
    int na = numWidth(atlas, DIGIT_SMALL, after);

    int total = nm->GetW() + 10 + nb + pw + 9 + arr->GetW() + 9 + na + pw;
    int x = cx - total / 2;

    blit(cv, *nm, x, y, BLIT_MASK);                       x += nm->GetW() + 10;
    drawNumber(cv, atlas, DIGIT_SMALL, before, x, y + 2, 0); x += nb;
    if (unit == 1) { blit(cv, *pct, x, y, BLIT_MASK); x += pw; }

    x += 9;
    blit(cv, *arr, x, y, BLIT_MASK);                      x += arr->GetW() + 9;
    drawNumber(cv, atlas, DIGIT_BIG, after, x, y - 1, 0);  x += na;
    if (unit == 1) blit(cv, *pct, x, y, BLIT_MASK);

    return total;
}

/* ---------------------------------------------------------------- numbers */

void Renderer::drawNumber(WCanvas &cv, const Atlas &atlas, int set, int value,
                          int sx, int sy, int centered)
{
    char buf[16];
    if (value < 0) value = 0;
    if (value > 999999) value = 999999;
    sprintf(buf, "%d", value);

    int w = 0;
    for (const char *p = buf; *p; ++p) {
        int d = *p - '0';
        if (d < 0 || d > 9) d = 0;
        w += atlas.DigitW(set, d);
    }
    if (centered) sx -= w / 2;

    for (const char *p = buf; *p; ++p) {
        int d = *p - '0';
        if (d < 0 || d > 9) d = 0;
        blit(cv, *atlas.Digit(set, d), sx, sy, BLIT_MASK);
        sx += atlas.DigitW(set, d);
    }
}

/* ---------------------------------------------------------------- terrain */

void Renderer::drawVignette(WCanvas &cv, const Atlas &atlas)
{
    WBitmap *v = atlas.Vignette();
    if (!v) return;
    cv.SetPos(0, 0);
    cv.Draw(*v, BLIT_SUBTRACT);
    ++m_count;
}

void Renderer::drawTerrain(WCanvas &cv, const World &w, const Atlas &atlas)
{
    int tx0 = m_ox / TILE_SIZE;
    int ty0 = m_oy / TILE_SIZE;
    int tx1 = (m_ox + FIELD_W) / TILE_SIZE + 1;
    int ty1 = (m_oy + FIELD_H) / TILE_SIZE + 1;

    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 > MAP_W) tx1 = MAP_W;
    if (ty1 > MAP_H) ty1 = MAP_H;

    for (int ty = ty0; ty < ty1; ++ty)
        for (int tx = tx0; tx < tx1; ++tx) {
            WBitmap *b = atlas.Tile((int)w.TileAt(tx, ty));
            if (!b) continue;
            blit(cv, *b, tx * TILE_SIZE - m_ox, ty * TILE_SIZE - m_oy, BLIT_COPY);
        }
}

/* ------------------------------------------------------------------ drops */

void Renderer::drawDrops(WCanvas &cv, const World &w, const Atlas &atlas)
{
    const Drop *d = w.Drops();

    for (int i = 0; i < MAX_DROPS; ++i) {
        if (!d[i].alive) continue;

        int sx = (int)d[i].x - m_ox;
        int sy = (int)d[i].y - m_oy;
        if (sx < -40 || sx > FIELD_W + 40 || sy < -40 || sy > FIELD_H + 40) continue;

        /* the last 120 frames they blink out of existence */
        if (d[i].life < 120 && ((d[i].life / 6) % 2) == 0) continue;

        if (d[i].kind == DROP_COIN) {
            WBitmap *b = atlas.Coin();
            blitShadow(cv, atlas, 0, (int)d[i].x - m_ox, (int)d[i].y - m_oy + 8);
            blit(cv, *b, sx - b->GetW() / 2, sy - b->GetH() + 4, BLIT_MASK);
        } else {
            WBitmap *b = atlas.Potion();
            blitShadow(cv, atlas, 0, (int)d[i].x - m_ox, (int)d[i].y - m_oy + 8);
            blit(cv, *b, sx - b->GetW() / 2, sy - b->GetH() + 4, BLIT_MASK);
        }
    }
}

/* ---------------------------------------------------------------- actors */

int Renderer::cmpEntDesc(const void *a, const void *b)
{
    const Ent *ea = (const Ent *)a;
    const Ent *eb = (const Ent *)b;
    /* larger y is nearer the camera and must be submitted first */
    if (ea->y < eb->y) return  1;
    if (ea->y > eb->y) return -1;
    return 0;
}

void Renderer::drawEntities(WCanvas &cv, const World &w, const Atlas &atlas)
{
    const Monster *m = w.Monsters();
    const Player  &p = w.Hero();

    int n = 0;
    for (int i = 0; i < MAX_MONSTERS; ++i)
        if (m[i].alive) {
            m_ents[n].y    = m[i].y;
            m_ents[n].kind = 0;
            m_ents[n].idx  = i;
            ++n;
        }
    m_ents[n].y    = p.y;
    m_ents[n].kind = 1;
    m_ents[n].idx  = 0;
    ++n;

    qsort(m_ents, n, sizeof(Ent), cmpEntDesc);

    for (int i = 0; i < n; ++i) {
        if (m_ents[i].kind == 1) {
            /* ---- the hero ---- */
            if (!p.alive) continue;
            if (p.invuln > 0 && ((p.invuln / 4) % 2) == 1) continue;   /* blink */

            WBitmap *b = atlas.Hero(p.facing);
            int bw = b->GetW(), bh = b->GetH();

            int bob = 0;
            float sp = sqrtf(p.vx * p.vx + p.vy * p.vy);
            if (sp > 0.7f && ((p.anim / 7) % 2) == 1) bob = -1;

            int sx = (int)p.x - bw / 2 - m_ox;
            int sy = (int)p.y - bh + 6 - m_oy + bob;

            blitShadow(cv, atlas, 0, (int)p.x - m_ox, (int)p.y - m_oy + 3);
            blit(cv, *b, sx, sy, BLIT_MASK);
            if (p.hitFlash > 0 && (p.hitFlash & 2))
                blit(cv, *atlas.HeroFlash(p.facing), sx, sy, BLIT_ADDITIVE);
        } else {
            /* ---- a monster ---- */
            const Monster &mo = m[m_ents[i].idx];
            WBitmap *b = atlas.Monster(mo.kind, mo.facing);
            int bw = b->GetW(), bh = b->GetH();

            int sx = (int)mo.x - bw / 2 - m_ox;
            int sy = (int)mo.y - bh + 6 - m_oy;

            /* wind-up tells you a blow is coming: the sprite puffs up */
            if (mo.state == MS_WINDUP && ((mo.timer / 4) % 2) == 1) {
                sx -= 1; sy -= 1;
            }

            blitShadow(cv, atlas, atlas.ShadowTierOf(mo.kind),
                       (int)mo.x - m_ox, (int)mo.y - m_oy + 2);
            blit(cv, *b, sx, sy, BLIT_MASK);
            if (mo.flash > 0 && (mo.flash & 2))
                blit(cv, *atlas.MonsterFlash(mo.kind, mo.facing), sx, sy, BLIT_ADDITIVE);
        }
    }
}

/* --------------------------------------------------------------- effects */

void Renderer::drawEffects(WCanvas &cv, const World &w, const Atlas &atlas)
{
    const Fx *f = w.Effects();

    static const float DIR[FACE_COUNT][2] = { {0,1}, {0,-1}, {-1,0}, {1,0} };

    for (int i = 0; i < MAX_FX; ++i) {
        if (!f[i].alive) continue;

        int sx = (int)f[i].x - m_ox;
        int sy = (int)f[i].y - m_oy;
        if (sx < -140 || sx > FIELD_W + 140 || sy < -140 || sy > FIELD_H + 140) continue;

        switch (f[i].kind) {
        case FX_TEXT: {
            int set = DIGIT_SMALL;
            if (f[i].crit) set = DIGIT_BIG;
            else if (f[i].color == (unsigned long)COL_DMG_HURT) set = DIGIT_HURT;
            drawNumber(cv, atlas, set, f[i].value, sx, sy, 1);
            break;
        }
        case FX_SPARK:
            blit(cv, *atlas.Spark(), sx - 2, sy - 2, BLIT_ADDITIVE);
            break;

        case FX_RING: {
            WBitmap *r = atlas.Ring(0);
            blit(cv, *r, sx - r->GetW() / 2, sy - r->GetH() / 2, BLIT_ADDITIVE);
            break;
        }
        case FX_WHIRL: {
            WBitmap *r = atlas.Ring(1);
            blit(cv, *r, sx - r->GetW() / 2, sy - r->GetH() / 2, BLIT_ADDITIVE);
            break;
        }
        case FX_SLASH: {
            int fa = f[i].facing;
            if (fa < 0 || fa >= FACE_COUNT) fa = FACE_DOWN;
            WBitmap *s = atlas.Slash(fa);
            int ox = (int)(DIR[fa][0] * 16.0f);
            int oy = (int)(DIR[fa][1] * 16.0f);
            blit(cv, *s, sx - s->GetW() / 2 + ox, sy - s->GetH() / 2 + oy, BLIT_ADDITIVE);
            break;
        }
        default:
            break;
        }
    }
}

/* -------------------------------------------------------------------- hud */

/* Three beveled clusters instead of loose text floating on the grass: level
   and experience top left, gold and kills top right, vitals bottom left.  The
   grouping is what stops the read-outs from fighting the terrain for
   attention, and it gives the gold -- which now has somewhere to be spent --
   a fixed home the eye can find without hunting. */
void Renderer::drawHud(WCanvas &cv, const World &w, const Atlas &atlas)
{
    const Player &p  = w.Hero();
    const Stats  &st = w.State();

    const int PH = 64;                  /* both top panels share a height */

    /* Every cluster below is written contents-first, panels-last.  That is
       not a style choice: see drawBevel().  The inner panel (the level badge)
       is also submitted before the outer one it sits on. */
    /* ---- level + experience (top left) --------------------------------- */
    {
        const int PX = 12, PY = 10, PW = 300;

        /* the level sits in a gold-keyed badge so it reads at a glance */
        const int BS = 32;
        const int BX = PX + 10, BY = PY + (PH - BS) / 2;
        drawNumber(cv, atlas, DIGIT_BIG, p.level,
                   BX + BS / 2, BY + (BS - atlas.DigitH(DIGIT_BIG)) / 2, 1);

        int bx = BX + BS + 12;
        int bw = PX + PW - 12 - bx;

        blit(cv, *atlas.Label(WF_LBL_EXP), bx, PY + 6, BLIT_MASK);

        /* experience as "have | need", right-aligned on the label row */
        {
            int nh = numWidth(atlas, DIGIT_SMALL, p.expNext);
            int nv = numWidth(atlas, DIGIT_SMALL, p.exp);
            int x  = PX + PW - 12 - nh;
            drawNumber(cv, atlas, DIGIT_SMALL, p.expNext, x, PY + 8, 0);
            drawRule(cv, x - 9, PY + 12, 2, (unsigned long)COL_UI_DIM);
            drawNumber(cv, atlas, DIGIT_SMALL, p.exp, x - 17 - nv, PY + 8, 0);
        }

        int fill = (p.expNext > 0) ? (bw - 2) * p.exp / p.expNext : 0;
        drawBarEx(cv, bx, PY + 42, bw, 13, fill,
                  (unsigned long)COL_EXP_TOP, (unsigned long)COL_EXP_BOT, 4);

        drawBevel(cv, BX, BY, BS, BS, 0x241E10, (unsigned long)COL_BANNER,
                  0x6A5A28, 0x120E06);
        drawBevel(cv, PX, PY, PW, PH, (unsigned long)COL_PANEL_BG,
                  (unsigned long)COL_PANEL_KEY, (unsigned long)COL_PANEL_LIT,
                  (unsigned long)COL_PANEL_SHADE);
    }

    /* ---- gold + kills (top right) -------------------------------------- */
    {
        const int PW = 250, PY = 10;
        const int PX = FIELD_W - 12 - PW;
        int right = PX + PW - 12;

        /* gold is the number the forge spends, so it gets the big digits */
        {
            int cw = atlas.Coin() ? atlas.Coin()->GetW() : 0;
            int nw = numWidth(atlas, DIGIT_BIG, p.gold);
            drawCoin(cv, atlas, p.gold, right - (cw + 6 + nw), PY + 8, 1);
        }
        {
            WBitmap *lb = atlas.Label(WF_LBL_KILL);
            int nw = numWidth(atlas, DIGIT_SMALL, st.kills);
            int x  = right - (lb->GetW() + 8 + nw);
            blit(cv, *lb, x, PY + 44, BLIT_MASK);
            drawNumber(cv, atlas, DIGIT_SMALL, st.kills,
                       x + lb->GetW() + 8, PY + 46, 0);
        }

        drawBevel(cv, PX, PY, PW, PH, (unsigned long)COL_PANEL_BG,
                  (unsigned long)COL_PANEL_KEY, (unsigned long)COL_PANEL_LIT,
                  (unsigned long)COL_PANEL_SHADE);
    }

    /* ---- wave banner (centre, top) ------------------------------------- */
    if (st.banner > 0) {
        /* fade out over the last 40 frames by blinking */
        if (st.banner > 40 || ((st.banner / 4) % 2)) {
            int a = atlas.LabelW(WF_LBL_WAVE_A);
            int b = atlas.LabelW(WF_LBL_WAVE_B);
            int nw = numWidth(atlas, DIGIT_BIG, st.bannerWave);
            int total = a + nw + b + 20;
            int x = FIELD_W / 2 - total / 2;

            blit(cv, *atlas.Label(WF_LBL_WAVE_A), x, 14, BLIT_MASK);
            x += a + 10;
            drawNumber(cv, atlas, DIGIT_BIG, st.bannerWave, x, 10, 0);
            x += nw + 10;
            blit(cv, *atlas.Label(WF_LBL_WAVE_B), x, 14, BLIT_MASK);
        }
    }

    /* ---- remaining + the threat the wave scaling added ------------------ */
    if (st.banner <= 40 && !st.gameover) {
        int left = st.toSpawn + w.LiveMonsters();
        if (left > 0) {
            WBitmap *ll = atlas.Label(WF_LBL_LEFT);
            int lnw = numWidth(atlas, DIGIT_SMALL, left);

            /* the enemy multiplier, as a percentage of its wave-1 self */
            WBitmap *el = atlas.Label(WF_LBL_ENEMYUP);
            WBitmap *pc = atlas.Label(WF_LBL_PCT);
            int mul = (int)(w.WaveHpMul() * 100.0f + 0.5f);
            int mnw = numWidth(atlas, DIGIT_SMALL, mul);

            int ltot = ll->GetW() + 8 + lnw;
            int etot = el->GetW() + 8 + mnw + pc->GetW();
            int gap  = 34;
            int x    = FIELD_W / 2 - (ltot + gap + etot) / 2;
            const int y = 52;

            blit(cv, *ll, x, y, BLIT_MASK);
            drawNumber(cv, atlas, DIGIT_SMALL, left, x + ll->GetW() + 8, y + 2, 0);
            x += ltot + gap;

            /* only worth showing once the waves actually scale (wave 2+) */
            if (w.WaveHpMul() > 1.001f) {
                blit(cv, *el, x, y, BLIT_MASK);
                drawNumber(cv, atlas, DIGIT_SMALL, mul,
                           x + el->GetW() + 8, y + 2, 0);
                blit(cv, *pc, x + el->GetW() + 8 + mnw, y, BLIT_MASK);
            }
        }
    }

    /* ---- vitals (bottom left) ------------------------------------------ */
    {
        const int PX = 12, PW = 300;
        const int PY = FIELD_H - 122, PHv = 110;

        const int bx = PX + 12;
        const int bw = PW - 24;

        /* health, with the exact figure on the label row */
        blit(cv, *atlas.Label(WF_LBL_HP), bx, PY + 6, BLIT_MASK);
        {
            int hw = numWidth(atlas, DIGIT_SMALL, p.hp);
            int mw = numWidth(atlas, DIGIT_SMALL, p.maxhp);
            int x  = PX + PW - 12 - mw;
            drawNumber(cv, atlas, DIGIT_SMALL, p.maxhp, x, PY + 8, 0);
            drawRule(cv, x - 9, PY + 12, 2, (unsigned long)COL_UI_DIM);
            drawNumber(cv, atlas, DIGIT_SMALL, p.hp, x - 17 - hw, PY + 8, 0);
        }
        {
            int fill = (p.maxhp > 0) ? (bw - 2) * p.hp / p.maxhp : 0;
            drawBarEx(cv, bx, PY + 38, bw, 18, fill,
                      (unsigned long)COL_HP_TOP, (unsigned long)COL_HP_BOT, 4);
        }

        /* rage */
        blit(cv, *atlas.Label(WF_LBL_MP), bx, PY + 62, BLIT_MASK);
        {
            int fill = (p.maxmp > 0) ? (bw - 2) * p.mp / p.maxmp : 0;
            drawBarEx(cv, bx, PY + 92, bw, 12, fill,
                      (unsigned long)COL_MP_TOP, (unsigned long)COL_MP_BOT, 4);
        }

        drawBevel(cv, PX, PY, PW, PHv, (unsigned long)COL_PANEL_BG,
                  (unsigned long)COL_PANEL_KEY, (unsigned long)COL_PANEL_LIT,
                  (unsigned long)COL_PANEL_SHADE);
    }

    /* ---- dash / whirlwind charge (bottom, right of the vitals) ---------- */
    {
        const int PX = 322, PW = 160;
        const int PY = FIELD_H - 122, PHc = 110;

        drawCool(cv, atlas, PX + 10, PY + 34, PW - 20, 16,
                 WF_LBL_DASH, p.dashCd, w.DashCdMax());
        drawCool(cv, atlas, PX + 10, PY + 90, PW - 20, 16,
                 WF_LBL_SPIN, p.spinCd, w.SpinCdMax());

        drawBevel(cv, PX, PY, PW, PHc, (unsigned long)COL_PANEL_BG,
                  (unsigned long)COL_PANEL_KEY, (unsigned long)COL_PANEL_LIT,
                  (unsigned long)COL_PANEL_SHADE);
    }

    /* ---- the run summary lives in DrawOver(); the HUD only reads live state */
}

/* ---------------------------------------------------------------- minimap */

/* 2 px per tile; 64x48 tiles -> 128x96, sits in the bottom-right corner. */
static unsigned long miniTileColor(int id)
{
    switch (id) {
    case TILE_DIRT:  return 0x4A3A22;
    case TILE_ROCK:  return 0x6E6E76;
    case TILE_WATER: return 0x1E3A5E;
    case TILE_TREE:  return 0x123A18;
    case TILE_GRASS:
    default:         return 0x244A24;
    }
}

void Renderer::buildMinimap(const World &w)
{
    m_miniW = 128;
    m_miniH = 96;
    m_miniX = FIELD_W - m_miniW - 12;
    m_miniY = FIELD_H - m_miniH - 12;

    m_mini = new WBitmap();
    m_mini->Create(m_miniW, m_miniH);

    const unsigned char *t = w.Tiles();
    for (int ty = 0; ty < MAP_H; ++ty)
        for (int tx = 0; tx < MAP_W; ++tx) {
            unsigned long c = miniTileColor(t[ty * MAP_W + tx]);
            for (int yy = 0; yy < 2; ++yy)
                for (int xx = 0; xx < 2; ++xx) {
                    int px = tx * 2 + xx;
                    int py = ty * 2 + yy;
                    if (px < m_miniW && py < m_miniH)
                        (*m_mini)[py][px] = WPixel((DWORD)c);
                }
        }
    m_mini->SetPos(0, 0);
}

/* The whole map at a glance plus what matters on it right now: every live
   monster as a 3 px dot in its own hue, the hero as a 5 px block, and the
   camera viewport as a rectangle, so you can see where you are and which
   pocket the monsters are funnelling out of.

   Order matters: WCanvas applies the link submitted FIRST last, i.e. on top.
   The terrain thumbnail is opaque, so the markers and the camera box are
   submitted first and the terrain last; the panel fill and its 2 px frame are
   submitted after the terrain (the frame lies outside it, so it stays put). */
void Renderer::drawMinimap(WCanvas &cv, const World &w, const Atlas &atlas)
{
    (void)atlas;
    if (!m_mini) buildMinimap(w);

    /* ---- monsters (on top) ------------------------------------------- */
    const Monster *m = w.Monsters();
    for (int i = 0; i < MAX_MONSTERS; ++i) {
        if (!m[i].alive) continue;
        int mx = m_miniX + (int)(m[i].x / (float)WORLD_W * (float)m_miniW);
        int my = m_miniY + (int)(m[i].y / (float)WORLD_H * (float)m_miniH);
        unsigned long c;
        switch (m[i].kind) {
        case MON_ARCHER: c = 0x8A5CB8; break;
        case MON_BRUTE:  c = 0xB84A3C; break;
        case MON_BOSS:   c = 0xE0A020; break;
        default:         c = 0x4AA8A0; break;   /* grunt */
        }
        cv.SetPos(mx - 1, my - 1);
        cv.Draw(WBox(3, 3, WPixel(c)), BLIT_COPY);
        ++m_count;
    }

    /* ---- hero (on top) ------------------------------------------------ */
    const Player &p = w.Hero();
    if (p.alive) {
        int hx = m_miniX + (int)(p.x / (float)WORLD_W * (float)m_miniW);
        int hy = m_miniY + (int)(p.y / (float)WORLD_H * (float)m_miniH);
        cv.SetPos(hx - 2, hy - 2);
        cv.Draw(WBox(5, 5, WPixel((DWORD)COL_HERO_A)), BLIT_COPY);
        ++m_count;
    }

    /* ---- camera viewport (on top) ------------------------------------- */
    int rx = m_miniX + (int)(w.CameraX() / (float)WORLD_W * (float)m_miniW);
    int ry = m_miniY + (int)(w.CameraY() / (float)WORLD_H * (float)m_miniH);
    int rw = (int)((float)FIELD_W / (float)WORLD_W * (float)m_miniW);
    int rh = (int)((float)FIELD_H / (float)WORLD_H * (float)m_miniH);
    if (rw < 2) rw = 2;
    if (rh < 2) rh = 2;
    WPixel viewCol((DWORD)COL_UI_TEXT);
    cv.SetPos(rx, ry);          cv.Draw(WBox(rw, 1, viewCol), BLIT_COPY);
    cv.SetPos(rx, ry + rh - 1); cv.Draw(WBox(rw, 1, viewCol), BLIT_COPY);
    cv.SetPos(rx, ry);          cv.Draw(WBox(1, rh, viewCol), BLIT_COPY);
    cv.SetPos(rx + rw - 1, ry); cv.Draw(WBox(1, rh, viewCol), BLIT_COPY);
    m_count += 4;

    /* ---- terrain thumbnail (under the markers) ----------------------- */
    blit(cv, *m_mini, m_miniX, m_miniY, BLIT_COPY);

    /* ---- panel fill + 2 px edge frame --------------------------------- */
    /* Submitted AFTER the terrain, so the bevel frame lands around the
       thumbnail instead of over it: its keyline and bevel rows live in the
       2 px ring the thumbnail does not cover, and its fill ends up underneath
       the thumbnail where it cannot be seen. */
    drawBevel(cv, m_miniX - 2, m_miniY - 2, m_miniW + 4, m_miniH + 4,
              (unsigned long)COL_PANEL_BG, (unsigned long)COL_PANEL_KEY,
              (unsigned long)COL_PANEL_LIT, (unsigned long)COL_PANEL_SHADE);
}

/* ------------------------------------------------------------- big map */

/* The whole world at 10 px per tile, baked once.  Walkable tiles the hero can
   never reach (walled-off pockets) are shaded dark, so "here be land you
   cannot get to" is visible at a glance; the blocking tiles keep their own
   colour. */
void Renderer::buildBig(const World &w)
{
    m_bigTile = 10;
    m_bigW = MAP_W * m_bigTile;                       /* 640 */
    m_bigH = MAP_H * m_bigTile;                       /* 480 */
    m_bigX = (FIELD_W - m_bigW) / 2;
    m_bigY = (FIELD_H - m_bigH) / 2;

    m_big = new WBitmap();
    m_big->Create(m_bigW, m_bigH);

    /* remember which hero tile produced the shading, so the map rebuilds
       only when the hero has actually moved to a different tile */
    m_bigHero = (int)(w.Hero().y / (float)TILE_SIZE) * MAP_W
              + (int)(w.Hero().x / (float)TILE_SIZE);

    const unsigned char *t = w.Tiles();
    for (int ty = 0; ty < MAP_H; ++ty)
        for (int tx = 0; tx < MAP_W; ++tx) {
            int id = t[ty * MAP_W + tx];
            unsigned long c = miniTileColor(id);
            int blocking = (id == TILE_ROCK || id == TILE_WATER || id == TILE_TREE);
            if (!blocking && w.DistAt(tx, ty) < 0) c = 0x0E1218;   /* stranded pocket */
            for (int yy = 0; yy < m_bigTile; ++yy)
                for (int xx = 0; xx < m_bigTile; ++xx)
                    (*m_big)[ty * m_bigTile + yy][tx * m_bigTile + xx] = WPixel((DWORD)c);
        }
    m_big->SetPos(0, 0);
}

/* Everything from the world drawn large.  Submits are ordered top->bottom:
   the info text first (topmost), then the hero, monsters, drops and camera
   box, and only then the terrain -- the markers sit on top because WCanvas
   applies the link submitted FIRST last, i.e. on top. */
void Renderer::drawBig(WCanvas &cv, const World &w, const Atlas &atlas)
{
    /* rebuild only when missing or when the hero has crossed to a new tile
       (the sim is paused while this map is up, so no churn on open) */
    int heroTile = (int)(w.Hero().y / (float)TILE_SIZE) * MAP_W
                 + (int)(w.Hero().x / (float)TILE_SIZE);
    if (!m_big || heroTile != m_bigHero) { delete m_big; m_big = 0; buildBig(w); }

    const Stats &st = w.State();

    int bx = m_bigX, by = m_bigY;

    /* ---- info (topmost) ------------------------------------------------ */
    {
        WBitmap *title = atlas.Label(WF_LBL_TITLE);
        blitCentered(cv, *title, FIELD_W / 2, 8, BLIT_MASK);

        int a = atlas.LabelW(WF_LBL_WAVE_A);
        int b = atlas.LabelW(WF_LBL_WAVE_B);
        char num[12];
        sprintf(num, "%d", st.wave);
        int nw = 0;
        for (const char *q = num; *q; ++q) nw += atlas.DigitW(DIGIT_BIG, *q - '0');
        int total = a + nw + b + 16;
        int x = FIELD_W / 2 - total / 2;
        blit(cv, *atlas.Label(WF_LBL_WAVE_A), x, 38, BLIT_MASK); x += a + 8;
        drawNumber(cv, atlas, DIGIT_BIG, st.wave, x, 32, 0);      x += nw + 8;
        blit(cv, *atlas.Label(WF_LBL_WAVE_B), x, 38, BLIT_MASK);

        int left = st.toSpawn + w.LiveMonsters();
        int lw = atlas.LabelW(WF_LBL_LEFT);
        sprintf(num, "%d", left);
        int lnw = 0;
        for (const char *q = num; *q; ++q) lnw += atlas.DigitW(DIGIT_SMALL, *q - '0');
        int ltot = lw + lnw + 8;
        int lx = FIELD_W / 2 - ltot / 2;
        int ly = FIELD_H - 30;
        blit(cv, *atlas.Label(WF_LBL_LEFT), lx, ly, BLIT_MASK);
        drawNumber(cv, atlas, DIGIT_SMALL, left, lx + lw + 8, ly - 2, 0);
    }

    /* ---- monsters (coloured, size = importance; boss carries an hp bar) */
    const Monster *m = w.Monsters();
    for (int i = 0; i < MAX_MONSTERS; ++i) {
        if (!m[i].alive) continue;
        int mx = bx + (int)(m[i].x / (float)WORLD_W * (float)m_bigW);
        int my = by + (int)(m[i].y / (float)WORLD_H * (float)m_bigH);
        int s; unsigned long c;
        switch (m[i].kind) {
        case MON_BOSS:  s = 11; c = 0xE0A020; break;
        case MON_BRUTE: s =  8; c = 0xB84A3C; break;
        case MON_ARCHER:s =  6; c = 0x9A6CC8; break;
        default:        s =  6; c = 0x4AA8A0; break;   /* grunt */
        }
        cv.SetPos(mx - s / 2, my - s / 2);
        cv.Draw(WBox(s, s, WPixel(c)), BLIT_COPY);
        ++m_count;
        if (m[i].kind == MON_BOSS) {
            int hw = 16, hx = mx - hw / 2, hy = my - s - 8;
            int fw = (m[i].maxhp > 0) ? hw * m[i].hp / m[i].maxhp : 0;
            if (fw > 0) { cv.SetPos(hx, hy); cv.Draw(WBox(fw, 3, WPixel((DWORD)COL_HP)), BLIT_COPY); ++m_count; }
            cv.SetPos(hx, hy); cv.Draw(WBox(hw, 3, WPixel((DWORD)COL_HP_BG)), BLIT_COPY); ++m_count;
        }
    }

    /* ---- drops --------------------------------------------------------- */
    const Drop *d = w.Drops();
    for (int i = 0; i < MAX_DROPS; ++i) {
        if (!d[i].alive) continue;
        int mx = bx + (int)(d[i].x / (float)WORLD_W * (float)m_bigW);
        int my = by + (int)(d[i].y / (float)WORLD_H * (float)m_bigH);
        unsigned long c = (d[i].kind == DROP_COIN) ? (DWORD)COL_DMG_CRIT : (DWORD)0xE85050;
        cv.SetPos(mx - 2, my - 2);
        cv.Draw(WBox(4, 4, WPixel(c)), BLIT_COPY);
        ++m_count;
    }

    /* ---- hero (dark ring, white core) ---------------------------------- */
    const Player &p = w.Hero();
    if (p.alive) {
        int hx = bx + (int)(p.x / (float)WORLD_W * (float)m_bigW);
        int hy = by + (int)(p.y / (float)WORLD_H * (float)m_bigH);
        cv.SetPos(hx - 4, hy - 4);
        cv.Draw(WBox(8, 8, WPixel((DWORD)COL_HERO_A)), BLIT_COPY); ++m_count;   /* core (top) */
        cv.SetPos(hx - 5, hy - 5);
        cv.Draw(WBox(10, 10, WPixel((DWORD)COL_UI_BG)), BLIT_COPY); ++m_count;  /* ring (below) */
    }

    /* ---- camera viewport ----------------------------------------------- */
    {
        int rx = bx + (int)(w.CameraX() / (float)WORLD_W * (float)m_bigW);
        int ry = by + (int)(w.CameraY() / (float)WORLD_H * (float)m_bigH);
        int rw = (int)((float)FIELD_W / (float)WORLD_W * (float)m_bigW);
        int rh = (int)((float)FIELD_H / (float)WORLD_H * (float)m_bigH);
        WPixel vc((DWORD)COL_UI_TEXT);
        cv.SetPos(rx, ry);          cv.Draw(WBox(rw, 1, vc), BLIT_COPY);
        cv.SetPos(rx, ry + rh - 1); cv.Draw(WBox(rw, 1, vc), BLIT_COPY);
        cv.SetPos(rx, ry);          cv.Draw(WBox(1, rh, vc), BLIT_COPY);
        cv.SetPos(rx + rw - 1, ry); cv.Draw(WBox(1, rh, vc), BLIT_COPY);
        m_count += 4;
    }

    /* ---- terrain (under the markers) ----------------------------------- */
    blit(cv, *m_big, bx, by, BLIT_COPY);

    /* ---- edge frame (its 3 px ring lies outside the terrain) ----------- */
    cv.SetPos(bx - 3, by - 3);
    cv.Draw(WBox(m_bigW + 6, m_bigH + 6, WPixel((DWORD)COL_UI_EDGE)), BLIT_COPY);
    ++m_count;

    /* ---- screen clear (bottom-most, shows in the margins) -------------- */
    cv.SetPos(0, 0);
    cv.Draw(WBox(FIELD_W, FIELD_H, WPixel((DWORD)0x07090C)), BLIT_COPY);
    ++m_count;
}

void Renderer::DrawMap(WCanvas &cv, const World &w, const Atlas &atlas)
{
    m_count = 0;
    drawBig(cv, w, atlas);
    cv.SetPos(0, 0);
}

/* ==========================================================================
 *  product shell screens
 *
 *  Same contract as Draw(): WCanvas applies the link submitted FIRST last,
 *  i.e. on top.  Every screen below therefore submits its UI first and the
 *  full-screen backdrop LAST (bottom), which is why drawBackdrop() is called
 *  at the end of each one and not the beginning.
 * ========================================================================*/

/* The menu sky.  Bands lerp from a cold night blue at the top to a deep navy
   at the bottom; a horizon rule marks the ground plane the character line-up
   stands on; a scatter of embers drifts on two slow sines so the menu never
   looks like a still image.

   The palette is deliberately kept ABOVE the vignette's darkest value (26,32,
   26): the vignette subtracts, so a band that dark sits under it would crush
   to pure black in the corners and read as a hole in the screen.

   The embers are Spark() blits and the halo is Glow(): WBox::DrawFunc() has no
   additive slot, so the additive boxes this used to use were drawing nothing
   at all and the menu was static. */
void Renderer::drawBackdrop(WCanvas &cv, const Atlas &atlas, unsigned tick)
{
    const int rows = 16;
    const int rh   = (FIELD_H + rows - 1) / rows;
    for (int i = 0; i < rows; ++i) {
        int t = (i * 255) / (rows - 1);
        int r = (0x3E * (255 - t) + 0x28 * t) / 255;
        int g = (0x52 * (255 - t) + 0x34 * t) / 255;
        int b = (0x76 * (255 - t) + 0x50 * t) / 255;
        cv.SetPos(0, i * rh);
        cv.Draw(WBox(FIELD_W, rh,
                     WPixel((DWORD)((r << 16) | (g << 8) | b))), BLIT_COPY);
        ++m_count;
    }

    /* a pool of warm light for the headline to sit in */
    if (atlas.Glow())
        blitCentered(cv, *atlas.Glow(), FIELD_W / 2, 32, BLIT_ADDITIVE);

    /* horizon */
    cv.SetPos(0, 416);
    cv.Draw(WBox(FIELD_W, 2, WPixel((DWORD)0x4A5C78)), BLIT_COPY);
    ++m_count;
    cv.SetPos(0, 418);
    cv.Draw(WBox(FIELD_W, 1, WPixel((DWORD)0x243048)), BLIT_COPY);
    ++m_count;

    /* drifting embers */
    WBitmap *sp = atlas.Spark();
    if (sp) {
        for (int i = 0; i < 18; ++i) {
            float ph = (float)i * 0.83f;
            float t  = (float)tick * 0.012f + ph;
            int ex = (int)(FIELD_W * 0.5f + 330.0f * sinf(t * (0.31f + 0.017f * i)));
            int ey = (int)(430.0f + 150.0f * sinf(t * (0.47f + 0.021f * i))
                                  - 26.0f * (float)i);
            if (ey < 40) ey = 40;
            if (ey > FIELD_H - 40) ey = FIELD_H - 40;
            if (ex < 8)  ex += FIELD_W - 16;
            if (ex > FIELD_W - 8) ex -= FIELD_W - 16;
            blit(cv, *sp, ex, ey, BLIT_ADDITIVE);
        }
    }
}

/* Grunt / hero / brute posed on the horizon -- the cast, at a glance.  Each
   actor is submitted BEFORE its shadow here, unlike the in-world version: the
   shadow is subtractive, and on top of a sprite it crushes the dark outline
   pixels at the feet down to pure black. */
void Renderer::drawDiorama(WCanvas &cv, const Atlas &atlas)
{
    const int baseY = 418;

    /* actors first, then their shadows, then the plinth under the feet */
    WBitmap *b = atlas.Monster(MON_GRUNT, FACE_DOWN);
    blit(cv, *b, 236 - b->GetW() / 2, baseY - b->GetH() + 4, BLIT_MASK);
    blitShadow(cv, atlas, atlas.ShadowTierOf(MON_GRUNT), 236, baseY);

    b = atlas.Hero(FACE_DOWN);
    blit(cv, *b, 400 - b->GetW() / 2, baseY - b->GetH() + 4, BLIT_MASK);
    blitShadow(cv, atlas, 0, 400, baseY);

    b = atlas.Monster(MON_BRUTE, FACE_DOWN);
    blit(cv, *b, 566 - b->GetW() / 2, baseY - b->GetH() + 4, BLIT_MASK);
    blitShadow(cv, atlas, atlas.ShadowTierOf(MON_BRUTE), 566, baseY);

    b = atlas.Monster(MON_ARCHER, FACE_DOWN);
    blit(cv, *b, 682 - b->GetW() / 2, baseY - b->GetH() + 4, BLIT_MASK);
    blitShadow(cv, atlas, 0, 682, baseY);

    cv.SetPos(120, baseY + 2);
    cv.Draw(WBox(560, 5, WPixel((DWORD)0x27334A)), BLIT_COPY);
    ++m_count;
    cv.SetPos(120, baseY + 7);
    cv.Draw(WBox(560, 2, WPixel((DWORD)0x141C28)), BLIT_COPY);
    ++m_count;
}

/* ---- the frozen-world wash ----------------------------------------------
   WBox::DrawFunc() looks the style up in a six-entry table whose additive and
   subtractive slots are NULL, so `cv.Draw(WBox(...), BLIT_SUBTRACT)` returns 0
   and draws NOTHING.  Every "dim the world behind the overlay" sheet in this
   file used to be one of those, which is why the paused screens looked like
   the game was still running at full brightness.  A WBitmap carries all six
   modes, so the dim is a bitmap: one full-screen sheet baked on first use. */
void Renderer::ensureDim()
{
    if (m_dim) return;
    m_dim = new WBitmap();
    m_dim->Create(FIELD_W, FIELD_H);

    WPixel c((DWORD)0x1C2230);
    for (int y = 0; y < FIELD_H; ++y)
        for (int x = 0; x < FIELD_W; ++x)
            (*m_dim)[y][x] = c;
    m_dim->SetPos(0, 0);
}

void Renderer::drawDim(WCanvas &cv, int passes)
{
    ensureDim();
    if (!m_dim) return;
    for (int i = 0; i < passes; ++i) blit(cv, *m_dim, 0, 0, BLIT_SUBTRACT);
}

int Renderer::numWidth(const Atlas &atlas, int set, int value) const
{
    char buf[16];
    if (value < 0) value = 0;
    if (value > 999999) value = 999999;
    sprintf(buf, "%d", value);
    int w = 0;
    for (const char *p = buf; *p; ++p) w += atlas.DigitW(set, *p - '0');
    return w;
}

/* The run summary is drawn by DrawOver(); the HUD itself only carries live
   read-outs, so nothing here reacts to gameover. */

/* mm:ss width in px, so a caller can centre it next to a label. */
int Renderer::clockWidth(const Atlas &atlas, int seconds) const
{
    if (seconds < 0) seconds = 0;
    int mm = seconds / 60;
    return numWidth(atlas, DIGIT_SMALL, mm) + 12 +
           atlas.DigitW(DIGIT_SMALL, 0) * 2;
}

/* mm:ss, centred.  The colon is two stacked 2x2 blocks -- there is no ASCII
   font in the atlas, only digits, so punctuation is drawn as geometry. */
void Renderer::drawClock(WCanvas &cv, const Atlas &atlas, int seconds, int cx, int y)
{
    if (seconds < 0) seconds = 0;
    int mm = seconds / 60;
    int ss = seconds % 60;
    int x  = cx - clockWidth(atlas, seconds) / 2;
    drawNumber(cv, atlas, DIGIT_SMALL, mm, x, y, 0);
    x += numWidth(atlas, DIGIT_SMALL, mm) + 4;
    int dy = y + atlas.DigitH(DIGIT_SMALL) / 2;
    cv.SetPos(x, dy - 3);
    cv.Draw(WBox(2, 2, WPixel((DWORD)COL_UI_DIM)), BLIT_COPY);
    cv.SetPos(x, dy + 1);
    cv.Draw(WBox(2, 2, WPixel((DWORD)COL_UI_DIM)), BLIT_COPY);
    m_count += 2;
    x += 8;
    char buf[8];
    sprintf(buf, "%02d", ss);
    for (const char *p = buf; *p; ++p) {
        blit(cv, *atlas.Digit(DIGIT_SMALL, *p - '0'), x, y, BLIT_MASK);
        x += atlas.DigitW(DIGIT_SMALL, *p - '0');
    }
}

/* A small right-pointing chevron, built out of 2x2 blocks because the library
   has no line primitive that would give a clean diagonal.  Returns the draw
   count so the caller can keep its item tally honest. */
static int drawChevron(WCanvas &cv, int x, int y, unsigned long col)
{
    static const int STEP[5][2] = { {4,0}, {2,2}, {0,4}, {2,6}, {4,8} };
    for (int i = 0; i < 5; ++i) {
        cv.SetPos(x + STEP[i][0], y + STEP[i][1]);
        cv.Draw(WBox(2, 2, WPixel((DWORD)col)), BLIT_COPY);
    }
    return 5;
}

/* ------------------------------------------------------------ title menu */

void Renderer::DrawTitle(WCanvas &cv, const World &w, const Atlas &atlas,
                         unsigned tick, int sel)
{
    m_count = 0;

    /* headline, sitting in the glow the backdrop lays down */
    blitCentered(cv, *atlas.TitleArt(), FIELD_W / 2, 62, BLIT_MASK);
    {
        /* the bright centre first: the dim full-width rule is drawn under it,
           so a link submitted later would bury the gold (see drawBevel) */
        int cx = FIELD_W / 2;
        drawRule(cv, cx - 90,  158, 180, (unsigned long)COL_BANNER);
        drawRule(cv, cx - 150, 158, 300, 0x6A5A28);
    }
    blitCentered(cv, *atlas.Label(WF_LBL_GOALTIP), FIELD_W / 2, 176, BLIT_MASK);

    /* menu: real buttons rather than loose text, so the live row is obvious
       from across the room.  Contents first -- the panel fill is the bottom
       layer, so anything drawn after the bevel would vanish under it. */
    static const int ITEM[2] = { WF_LBL_START, WF_LBL_HOWTO };
    {
        const int BW = 300, BH = 50;
        for (int i = 0; i < 2; ++i) {
            int x = FIELD_W / 2 - BW / 2;
            int y = 234 + i * 62;
            int live = (i == sel);

            if (live) {
                /* pulsing gold underline under the live row */
                int pw = (int)(120.0f + 70.0f *
                               (0.5f + 0.5f * sinf((float)tick * 0.12f)));
                cv.SetPos(FIELD_W / 2 - pw / 2, y + BH + 4);
                cv.Draw(WBox(pw, 3, WPixel((DWORD)COL_BANNER)), BLIT_COPY);
                ++m_count;
                m_count += drawChevron(cv, x + 20, y + BH / 2 - 5,
                                       (unsigned long)COL_BANNER);
            }

            WBitmap *lb = atlas.Label(ITEM[i]);
            blitCentered(cv, *lb, FIELD_W / 2 + (live ? 8 : 0),
                         y + (BH - lb->GetH()) / 2, BLIT_MASK);

            drawBevel(cv, x, y, BW, BH,
                      live ? (unsigned long)0x1E2A3E : (unsigned long)0x0E1420,
                      live ? (unsigned long)COL_BANNER : (unsigned long)COL_PANEL_KEY,
                      live ? (unsigned long)0x5A6E92 : (unsigned long)COL_PANEL_LIT,
                      (unsigned long)COL_PANEL_SHADE);
            drawShadow(cv, x, y, BW, BH, 5);
        }
    }

    drawDiorama(cv, atlas);

    /* best run so far, on its own strip */
    {
        int bw = atlas.LabelW(WF_LBL_BEST);
        int aw = atlas.LabelW(WF_LBL_WAVE_A);
        int cw = atlas.LabelW(WF_LBL_WAVE_B);
        int kw = atlas.LabelW(WF_LBL_KILL);
        int wv = numWidth(atlas, DIGIT_SMALL, w.BestWave());
        int kk = numWidth(atlas, DIGIT_SMALL, w.BestKills());
        int total = bw + 18 + aw + 8 + wv + 8 + cw + 26 + kw + 8 + kk;
        int x = FIELD_W / 2 - total / 2;
        const int SY = 444, SH = 36;
        int y = SY + 9;

        blit(cv, *atlas.Label(WF_LBL_BEST), x, y, BLIT_MASK); x += bw + 18;
        blit(cv, *atlas.Label(WF_LBL_WAVE_A), x, y, BLIT_MASK); x += aw + 8;
        drawNumber(cv, atlas, DIGIT_SMALL, w.BestWave(), x, y + 2, 0); x += wv + 8;
        blit(cv, *atlas.Label(WF_LBL_WAVE_B), x, y, BLIT_MASK); x += cw + 26;
        blit(cv, *atlas.Label(WF_LBL_KILL), x, y, BLIT_MASK); x += kw + 8;
        drawNumber(cv, atlas, DIGIT_SMALL, w.BestKills(), x, y + 2, 0);

        drawBevel(cv, FIELD_W / 2 - (total + 64) / 2, SY, total + 64, SH,
                  (unsigned long)COL_PANEL_BG, (unsigned long)COL_PANEL_KEY,
                  (unsigned long)COL_PANEL_LIT, (unsigned long)COL_PANEL_SHADE);
    }

    /* hints */
    blitCentered(cv, *atlas.Label(WF_LBL_SELECT),  FIELD_W / 2, 520, BLIT_MASK);
    blitCentered(cv, *atlas.Label(WF_LBL_CONFIRM), FIELD_W / 2, 548, BLIT_MASK);

    /* backdrop last: it covers the whole screen, so it has to sit underneath */
    drawVignette(cv, atlas);
    drawBackdrop(cv, atlas, tick);

    cv.SetPos(0, 0);
}

/* ------------------------------------------------------------- controls */

void Renderer::DrawHowto(WCanvas &cv, const Atlas &atlas)
{
    m_count = 0;

    blitCentered(cv, *atlas.Label(WF_LBL_HOWTO), FIELD_W / 2, 58, BLIT_MASK);
    {
        int cx = FIELD_W / 2;
        drawRule(cv, cx - 70,  100, 140, (unsigned long)COL_BANNER);
        drawRule(cv, cx - 120, 100, 240, 0x6A5A28);
    }

    const int PX = 100, PY = 116, PW = 600, PH = 372;

    /* the text first (top), then the panel under it: a WCanvas applies the
       link submitted first last, so the panel has to come after its contents */
    for (int i = 0; i < 6; ++i) {
        int y = PY + 22 + i * 56;

        cv.SetPos(PX + 30, y + 6);
        cv.Draw(WBox(7, 7, WPixel((DWORD)COL_BANNER)), BLIT_COPY);
        ++m_count;
        blit(cv, *atlas.Label(WF_LBL_HOW0 + i), PX + 56, y, BLIT_MASK);

        /* the zebra band goes down AFTER the line: it is opaque and spans the
           whole row, so submitting it first (as it used to be) hid the text of
           every other row -- three of the six lines never appeared at all */
        if ((i & 1) == 0) {
            cv.SetPos(PX + 8, y - 12);
            cv.Draw(WBox(PW - 16, 48, WPixel((DWORD)0x161E2C)), BLIT_COPY);
            ++m_count;
        }
    }

    blitCentered(cv, *atlas.Label(WF_LBL_BACK),    FIELD_W / 2, 512, BLIT_MASK);
    blitCentered(cv, *atlas.Label(WF_LBL_CONFIRM), FIELD_W / 2, 540, BLIT_MASK);

    drawBevel(cv, PX, PY, PW, PH, (unsigned long)COL_PANEL_BG,
              (unsigned long)COL_PANEL_KEY, (unsigned long)COL_PANEL_LIT,
              (unsigned long)COL_PANEL_SHADE);
    drawShadow(cv, PX, PY, PW, PH, 7);

    drawVignette(cv, atlas);
    drawBackdrop(cv, atlas, 0);

    cv.SetPos(0, 0);
}

/* ------------------------------------------------------- level-up overlay */

/* One frame, two pages.  Tab 0 is the perk cards; tab 1 is the forge, where
   the run's gold finally has somewhere to go.  They share the frame on
   purpose: the level-up pause is the only pause the player ever gets, so a
   shop anywhere else would either interrupt a fight or never be visited.

   Every card prints what the pick actually does to the hero's numbers
   ("34 -> 39"), because at level 2 and at level 9 a flat "+5 attack" means
   very different things and the player had no way to tell which they were
   looking at. */
void Renderer::DrawPerks(WCanvas &cv, const World &w, const Atlas &atlas,
                         int sel, int tab)
{
    const Player &p  = w.Hero();
    const Stats  &st = w.State();

    if (tab != 0) tab = 1;
    if (sel < 0 || sel > 2) sel = 0;

    /* ---- headline + tab strip ------------------------------------------ */
    blitCentered(cv, *atlas.Label(tab == 0 ? WF_LBL_UPGRADE : WF_LBL_FORGE),
                 FIELD_W / 2, 44, BLIT_MASK);

    {
        const int TW = 132, TH = 34, GAP = 8;
        int x0 = FIELD_W / 2 - (TW * 2 + GAP) / 2;
        drawTab(cv, atlas, x0,            92, TW, TH, WF_LBL_TABPERK, tab == 0);
        drawTab(cv, atlas, x0 + TW + GAP, 92, TW, TH, WF_LBL_FORGE,  tab == 1);

        /* the balance, top right -- that is the number the forge spends */
        int cw = atlas.Coin() ? atlas.Coin()->GetW() : 0;
        int nw = numWidth(atlas, DIGIT_BIG, p.gold);
        drawCoin(cv, atlas, p.gold, FIELD_W - 26 - (cw + 6 + nw), 92, 1);
    }

    /* ---- level, and how many picks are still owed ----------------------- */
    {
        WBitmap *lv = atlas.Label(WF_LBL_LEVEL);
        int nw = numWidth(atlas, DIGIT_BIG, p.level);
        int total = lv->GetW() + 10 + nw;
        if (st.pending > 1) total += 24 + numWidth(atlas, DIGIT_SMALL, st.pending);
        int x = FIELD_W / 2 - total / 2;

        blit(cv, *lv, x, 148, BLIT_MASK);
        drawNumber(cv, atlas, DIGIT_BIG, p.level, x + lv->GetW() + 10, 144, 0);

        if (st.pending > 1) {
            /* still owed another pick after this one -- say so, or the player
               leaves the overlay thinking the level is spent */
            drawNumber(cv, atlas, DIGIT_SMALL, st.pending,
                       x + lv->GetW() + 10 + nw + 24, 152, 0);
        }
    }

    /* ---- three cards ---------------------------------------------------- */
    const int CW = 216, CH = 226, GAP = 22;
    const int x0 = (FIELD_W - (CW * 3 + GAP * 2)) / 2;
    const int y0 = 186;

    for (int i = 0; i < 3; ++i) {
        const int x  = x0 + i * (CW + GAP);
        const int cx = x + CW / 2;
        const int live = (i == sel);

        /* the number first, then its badge, then the card contents, then the
           card frame -- every layer has to precede the fill that sits under
           it (see drawBevel) */
        drawNumber(cv, atlas, DIGIT_SMALL, i + 1, x + 24,
                   y0 + 12 + (24 - atlas.DigitH(DIGIT_SMALL)) / 2, 1);
        drawBevel(cv, x + 12, y0 + 12, 24, 24,
                  live ? (unsigned long)0x241E10 : (unsigned long)COL_PANEL_BG,
                  live ? (unsigned long)COL_BANNER : (unsigned long)COL_PANEL_KEY,
                  (unsigned long)COL_PANEL_LIT, (unsigned long)COL_PANEL_SHADE);

        if (tab == 0) {
            int perk = w.PerkOffers()[i];
            if (perk >= 0 && perk < PERK_COUNT) {
                drawPips(cv, cx, y0 + 178, w.PerkTaken(perk), 5,
                         live ? (unsigned long)COL_BANNER
                              : (unsigned long)COL_UI_DIM);
                drawStatRow(cv, atlas, w, perk, cx, y0 + 136);
                drawRule(cv, x + 30, y0 + 122, CW - 60, 0x232C3C);
                blitCentered(cv, *atlas.Label(WF_LBL_PERKEFF0 + perk),
                             cx, y0 + 88, BLIT_MASK);
                blitCentered(cv, *atlas.Label(WF_LBL_PERK0 + perk),
                             cx, y0 + 52, BLIT_MASK);
            }
            drawCardBevel(cv, x, y0, CW, CH, live, 1,
                          (unsigned long)COL_BANNER);
            drawShadow(cv, x, y0, CW, CH, 6);
        } else {
            int price = w.ForgePrice(i);
            int ok    = (p.gold >= price);
            unsigned long accent = ok ? (unsigned long)COL_CYAN
                                      : (unsigned long)COL_DMG_HURT;

            drawPips(cv, cx, y0 + 178, w.ForgeBought(i), 5, accent);
            {
                /* the price is the whole decision, so it is the biggest thing
                   on the card */
                int cw = atlas.Coin() ? atlas.Coin()->GetW() : 0;
                int nw = numWidth(atlas, DIGIT_BIG, price);
                drawCoin(cv, atlas, price, cx - (cw + 6 + nw) / 2, y0 + 136, 1);
            }
            drawRule(cv, x + 30, y0 + 122, CW - 60, 0x232C3C);
            blitCentered(cv, *atlas.Label(WF_LBL_FORGEEFF0 + i),
                         cx, y0 + 88, BLIT_MASK);
            blitCentered(cv, *atlas.Label(WF_LBL_FORGE0 + i),
                         cx, y0 + 52, BLIT_MASK);

            if (!ok)          blitCentered(cv, *atlas.Label(WF_LBL_NOMONEY),
                                           cx, y0 + 198, BLIT_MASK);
            else if (live)    blitCentered(cv, *atlas.Label(WF_LBL_BUY),
                                           cx, y0 + 198, BLIT_MASK);

            drawCardBevel(cv, x, y0, CW, CH, live, ok, accent);
            drawShadow(cv, x, y0, CW, CH, 6);
        }
    }

    /* ---- hints ---------------------------------------------------------- */
    blitCentered(cv, *atlas.Label(WF_LBL_TABHINT), FIELD_W / 2, 442, BLIT_MASK);
    blitCentered(cv, *atlas.Label(WF_LBL_SELECT),  FIELD_W / 2, 470, BLIT_MASK);
    blitCentered(cv, *atlas.Label(WF_LBL_CONFIRM), FIELD_W / 2, 496, BLIT_MASK);

    /* Freeze the world behind the overlay.  Submitted LAST, which under the
       canvas's inverted order means it is applied FIRST -- so it dims the
       world and then the cards paint over it.  A WBox could not do this at
       all (its subtractive slot is NULL); see drawDim(). */
    drawDim(cv, 3);

    cv.SetPos(0, 0);
}

/* ------------------------------------------------------------ run summary */

void Renderer::DrawOver(WCanvas &cv, const World &w, const Atlas &atlas, int won)
{
    const Player &p  = w.Hero();
    const Stats  &st = w.State();

    /* The headline gets its own 48 px bake: this is the one line that ends a
       run, and the 24 px label font cannot carry it. */
    blitCentered(cv, *atlas.BannerArt(won ? 1 : 0), FIELD_W / 2, 76, BLIT_MASK);
    {
        int cx = FIELD_W / 2;
        drawRule(cv, cx - 150, 148, 300, 0x6A5A28);
        drawRule(cv, cx - 90,  148, 180,
                 won ? (unsigned long)COL_BANNER : (unsigned long)COL_HP);
    }

    const int PX = 208, PY = 170, PW = 384, PH = 262;
    const int LX = PX + 32;                 /* labels pinned left   */
    const int VX = PX + PW - 32;            /* figures pinned right */

    /* ---- wave: "第 N 波", right-aligned as a phrase ---------------------- */
    {
        WBitmap *a = atlas.Label(WF_LBL_WAVE_A);
        WBitmap *b = atlas.Label(WF_LBL_WAVE_B);
        int nw = numWidth(atlas, DIGIT_BIG, st.wave);
        int x  = VX - (a->GetW() + 8 + nw + 8 + b->GetW());
        blit(cv, *a, x, 194, BLIT_MASK);
        drawNumber(cv, atlas, DIGIT_BIG, st.wave, x + a->GetW() + 8, 190, 0);
        blit(cv, *b, x + a->GetW() + 8 + nw + 8, 194, BLIT_MASK);
    }

    /* ---- kills ---------------------------------------------------------- */
    blit(cv, *atlas.Label(WF_LBL_KILL), LX, 246, BLIT_MASK);
    drawNumber(cv, atlas, DIGIT_BIG, st.kills,
               VX - numWidth(atlas, DIGIT_BIG, st.kills), 242, 0);

    /* ---- gold ----------------------------------------------------------- */
    {
        blit(cv, *atlas.Label(WF_LBL_GOLD), LX, 298, BLIT_MASK);
        int cw = atlas.Coin() ? atlas.Coin()->GetW() : 0;
        int nw = numWidth(atlas, DIGIT_BIG, p.gold);
        drawCoin(cv, atlas, p.gold, VX - (cw + 6 + nw), 294, 1);
    }

    /* ---- time ----------------------------------------------------------- */
    {
        blit(cv, *atlas.Label(WF_LBL_TIME), LX, 350, BLIT_MASK);
        int secs = st.frame / 60;
        drawClock(cv, atlas, secs, VX - clockWidth(atlas, secs) / 2, 346);
    }

    /* row separators: they turn four loose lines into a table */
    drawRule(cv, PX + 20, 232, PW - 40, 0x232C3C);
    drawRule(cv, PX + 20, 284, PW - 40, 0x232C3C);
    drawRule(cv, PX + 20, 336, PW - 40, 0x232C3C);
    drawRule(cv, PX + 20, 388, PW - 40, 0x232C3C);

    /* the panel goes under its own contents */
    drawBevel(cv, PX, PY, PW, PH, (unsigned long)COL_PANEL_BG,
              (unsigned long)COL_PANEL_KEY, (unsigned long)COL_PANEL_LIT,
              (unsigned long)COL_PANEL_SHADE);
    drawShadow(cv, PX, PY, PW, PH, 7);

    /* ---- best run so far, on its own strip ------------------------------ */
    {
        int bw = atlas.LabelW(WF_LBL_BEST);
        int aw = atlas.LabelW(WF_LBL_WAVE_A);
        int ww = atlas.LabelW(WF_LBL_WAVE_B);
        int kw = atlas.LabelW(WF_LBL_KILL);
        int nw = numWidth(atlas, DIGIT_SMALL, w.BestWave());
        int kk = numWidth(atlas, DIGIT_SMALL, w.BestKills());
        int total = bw + 14 + aw + 6 + nw + 6 + ww + 18 + kw + 8 + kk;
        int x = FIELD_W / 2 - total / 2;
        const int SY = 452, SH = 36;
        int y = SY + 9;

        blit(cv, *atlas.Label(WF_LBL_BEST), x, y, BLIT_MASK); x += bw + 14;
        blit(cv, *atlas.Label(WF_LBL_WAVE_A), x, y, BLIT_MASK); x += aw + 6;
        drawNumber(cv, atlas, DIGIT_SMALL, w.BestWave(), x, y + 2, 0); x += nw + 6;
        blit(cv, *atlas.Label(WF_LBL_WAVE_B), x, y, BLIT_MASK); x += ww + 18;
        blit(cv, *atlas.Label(WF_LBL_KILL), x, y, BLIT_MASK); x += kw + 8;
        drawNumber(cv, atlas, DIGIT_SMALL, w.BestKills(), x, y + 2, 0);

        drawBevel(cv, FIELD_W / 2 - (total + 60) / 2, SY, total + 60, SH,
                  (unsigned long)COL_PANEL_BG, (unsigned long)COL_PANEL_KEY,
                  (unsigned long)COL_PANEL_LIT, (unsigned long)COL_PANEL_SHADE);
        drawShadow(cv, FIELD_W / 2 - (total + 60) / 2, SY, total + 60, SH, 4);
    }

    /* hints: R jumps straight back in, Enter goes to the menu */
    blitCentered(cv, *atlas.Label(WF_LBL_RESTART), FIELD_W / 2 - 118, 516, BLIT_MASK);
    blitCentered(cv, *atlas.Label(WF_LBL_BACK),    FIELD_W / 2 + 118, 516, BLIT_MASK);

    /* dim the frozen world -- submitted last, so it is applied first and the
       panel paints over it */
    drawDim(cv, 3);

    cv.SetPos(0, 0);
}

/* ------------------------------------------------------------------- draw */

Renderer::Renderer()
    : m_count(0), m_perkSel(0), m_tab(0), m_ox(0), m_oy(0), m_rng(0x2468ACE0u),
      m_mini(0), m_miniW(128), m_miniH(96), m_miniX(0), m_miniY(0),
      m_big(0), m_bigW(0), m_bigH(0), m_bigX(0), m_bigY(0), m_bigTile(10),
      m_bigHero(-1), m_dim(0)
{
}

Renderer::~Renderer()
{
    delete m_mini;
    m_mini = 0;
    delete m_big;
    m_big = 0;
    delete m_dim;
    m_dim = 0;
}

void Renderer::Reset()
{
    m_count   = 0;
    m_perkSel = 0;
    m_tab     = 0;
    m_rng     = 0x2468ACE0u;
}

void Renderer::Draw(WCanvas &cv, const World &w, const Atlas &atlas)
{
    m_count = 0;

    const Stats &st = w.State();

    int shx = 0, shy = 0;
    if (st.shake > 0) {
        shx = (int)(wf_rand(m_rng) % 9u) - 4;
        shy = (int)(wf_rand(m_rng) % 9u) - 4;
    }
    m_ox = (int)w.CameraX() - shx;
    m_oy = (int)w.CameraY() - shy;

    /* The HUD and the minimap are instrumentation for a live fight.  While a
       modal overlay is up the world is frozen behind a dim and the overlay
       reports the level, the gold and the run's numbers itself, so keeping
       the HUD would only be noise -- and its bottom-right charge boxes sit
       exactly where the overlay's hints go. */
    int modal = (st.app == APP_LEVELUP) || st.gameover;
    if (!modal) {
        drawHud(cv, w, atlas);      /* submitted first -> topmost           */
        drawMinimap(cv, w, atlas);  /* under the HUD, above everything else */
    }
    drawVignette(cv, atlas);        /* darkens the world, never the HUD     */

    /* ---- product overlays sit between the HUD and the world ------------- */
    if (st.app == APP_LEVELUP)     DrawPerks(cv, w, atlas, m_perkSel, m_tab);
    else if (st.gameover)          DrawOver(cv, w, atlas, st.app == APP_WIN);

    drawEffects(cv, w, atlas);
    drawEntities(cv, w, atlas);
    drawDrops(cv, w, atlas);
    drawTerrain(cv, w, atlas);      /* submitted last   -> bottom           */

    cv.SetPos(0, 0);
}

} /* namespace wf */
