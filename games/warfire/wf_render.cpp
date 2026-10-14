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

/* WCanvas paints the link submitted first on top, so the filled part is
   submitted before the trough. */
static void drawBar(WCanvas &cv, int x, int y, int w, int h,
                    int fillW, unsigned long fg, unsigned long bg)
{
    /* a 1 px frame keeps the bar from reading as loose pixels on the terrain */
    cv.SetPos(x, y);
    cv.Draw(WBox(w, 1, WPixel((DWORD)COL_UI_EDGE)), BLIT_COPY);
    cv.SetPos(x, y + h - 1);
    cv.Draw(WBox(w, 1, WPixel((DWORD)COL_UI_EDGE)), BLIT_COPY);
    cv.SetPos(x, y);
    cv.Draw(WBox(1, h, WPixel((DWORD)COL_UI_EDGE)), BLIT_COPY);
    cv.SetPos(x + w - 1, y);
    cv.Draw(WBox(1, h, WPixel((DWORD)COL_UI_EDGE)), BLIT_COPY);

    if (fillW > 0) {
        cv.SetPos(x, y);
        cv.Draw(WBox(fillW, h, WPixel((DWORD)fg)), BLIT_COPY);
    }
    cv.SetPos(x, y);
    cv.Draw(WBox(w, h, WPixel((DWORD)bg)), BLIT_COPY);
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

void Renderer::drawHud(WCanvas &cv, const World &w, const Atlas &atlas)
{
    const Player &p  = w.Hero();
    const Stats  &st = w.State();

    /* ---- level and experience (top left) -------------------------------- */
    blit(cv, *atlas.Label(WF_LBL_LEVEL), 16, 10, BLIT_MASK);
    drawNumber(cv, atlas, DIGIT_SMALL, p.level,
               16 + atlas.LabelW(WF_LBL_LEVEL) + 8, 8, 0);

    blit(cv, *atlas.Label(WF_LBL_EXP), 16, 36, BLIT_MASK);
    {
        int bx = 16 + atlas.LabelW(WF_LBL_EXP) + 8;
        int bw = 190;
        int fill = (p.expNext > 0) ? (bw * p.exp / p.expNext) : 0;
        drawBar(cv, bx, 38, bw, 8, fill, COL_EXP, COL_UI_BG);
    }

    /* ---- gold and kills (top right) ------------------------------------- */
    {
        int rx = FIELD_W - 190;
        blit(cv, *atlas.Label(WF_LBL_GOLD), rx, 10, BLIT_MASK);
        drawNumber(cv, atlas, DIGIT_BIG, p.gold,
                   rx + atlas.LabelW(WF_LBL_GOLD) + 8, 4, 0);

        blit(cv, *atlas.Label(WF_LBL_KILL), rx, 44, BLIT_MASK);
        drawNumber(cv, atlas, DIGIT_SMALL, st.kills,
                   rx + atlas.LabelW(WF_LBL_KILL) + 8, 42, 0);
    }

    /* ---- wave banner (centre, top) -------------------------------------- */
    if (st.banner > 0) {
        int a = atlas.LabelW(WF_LBL_WAVE_A);
        int b = atlas.LabelW(WF_LBL_WAVE_B);
        char num[12];
        sprintf(num, "%d", st.bannerWave);

        /* fade out over the last 40 frames by blinking */
        int visible = (st.banner > 40) || ((st.banner / 4) % 2);
        if (visible) {
            int nw = 0;
            for (const char *q = num; *q; ++q) nw += atlas.DigitW(DIGIT_BIG, *q - '0');
            int total = a + nw + b + 16;
            int x = FIELD_W / 2 - total / 2;

            blit(cv, *atlas.Label(WF_LBL_WAVE_A), x, 58, BLIT_MASK);
            x += a + 8;
            drawNumber(cv, atlas, DIGIT_BIG, st.bannerWave, x, 52, 0);
            x += nw + 8;
            blit(cv, *atlas.Label(WF_LBL_WAVE_B), x, 58, BLIT_MASK);
        }
    }

    /* ---- how many are left this wave ------------------------------------ */
    if (st.banner <= 40 && !st.gameover) {
        int left = st.toSpawn + w.LiveMonsters();
        if (left > 0) {
            int lw = atlas.LabelW(WF_LBL_LEFT);
            char num[12];
            sprintf(num, "%d", left);
            int nw = 0;
            for (const char *q = num; *q; ++q) nw += atlas.DigitW(DIGIT_SMALL, *q - '0');
            int total = lw + nw + 8;
            int x = FIELD_W / 2 - total / 2;

            blit(cv, *atlas.Label(WF_LBL_LEFT), x, 96, BLIT_MASK);
            drawNumber(cv, atlas, DIGIT_SMALL, left, x + lw + 8, 94, 0);
        }
    }

    /* ---- health and rage (bottom left) ---------------------------------- */
    {
        int y = FIELD_H - 100;

        blit(cv, *atlas.Label(WF_LBL_HP), 16, y, BLIT_MASK);
        drawNumber(cv, atlas, DIGIT_SMALL, p.hp,
                   16 + atlas.LabelW(WF_LBL_HP) + 10, y + 2, 0);
        {
            int bw = 260;
            int fill = (p.maxhp > 0) ? (bw * p.hp / p.maxhp) : 0;
            drawBar(cv, 16, y + 26, bw, 16, fill, COL_HP, COL_HP_BG);
        }

        blit(cv, *atlas.Label(WF_LBL_MP), 16, y + 50, BLIT_MASK);
        {
            int bw = 200;
            int fill = (p.maxmp > 0) ? (bw * p.mp / p.maxmp) : 0;
            drawBar(cv, 16, y + 74, bw, 10, fill, COL_MP, COL_UI_BG);
        }
    }

    /* ---- game over ------------------------------------------------------ */
    if (st.gameover) {
        cv.SetPos(0, 0);
        cv.Draw(WBox(FIELD_W, FIELD_H, WPixel(0x000000)), BLIT_TRANS);
        ++m_count;

        blitCentered(cv, *atlas.Label(WF_LBL_DEAD), FIELD_W / 2, FIELD_H / 2 - 70, BLIT_MASK);
        blitCentered(cv, *atlas.Label(WF_LBL_RESTART), FIELD_W / 2, FIELD_H / 2 + 4, BLIT_MASK);
        drawNumber(cv, atlas, DIGIT_BIG, p.level, FIELD_W / 2, FIELD_H / 2 + 60, 1);
    }
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
    cv.SetPos(m_miniX - 1, m_miniY - 1);
    cv.Draw(WBox(m_miniW + 2, m_miniH + 2, WPixel((DWORD)COL_UI_BG)), BLIT_COPY);
    ++m_count;
    cv.SetPos(m_miniX - 2, m_miniY - 2);
    cv.Draw(WBox(m_miniW + 4, m_miniH + 4, WPixel((DWORD)COL_UI_EDGE)), BLIT_COPY);
    ++m_count;
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

/* ------------------------------------------------------------------- draw */

Renderer::Renderer()
    : m_count(0), m_ox(0), m_oy(0), m_rng(0x2468ACE0u),
      m_mini(0), m_miniW(128), m_miniH(96), m_miniX(0), m_miniY(0),
      m_big(0), m_bigW(0), m_bigH(0), m_bigX(0), m_bigY(0), m_bigTile(10), m_bigHero(-1)
{
}

Renderer::~Renderer()
{
    delete m_mini;
    m_mini = 0;
    delete m_big;
    m_big = 0;
}

void Renderer::Reset()
{
    m_count = 0;
    m_rng   = 0x2468ACE0u;
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

    drawHud(cv, w, atlas);          /* submitted first  -> topmost          */
    drawMinimap(cv, w, atlas);      /* under the HUD, above everything else */
    drawVignette(cv, atlas);        /* darkens the world, never the HUD     */
    drawEffects(cv, w, atlas);
    drawEntities(cv, w, atlas);
    drawDrops(cv, w, atlas);
    drawTerrain(cv, w, atlas);      /* submitted last   -> bottom           */

    cv.SetPos(0, 0);
}

} /* namespace wf */
