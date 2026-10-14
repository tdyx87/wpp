/* ===========================================================================
 *  nb_core.cpp -- Neon Barrage : game logic
 *  No platform headers.  See nb_core.h.
 * ===========================================================================
 */
#include "nb_core.h"

#include <math.h>
#include <string.h>

namespace nb {

static const float NB_PI = 3.14159265358979f;

unsigned nb_rand(unsigned &state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float nb_randf(unsigned &state)
{
    return (float)(nb_rand(state) & 0xFFFFFFu) / (float)0x1000000u;
}

World::World()
{
    Reset();
}

void World::Reset()
{
    memset(m_bullets, 0, sizeof(m_bullets));
    memset(m_shots,   0, sizeof(m_shots));
    memset(m_enemies, 0, sizeof(m_enemies));

    memset(&m_ship, 0, sizeof(m_ship));
    m_ship.x       = FIELD_W * 0.5f;
    m_ship.y       = FIELD_H - 70.0f;
    m_ship.r       = 3.0f;
    m_ship.graze_r = 22.0f;
    m_ship.power   = 1;
    m_ship.alive   = 1;

    memset(&m_st, 0, sizeof(m_st));
    m_st.lives = 3;
    m_st.bombs = 3;
    m_st.wave  = 1;

    m_waveTimer    = 0;
    m_spawnTimer   = 0;
    m_spiralPhase  = 0.0f;
    m_bossPending  = 0;
    m_bulletCursor = 0;
    m_shotCursor   = 0;
    m_enemyCursor  = 0;
    m_rng          = 0x9E3779B9u;
}

/* ------------------------------------------------------------------ pools */

Bullet *World::allocBullet()
{
    for (int k = 0; k < MAX_BULLETS; ++k) {
        int i = m_bulletCursor + k;
        if (i >= MAX_BULLETS) i -= MAX_BULLETS;
        if (!m_bullets[i].alive) {
            m_bulletCursor = i + 1;
            if (m_bulletCursor >= MAX_BULLETS) m_bulletCursor = 0;
            return &m_bullets[i];
        }
    }
    return 0;
}

Bullet *World::allocShot()
{
    for (int k = 0; k < MAX_SHOTS; ++k) {
        int i = m_shotCursor + k;
        if (i >= MAX_SHOTS) i -= MAX_SHOTS;
        if (!m_shots[i].alive) {
            m_shotCursor = i + 1;
            if (m_shotCursor >= MAX_SHOTS) m_shotCursor = 0;
            return &m_shots[i];
        }
    }
    return 0;
}

Enemy *World::allocEnemy()
{
    for (int k = 0; k < MAX_ENEMIES; ++k) {
        int i = m_enemyCursor + k;
        if (i >= MAX_ENEMIES) i -= MAX_ENEMIES;
        if (!m_enemies[i].alive) {
            m_enemyCursor = i + 1;
            if (m_enemyCursor >= MAX_ENEMIES) m_enemyCursor = 0;
            return &m_enemies[i];
        }
    }
    return 0;
}

/* --------------------------------------------------------------- emitters */

static void initBullet(Bullet *b, float x, float y, float vx, float vy,
                       float r, unsigned long color, int shape)
{
    b->x     = x;
    b->y     = y;
    b->vx    = vx;
    b->vy    = vy;
    b->r     = r;
    b->color = color;
    b->shape = (unsigned char)shape;
    b->grazed = 0;
    b->alive = 1;
}

int World::EmitFan(float x, float y, int count, float spread, float angle,
                   float speed, unsigned long color, int shape)
{
    if (count <= 0) return 0;
    float step = (count > 1) ? spread / (float)(count - 1) : 0.0f;
    float a0   = angle - spread * 0.5f;

    int n = 0;
    for (int i = 0; i < count; ++i) {
        Bullet *b = allocBullet();
        if (!b) break;
        float a = a0 + step * (float)i;
        initBullet(b, x, y, cosf(a) * speed, sinf(a) * speed,
                   4.0f, color, shape);
        ++n;
        ++m_st.bulletsFired;
    }
    return n;
}

int World::EmitRing(float x, float y, int count, float phase, float speed,
                    unsigned long color, int shape)
{
    if (count <= 0) return 0;
    int n = 0;
    for (int i = 0; i < count; ++i) {
        Bullet *b = allocBullet();
        if (!b) break;
        float a = phase + (float)i * (2.0f * NB_PI / (float)count);
        initBullet(b, x, y, cosf(a) * speed, sinf(a) * speed,
                   5.0f, color, shape);
        ++n;
        ++m_st.bulletsFired;
    }
    return n;
}

int World::EmitAimed(float x, float y, float speed, unsigned long color, int shape)
{
    float dx = m_ship.x - x;
    float dy = m_ship.y - y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1e-3f) { dx = 0.0f; dy = 1.0f; d = 1.0f; }

    Bullet *b = allocBullet();
    if (!b) return 0;
    initBullet(b, x, y, dx / d * speed, dy / d * speed, 4.0f, color, shape);
    ++m_st.bulletsFired;
    return 1;
}

int World::EmitSpiral(float x, float y, int arms, float phase, float speed,
                      unsigned long color, int shape)
{
    if (arms <= 0) return 0;
    int n = 0;
    for (int i = 0; i < arms; ++i) {
        Bullet *b = allocBullet();
        if (!b) break;
        float a = phase + (float)i * (2.0f * NB_PI / (float)arms);
        initBullet(b, x, y, cosf(a) * speed, sinf(a) * speed,
                   5.0f, color, shape);
        ++n;
        ++m_st.bulletsFired;
    }
    return n;
}

int World::EmitPlayerShot(float x, float y, float vx, float vy, float r)
{
    Bullet *s = allocShot();
    if (!s) return 0;
    initBullet(s, x, y, vx, vy, r, COL_SHOT, SHAPE_DOT);
    ++m_st.shotsFired;
    return 1;
}

void World::SpawnEnemy(float x, float y, int hp, int pattern, int boss)
{
    Enemy *e = allocEnemy();
    if (!e) return;
    e->x       = x;
    e->y       = y;
    e->vx      = boss ? 1.1f : ((nb_rand(m_rng) & 1u) ? 0.9f : -0.9f);
    e->vy      = boss ? 0.0f : 0.30f;
    e->r       = boss ? 34.0f : 16.0f;
    e->hp      = hp;
    e->maxhp   = hp;
    e->age     = 0;
    e->pattern = pattern;
    e->boss    = boss;
    e->alive   = 1;
}

void World::ClearBullets()
{
    int n = 0;
    for (int i = 0; i < MAX_BULLETS; ++i) {
        if (m_bullets[i].alive) { m_bullets[i].alive = 0; ++n; }
    }
    if (n > 0) m_st.score += n * 20;
}

/* ------------------------------------------------------------------ counts */

int World::LiveBullets() const
{
    int n = 0;
    for (int i = 0; i < MAX_BULLETS; ++i) if (m_bullets[i].alive) ++n;
    return n;
}

int World::LiveShots() const
{
    int n = 0;
    for (int i = 0; i < MAX_SHOTS; ++i) if (m_shots[i].alive) ++n;
    return n;
}

int World::LiveEnemies() const
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; ++i) if (m_enemies[i].alive) ++n;
    return n;
}

float World::Difficulty() const
{
    float d = 1.0f + (float)(m_st.wave - 1) * 0.22f;
    if (d > 4.0f) d = 4.0f;
    return d;
}

/* ------------------------------------------------------------- simulation */

void World::stepPlayer(const Input &in)
{
    Ship &p = m_ship;
    if (!p.alive) return;

    float dx = in.dx;
    float dy = in.dy;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 1.0f) { dx /= len; dy /= len; }

    float speed = in.focus ? 1.9f : 4.3f;
    p.x += dx * speed;
    p.y += dy * speed;

    const float margin = 12.0f;
    if (p.x < margin) p.x = margin;
    if (p.x > FIELD_W - margin) p.x = FIELD_W - margin;
    if (p.y < margin) p.y = margin;
    if (p.y > FIELD_H - margin) p.y = FIELD_H - margin;

    if (p.invuln   > 0) --p.invuln;
    if (p.cooldown > 0) --p.cooldown;

    if (in.fire && p.cooldown == 0) {
        p.cooldown = 4;
        const float vy = -11.0f;
        if (p.power >= 3) {
            EmitPlayerShot(p.x,      p.y - 8.0f,  0.0f, vy, 3.0f);
            EmitPlayerShot(p.x - 7,  p.y - 2.0f, -1.4f, vy, 3.0f);
            EmitPlayerShot(p.x + 7,  p.y - 2.0f,  1.4f, vy, 3.0f);
        } else if (p.power == 2) {
            EmitPlayerShot(p.x - 5,  p.y - 6.0f,  0.0f, vy, 3.0f);
            EmitPlayerShot(p.x + 5,  p.y - 6.0f,  0.0f, vy, 3.0f);
        } else {
            EmitPlayerShot(p.x,      p.y - 8.0f,  0.0f, vy, 3.0f);
        }
    }

    if (in.bomb && m_st.bombs > 0) {
        --m_st.bombs;
        ClearBullets();
        m_st.bombFlash = 12;
        m_st.shake     = 14;
        p.invuln       = 90;
    }
}

void World::stepDirector()
{
    ++m_waveTimer;
    ++m_spawnTimer;

    if (m_waveTimer >= WAVE_LEN) {
        m_waveTimer  = 0;
        m_spawnTimer = 0;
        ++m_st.wave;
        m_bossPending = ((m_st.wave % 4) == 0) ? 1 : 0;
    }

    if (m_bossPending) {
        if (LiveEnemies() > 0) return;          /* wait for the field to clear */
        m_bossPending = 0;
        SpawnEnemy(FIELD_W * 0.5f, 100.0f,
                   120 + m_st.wave * 30, PAT_SPIRAL, 1);
        m_st.shake = 12;
        return;
    }

    int period = 90 - (m_st.wave - 1) * 6;
    if (period < 34) period = 34;
    if (m_spawnTimer < period) return;
    m_spawnTimer = 0;

    if (LiveEnemies() >= MAX_ENEMIES - 4) return;

    float x = 80.0f + nb_randf(m_rng) * (FIELD_W - 160.0f);
    float y = 40.0f + nb_randf(m_rng) * 90.0f;
    int   hp  = 4 + m_st.wave;
    int   pat = PAT_FAN + (int)(nb_rand(m_rng) % 4u);
    SpawnEnemy(x, y, hp, pat, 0);
}

void World::stepEnemies()
{
    const float diff = Difficulty();

    for (int i = 0; i < MAX_ENEMIES; ++i) {
        Enemy &e = m_enemies[i];
        if (!e.alive) continue;

        ++e.age;
        e.x += e.vx;
        e.y += e.vy;

        if (e.x < 40.0f)           { e.x = 40.0f;           e.vx = -e.vx; }
        if (e.x > FIELD_W - 40.0f) { e.x = FIELD_W - 40.0f; e.vx = -e.vx; }

        int fanPeriod    = (int)(70.0f / diff); if (fanPeriod    < 6) fanPeriod    = 6;
        int ringPeriod   = (int)(90.0f / diff); if (ringPeriod   < 8) ringPeriod   = 8;
        int aimedPeriod  = (int)(60.0f / diff); if (aimedPeriod  < 6) aimedPeriod  = 6;

        switch (e.pattern) {
        case PAT_FAN:
            if ((e.age % fanPeriod) == 0)
                EmitFan(e.x, e.y, 5, 0.55f, NB_PI * 0.5f,
                        2.6f + diff * 0.30f, COL_PAT_FAN, SHAPE_DOT);
            break;

        case PAT_RING:
            if ((e.age % ringPeriod) == 0)
                EmitRing(e.x, e.y, 14, m_spiralPhase,
                         2.2f + diff * 0.25f, COL_PAT_RING, SHAPE_DIAMOND);
            break;

        case PAT_SPIRAL:
            if ((e.age % 4) == 0)
                EmitSpiral(e.x, e.y, e.boss ? 3 : 1, m_spiralPhase,
                           2.9f, COL_PAT_SPIRAL, SHAPE_RING);
            break;

        case PAT_AIMED:
            if ((e.age % aimedPeriod) == 0)
                EmitAimed(e.x, e.y, 4.0f + diff * 0.50f,
                          COL_PAT_AIMED, SHAPE_SHARD);
            break;

        default:
            break;
        }
    }

    m_spiralPhase += 0.17f;
    if (m_spiralPhase > 2.0f * NB_PI) m_spiralPhase -= 2.0f * NB_PI;
}

void World::stepBullets()
{
    const float margin = 24.0f;

    for (int i = 0; i < MAX_BULLETS; ++i) {
        Bullet &b = m_bullets[i];
        if (!b.alive) continue;
        b.x += b.vx;
        b.y += b.vy;
        if (b.x < -margin || b.x > FIELD_W + margin ||
            b.y < -margin || b.y > FIELD_H + margin)
            b.alive = 0;
    }

    for (int i = 0; i < MAX_SHOTS; ++i) {
        Bullet &s = m_shots[i];
        if (!s.alive) continue;
        s.x += s.vx;
        s.y += s.vy;
        if (s.x < -margin || s.x > FIELD_W + margin ||
            s.y < -margin || s.y > FIELD_H + margin)
            s.alive = 0;
    }
}

void World::bumpCombo()
{
    ++m_st.combo;
    if (m_st.combo > m_st.maxCombo) m_st.maxCombo = m_st.combo;
    m_st.comboTimer = 150;
}

void World::hitPlayer()
{
    Ship &p = m_ship;

    --m_st.lives;
    m_st.combo      = 0;
    m_st.comboTimer = 0;
    m_st.hitFlash   = 10;
    m_st.shake      = 16;

    ClearBullets();

    if (m_st.lives <= 0) {
        m_st.lives = 0;
        p.alive    = 0;
        m_st.gameover = 1;
        return;
    }

    p.x      = FIELD_W * 0.5f;
    p.y      = FIELD_H - 70.0f;
    p.invuln = 150;
    if (p.power > 1) --p.power;
}

void World::collide()
{
    /* player shots vs enemies */
    for (int i = 0; i < MAX_SHOTS; ++i) {
        Bullet &s = m_shots[i];
        if (!s.alive) continue;

        for (int j = 0; j < MAX_ENEMIES; ++j) {
            Enemy &e = m_enemies[j];
            if (!e.alive) continue;

            float dx = e.x - s.x;
            float dy = e.y - s.y;
            float rr = e.r + s.r;
            if (dx * dx + dy * dy > rr * rr) continue;

            s.alive = 0;
            --e.hp;
            m_st.score += 10;

            if (e.hp <= 0) {
                e.alive = 0;
                ++m_st.kills;
                m_st.score += e.boss ? 5000 : 100;
                m_st.shake  = e.boss ? 18 : 4;
                bumpCombo();
                if (e.boss) {
                    ++m_st.bombs;
                    if (m_st.bombs > 5) m_st.bombs = 5;
                }
                if ((m_st.kills % 12) == 0 && m_ship.power < 3) ++m_ship.power;
            }
            break;
        }
    }

    Ship &p = m_ship;
    if (!p.alive || p.invuln > 0) return;

    /* enemy bullets vs player: tiny hitbox, generous graze ring */
    for (int i = 0; i < MAX_BULLETS; ++i) {
        Bullet &b = m_bullets[i];
        if (!b.alive) continue;

        float dx = b.x - p.x;
        float dy = b.y - p.y;
        float d2 = dx * dx + dy * dy;

        float hit = b.r + p.r;
        if (d2 <= hit * hit) {
            b.alive = 0;
            hitPlayer();
            return;
        }

        if (!b.grazed) {
            float gz = b.r + p.graze_r;
            if (d2 <= gz * gz) {
                b.grazed = 1;               /* each bullet grazes only once  */
                ++m_st.graze;
                ++m_st.grazedTotal;
                bumpCombo();
                m_st.score += 30 + m_st.combo * 5;
            }
        }
    }
}

void World::Step(const Input &in)
{
    if (m_st.gameover) return;

    ++m_st.frame;

    stepPlayer(in);
    stepDirector();
    stepEnemies();
    stepBullets();
    collide();

    if (m_st.hitFlash  > 0) --m_st.hitFlash;
    if (m_st.bombFlash > 0) --m_st.bombFlash;
    if (m_st.shake     > 0) --m_st.shake;

    if (m_st.comboTimer > 0) {
        --m_st.comboTimer;
        if (m_st.comboTimer == 0) m_st.combo = 0;
    }
}

} /* namespace nb */
