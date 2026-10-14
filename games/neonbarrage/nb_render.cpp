/* ===========================================================================
 *  nb_render.cpp -- Neon Barrage : renderer
 *  See nb_render.h for the draw-order contract.
 * ===========================================================================
 */
#include <windows.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"

#include "nb_render.h"

#include <stdio.h>
#include <string.h>

namespace nb {

/* Scale a 0xRRGGBB colour by num/den -- used for halos and dim fills. */
static unsigned long shade(unsigned long c, int num, int den)
{
    unsigned r = ((c >> 16) & 0xFFu) * (unsigned)num / (unsigned)den;
    unsigned g = ((c >>  8) & 0xFFu) * (unsigned)num / (unsigned)den;
    unsigned b = ( c        & 0xFFu) * (unsigned)num / (unsigned)den;
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return (r << 16) | (g << 8) | b;
}

Renderer::Renderer()
    : m_count(0), m_blockCount(0), m_rng(0x1234567u), m_offX(0), m_offY(0)
{
    memset(m_blocks, 0, sizeof(m_blocks));
    Reset();
}

Renderer::~Renderer()
{
    for (int i = 0; i < m_blockCount; ++i) {
        delete m_blocks[i].bmp;
        m_blocks[i].bmp = 0;
    }
    m_blockCount = 0;
}

void Renderer::Reset()
{
    m_count = 0;
    m_rng   = 0x1234567u;

    for (int i = 0; i < STAR_COUNT; ++i) {
        Star &s = m_stars[i];
        s.x = (float)(nb_rand(m_rng) % (unsigned)FIELD_W);
        s.y = (float)(nb_rand(m_rng) % (unsigned)FIELD_H);
        int layer = i % 3;
        s.speed = 0.6f + (float)layer * 1.1f;
        s.color = (layer == 0) ? 0x203050UL
                : (layer == 1) ? 0x406080UL
                               : 0x90C0E0UL;
    }
}

/* --------------------------------------------------------------- blocks */

const WBitmap *Renderer::block(int w, int h, unsigned long color)
{
    if (w <= 0 || h <= 0) return 0;

    for (int i = 0; i < m_blockCount; ++i) {
        const Block &b = m_blocks[i];
        if (b.w == w && b.h == h && b.color == color) return b.bmp;
    }
    if (m_blockCount >= MAX_BLOCKS) return 0;

    WBitmap *bmp = new WBitmap();
    if (bmp->Create(w, h) != WS_OK) {
        delete bmp;
        return 0;
    }
    bmp->Clear(WPixel((DWORD)color));

    Block &b = m_blocks[m_blockCount++];
    b.w     = w;
    b.h     = h;
    b.color = color;
    b.bmp   = bmp;
    return bmp;
}

void Renderer::push(int cx, int cy, int w, int h, unsigned long color, int style)
{
    if (m_count >= MAX_ITEMS) return;

    const WBitmap *b = block(w, h, color);
    if (!b) return;

    Item &it = m_items[m_count++];
    it.x     = cx - w / 2 + m_offX;
    it.y     = cy - h / 2 + m_offY;
    it.bmp   = b;
    it.style = (unsigned char)style;
}

/* ----------------------------------------------------------- scene parts */

void Renderer::buildStars()
{
    for (int i = 0; i < STAR_COUNT; ++i) {
        Star &s = m_stars[i];
        s.y += s.speed;
        if (s.y >= (float)FIELD_H) {
            s.y = 0.0f;
            s.x = (float)(nb_rand(m_rng) % (unsigned)FIELD_W);
        }
        int size = (s.speed > 1.6f) ? 2 : 1;
        push((int)s.x, (int)s.y, size, size, s.color, BLIT_ADDITIVE);
    }
}

void Renderer::buildEnemies(const World &w)
{
    const Enemy *arr = w.Enemies();

    for (int i = 0; i < MAX_ENEMIES; ++i) {
        const Enemy &e = arr[i];
        if (!e.alive) continue;

        int x = (int)e.x;
        int y = (int)e.y;
        int r = (int)e.r;

        unsigned long core = e.boss ? (unsigned long)COL_BOSS : (unsigned long)COL_ENEMY;
        unsigned long dim  = e.boss ? (unsigned long)COL_BOSS_DIM : (unsigned long)COL_ENEMY_DIM;

        push(x, y, r * 3, r * 3, dim,               BLIT_ADDITIVE);
        push(x, y, r * 2, r * 2, shade(core, 1, 2), BLIT_ADDITIVE);
        push(x, y, r,     r,     core,              BLIT_COPY);

        if (e.boss) {
            int bw = r * 4;
            int by = y - r - 14;
            int fill = (e.maxhp > 0) ? (bw * e.hp / e.maxhp) : 0;
            push(x, by, bw, 4, 0x202028UL, BLIT_COPY);
            if (fill > 0)
                push(x - bw / 2 + fill / 2, by, fill, 4, COL_BOSS, BLIT_COPY);
        }
    }
}

void Renderer::buildShots(const World &w)
{
    const Bullet *arr = w.Shots();

    for (int i = 0; i < MAX_SHOTS; ++i) {
        const Bullet &s = arr[i];
        if (!s.alive) continue;

        int x = (int)s.x;
        int y = (int)s.y;

        push(x, y, 10, 10, shade(COL_SHOT, 1, 3), BLIT_ADDITIVE);
        push(x, y, 4, 16, COL_SHOT, BLIT_ADDITIVE);
    }
}

void Renderer::buildBullets(const World &w)
{
    const Bullet *arr = w.Bullets();
    const int live = w.LiveBullets();

    /* Halos are expensive (an extra item per bullet), so drop them once the
       screen gets busy -- the game still reads clearly without them. */
    const int wantGlow = (live <= 400);
    int drawn = 0;

    for (int i = 0; i < MAX_BULLETS && drawn < 1400; ++i) {
        const Bullet &b = arr[i];
        if (!b.alive) continue;
        ++drawn;

        int x = (int)b.x;
        int y = (int)b.y;

        int core;
        switch (b.shape) {
        case SHAPE_DIAMOND: core = 8;  break;
        case SHAPE_RING:    core = 10; break;
        case SHAPE_SHARD:   core = 6;  break;
        default:            core = 6;  break;
        }

        if (wantGlow) {
            int g = core * 3;
            push(x, y, g, g, shade(b.color, 1, 4), BLIT_ADDITIVE);
        }
        push(x, y, core, core, b.color, BLIT_COPY);
    }
}

void Renderer::buildPlayer(const World &w)
{
    const Ship &p = w.Player();
    if (!p.alive) return;

    /* blink while respawning */
    if (p.invuln > 0 && ((p.invuln / 3) % 2) == 0) return;

    int x = (int)p.x;
    int y = (int)p.y;

    push(x, y + 16, 12, 10, shade(COL_PLAYER, 1, 3), BLIT_ADDITIVE);   /* thruster */
    push(x - 10, y + 6, 12, 8, shade(COL_PLAYER, 2, 3), BLIT_ADDITIVE);
    push(x + 10, y + 6, 12, 8, shade(COL_PLAYER, 2, 3), BLIT_ADDITIVE);
    push(x, y, 12, 22, COL_PLAYER, BLIT_COPY);                         /* body     */
    push(x, y - 4, 5, 5, COL_CORE, BLIT_COPY);                         /* hitbox   */
}

void Renderer::buildScene(const World &w)
{
    const Stats &st = w.State();
    m_count = 0;

    /* --- bottom layer ---------------------------------------------------- */
    /* Slightly oversized so a screen shake never exposes the clear colour. */
    push(FIELD_W / 2, FIELD_H / 2, FIELD_W + 16, FIELD_H + 16, COL_BG, BLIT_COPY);

    for (int y = 40; y < FIELD_H; y += 40)
        push(FIELD_W / 2, y, FIELD_W, 1, COL_GRID, BLIT_ADDITIVE);
    for (int x = 40; x < FIELD_W; x += 40)
        push(x, FIELD_H / 2, 1, FIELD_H, COL_GRID, BLIT_ADDITIVE);

    buildStars();

    /* --- actors ---------------------------------------------------------- */
    /* Painter order, bottom -> top.  Note the ship sits *under* the bullets:
       in a dense pattern the player has to be able to see the bullet that is
       about to hit them even when it overlaps the sprite, and the hitbox dot
       is only 5x5 anyway.  Enemies go under the bullets for the same reason. */
    buildEnemies(w);
    buildPlayer(w);
    buildShots(w);
    buildBullets(w);

    /* --- full screen effects (topmost in the world) ---------------------- */
    if (st.hitFlash > 0)
        push(FIELD_W / 2, FIELD_H / 2, FIELD_W, FIELD_H,
             shade(0xFFFFFFUL, st.hitFlash, 14), BLIT_ADDITIVE);

    if (st.bombFlash > 0)
        push(FIELD_W / 2, FIELD_H / 2, FIELD_W, FIELD_H,
             shade(0x9CFFFFUL, st.bombFlash, 16), BLIT_ADDITIVE);
}

/* --------------------------------------------------------------- output */

void Renderer::drawHud(WCanvas &canvas, const World &w)
{
    const Stats &st = w.State();
    char buf[128];

    canvas.SetPos(8, 8);
    sprintf(buf, "SCORE %07d", st.score);
    canvas.Draw(buf, WPixel((DWORD)COL_HUD));

    canvas.SetPos(8, 22);
    sprintf(buf, "LIFE %d   BOMB %d   WAVE %d", st.lives, st.bombs, st.wave);
    canvas.Draw(buf, WPixel((DWORD)COL_HUD));

    canvas.SetPos(8, 36);
    sprintf(buf, "GRAZE %d   POWER %d", st.graze, w.Player().power);
    canvas.Draw(buf, WPixel((DWORD)COL_HUD_DIM));

    if (st.combo >= 3) {
        sprintf(buf, "COMBO x%d", st.combo);
        canvas.SetPos(FIELD_W - 8 - (int)strlen(buf) * 7, 8);
        canvas.Draw(buf, WPixel((DWORD)COL_BOSS));
    }

    if (st.gameover) {
        canvas.SetPos(FIELD_W / 2 - 4 * 7 * 2, FIELD_H / 2 - 20);
        canvas.Draw("GAME  OVER", WPixel((DWORD)COL_WARN));

        sprintf(buf, "MAX COMBO %d   TOTAL GRAZE %d", st.maxCombo, st.grazedTotal);
        canvas.SetPos(FIELD_W / 2 - (int)strlen(buf) * 7 / 2, FIELD_H / 2 + 4);
        canvas.Draw(buf, WPixel((DWORD)COL_HUD));
    }
}

void Renderer::flush(WCanvas &canvas)
{
    /* Reverse order: the first link WCanvas receives is the one it applies
       last, so submitting back-to-front yields ordinary painter ordering. */
    for (int i = m_count - 1; i >= 0; --i) {
        const Item &it = m_items[i];
        canvas.SetPos(it.x, it.y);
        canvas.Draw(*it.bmp, it.style);
    }
    canvas.SetPos(0, 0);
}

void Renderer::Draw(WCanvas &canvas, const World &w)
{
    const Stats &st = w.State();

    if (st.shake > 0) {
        m_offX = (int)(nb_rand(m_rng) % 7u) - 3;
        m_offY = (int)(nb_rand(m_rng) % 7u) - 3;
    } else {
        m_offX = 0;
        m_offY = 0;
    }

    buildScene(w);        /* display list, painter order (bottom -> top)  */
    drawHud(canvas, w);   /* submitted first, so it ends up on top        */
    flush(canvas);        /* everything else, submitted in reverse        */
}

} /* namespace nb */
