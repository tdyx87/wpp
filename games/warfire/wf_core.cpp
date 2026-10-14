/* ===========================================================================
 *  wf_core.cpp -- Warfire : game logic
 *  No platform headers.  See wf_core.h.
 * ===========================================================================
 */
#include "wf_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace wf {

/* ---------------------------------------------------------------- tuning */

struct MonDef {
    int   hp;
    int   atk;
    float speed;
    float radius;
    float reach;
    int   windup;
    int   strike;
    int   recover;
    int   cool;
    int   exp;
    int   goldMin, goldMax;
};

static const MonDef MON_DEF[MON_KIND_COUNT] = {
    /* GRUNT  */ {  30,   8, 2.10f, 12.0f, 26.0f, 24,  5, 20,  70,  12,   3,   8 },
    /* ARCHER */ {  24,   7, 1.70f, 11.0f, 24.0f, 26,  5, 24, 100,  14,   4,  10 },
    /* BRUTE  */ {  90,  20, 1.30f, 16.0f, 34.0f, 42,  7, 30, 140,  30,  12,  20 },
    /* BOSS   */ { 600,  26, 1.55f, 26.0f, 48.0f, 36,  8, 34, 120, 200,  90, 160 }
};

static const int   SWING_WINDUP[4]  = { 0,  6,  6,  9 };
static const int   SWING_ACTIVE[4]  = { 0,  5,  5,  7 };
static const int   SWING_RECOVER[4] = { 0,  9,  9, 16 };
static const float SWING_RANGE[4]   = { 0.0f, 30.0f, 32.0f, 42.0f };
static const float SWING_OFFSET[4]  = { 0.0f, 18.0f, 20.0f, 24.0f };
static const float SWING_MUL[4]     = { 0.0f, 1.0f, 1.15f, 1.7f };

/* perk: attack timers scaled by the hero's attack-speed multiplier */
static int atkT(int base, float mul)
{
    int t = (int)(base * mul + 0.5f);
    return t < 1 ? 1 : t;
}

static const float HERO_ACCEL    = 1.8f;
static const float HERO_MAXSPD   = 5.0f;
static const float HERO_FRICTION = 0.74f;
static const float HERO_RADIUS   = 9.0f;

static const int   DASH_LEN   = 12;
static const int   DASH_CD    = 42;
static const float DASH_SPEED = 11.5f;

static const int   SPIN_LEN   = 34;
static const int   SPIN_CD    = 300;
static const int   SPIN_COST  = 30;
static const int   SPIN_TICK  = 5;          /* hit test every N frames       */
static const float SPIN_RANGE = 62.0f;

static const float CRIT_CHANCE = 0.18f;
static const float CRIT_MULT   = 2.0f;

/* ---- the forge (gold sink) ---------------------------------------------
   Base cost of each item and how much dearer it gets with every purchase.
   A full 10-wave run yields about 1460 gold; the growth rates below are
   tuned so a run buys roughly six or seven things in total and can never
   have all of them -- that is what makes the gold a decision instead of a
   counter.  Before this table existed, gold had no sink at all. */
static const int   FORGE_BASE[FORGE_COUNT]   = { 60, 90, 30 };
static const float FORGE_GROWTH[FORGE_COUNT] = { 1.55f, 1.60f, 1.30f };
static const int   FORGE_TEMPER_ATK = 4;      /* +attack per temper        */
static const float FORGE_PLATE_MUL  = 0.92f;  /* -8% damage taken each     */
static const float FORGE_PLATE_MIN  = 0.45f;  /* armour floor              */
static const int   FORGE_ELIXIR_HP  = 60;     /* instant heal              */

/* ---- per-wave enemy scaling --------------------------------------------
   The run used to get EASIER as it went.  The hero's attack grows about
   3.4x across ten waves while a monster's stats never moved at all, so the
   time to kill one monster fell from 3.0 s to 1.2 s and only the rising
   head-count kept the waves long.  These two slopes put the threat back: on
   wave 10 a monster carries 2.98x the hp and 1.90x the attack of its wave-1
   self.  Both are exactly 1.0 on wave 1, so nothing spawned by a test on a
   fresh World changes. */
static const float WAVE_HP_SLOPE  = 0.22f;
static const float WAVE_ATK_SLOPE = 0.10f;

static inline int IsBlocking(int t)
{
    return t == TILE_ROCK || t == TILE_WATER || t == TILE_TREE;
}

/* ------------------------------------------------------------------- rng */

unsigned wf_rand(unsigned &state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float wf_randf(unsigned &state)
{
    return (float)(wf_rand(state) & 0xFFFFFFu) / (float)0x1000000u;
}

/* ----------------------------------------------------------------- pools */

Monster *World::allocMonster()
{
    for (int k = 0; k < MAX_MONSTERS; ++k) {
        int i = m_monCursor + k;
        if (i >= MAX_MONSTERS) i -= MAX_MONSTERS;
        if (!m_mons[i].alive) { m_monCursor = (i + 1) % MAX_MONSTERS; return &m_mons[i]; }
    }
    return 0;
}

Arrow *World::allocArrow()
{
    for (int k = 0; k < MAX_ARROWS; ++k) {
        int i = m_arrowCursor + k;
        if (i >= MAX_ARROWS) i -= MAX_ARROWS;
        if (!m_arrows[i].alive) { m_arrowCursor = (i + 1) % MAX_ARROWS; return &m_arrows[i]; }
    }
    return 0;
}

Fx *World::allocFx()
{
    for (int k = 0; k < MAX_FX; ++k) {
        int i = m_fxCursor + k;
        if (i >= MAX_FX) i -= MAX_FX;
        if (!m_fx[i].alive) { m_fxCursor = (i + 1) % MAX_FX; return &m_fx[i]; }
    }
    return 0;
}

Drop *World::allocDrop()
{
    for (int k = 0; k < MAX_DROPS; ++k) {
        int i = m_dropCursor + k;
        if (i >= MAX_DROPS) i -= MAX_DROPS;
        if (!m_drops[i].alive) { m_dropCursor = (i + 1) % MAX_DROPS; return &m_drops[i]; }
    }
    return 0;
}

/* ------------------------------------------------------------------ world */

World::World()
{
    m_bestWave  = 0;
    m_bestKills = 0;
    m_save      = 0;
    m_offer[0] = m_offer[1] = m_offer[2] = -1;
    for (int i = 0; i < PERK_COUNT; ++i) m_perkCount[i] = 0;
    Reset();
}

void World::Reset()
{
    memset(m_mons,   0, sizeof(m_mons));
    memset(m_arrows, 0, sizeof(m_arrows));
    memset(m_fx,     0, sizeof(m_fx));
    memset(m_drops,  0, sizeof(m_drops));

    m_monCursor   = 0;
    m_arrowCursor = 0;
    m_fxCursor    = 0;
    m_dropCursor  = 0;
    m_rng         = 0xC0FFEEu;

    memset(&m_hero, 0, sizeof(m_hero));
    m_hero.x        = WORLD_W * 0.5f;
    m_hero.y        = WORLD_H * 0.5f;
    m_hero.maxhp    = 120;
    m_hero.hp       = 120;
    m_hero.maxmp    = 100;
    m_hero.mp       = 100;
    m_hero.level    = 1;
    m_hero.expNext  = 60;
    m_hero.atk      = 10;
    m_hero.facing   = FACE_DOWN;
    m_hero.alive    = 1;

    /* perk buffs start neutral (Reset is called every run) */
    m_hero.spdMul       = 1.0f;
    m_hero.atkSpdMul    = 1.0f;
    m_hero.critAdd      = 0.0f;
    m_hero.dashCdMul    = 1.0f;
    m_hero.spinCdMul    = 1.0f;
    m_hero.spinRangeAdd = 0.0f;
    m_hero.dmgMul       = 1.0f;
    m_hero.lifesteal    = 0;

    memset(&m_st, 0, sizeof(m_st));
    m_st.wave    = 0;
    m_st.app     = APP_PLAY;
    m_st.pending = 0;

    for (int i = 0; i < PERK_COUNT; ++i) m_perkCount[i] = 0;
    for (int i = 0; i < FORGE_COUNT; ++i) m_forgeCount[i] = 0;
    m_offer[0] = m_offer[1] = m_offer[2] = -1;
    m_waveHpMul = m_waveAtkMul = 1.0f;

    buildMap();
    StartWave(1);
    updateFlow();
}

/* Deterministic tile map: a grass/dirt base, then clustered props.  The
   spawn plaza in the middle is always kept clear. */
void World::buildMap()
{
    unsigned rng = 0x5EED1234u;

    for (int i = 0; i < MAP_W * MAP_H; ++i)
        m_tile[i] = ((wf_rand(rng) & 7u) == 0u) ? TILE_DIRT : TILE_GRASS;

    for (int n = 0; n < 520; ++n) {
        int bx = (int)(wf_rand(rng) % (unsigned)MAP_W);
        int by = (int)(wf_rand(rng) % (unsigned)MAP_H);
        unsigned r = wf_rand(rng) % 100u;
        int t = (r < 40u) ? TILE_ROCK : (r < 72u ? TILE_TREE : TILE_WATER);
        int w = 1 + (int)(wf_rand(rng) % 3u);
        int h = 1 + (int)(wf_rand(rng) % 3u);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                int tx = bx + i, ty = by + j;
                if (tx >= 0 && tx < MAP_W && ty >= 0 && ty < MAP_H)
                    m_tile[ty * MAP_W + tx] = (unsigned char)t;
            }
    }

    /* keep a 10x10 plaza clear around the spawn point */
    int cx = MAP_W / 2, cy = MAP_H / 2;
    for (int ty = cy - 5; ty <= cy + 5; ++ty)
        for (int tx = cx - 5; tx <= cx + 5; ++tx)
            if (tx >= 0 && tx < MAP_W && ty >= 0 && ty < MAP_H)
                m_tile[ty * MAP_W + tx] = TILE_GRASS;
}

unsigned char World::TileAt(int tx, int ty) const
{
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) return TILE_ROCK;
    return m_tile[ty * MAP_W + tx];
}

int World::TileAtWorld(float wx, float wy) const
{
    return (int)TileAt((int)(wx / (float)TILE_SIZE), (int)(wy / (float)TILE_SIZE));
}

int World::Blocked(float wx, float wy) const
{
    return IsBlocking(TileAtWorld(wx, wy));
}

/* A monster's body is 14 px of the tile grid either side of its centre, so a
   centre that is merely inside a walkable tile can still be buried in a rock. */
static bool FootprintClear(const World &w, float x, float y)
{
    const float r = 14.0f;
    return !w.Blocked(x, y)
        && !w.Blocked(x - r, y) && !w.Blocked(x + r, y)
        && !w.Blocked(x, y - r) && !w.Blocked(x, y + r);
}

bool World::FreeSpot(float cx, float cy, float ang, float r,
                     float &ox, float &oy) const
{
    const int   fan   = 24;
    const float step  = 6.2831853f / (float)fan;

    for (int pass = 0; pass < 2; ++pass) {
        /* pass 0 keeps the whole body off the wall, pass 1 only the centre --
           a cramped map should still get monsters rather than none. */
        for (int i = 0; i < fan; ++i) {
            int   k = (i + 1) / 2;
            float a = ang + ((i & 1) ? -1.0f : 1.0f) * (float)k * step;
            float x = cx + cosf(a) * r;
            float y = cy + sinf(a) * r;

        if (x < 48.0f || x > (float)WORLD_W - 48.0f) continue;
        if (y < 48.0f || y > (float)WORLD_H - 48.0f) continue;
        if (pass == 0 ? !FootprintClear(*this, x, y) : Blocked(x, y)) continue;

        ox = x;
        oy = y;
        return true;
    }
    }
    return false;
}

/* --------------------------------------------------------------- pathing */
/* Flood the walkable tiles from the hero (BFS).  Every tile the hero could
   ever walk to gets its distance to the hero and a parent pointing one step
   back toward him.  That gradient is what both lets monsters steer around
   walls and lets SpawnPos() refuse to put a monster where the hero can never
   reach it.  ~3000 tiles, so it costs next to nothing per frame. */
void World::updateFlow()
{
    const int N = MAP_W * MAP_H;
    for (int i = 0; i < N; ++i) { m_dist[i] = -1; m_next[i] = -1; }

    int hx = (int)(m_hero.x / (float)TILE_SIZE);
    int hy = (int)(m_hero.y / (float)TILE_SIZE);
    if (hx < 0) hx = 0; else if (hx >= MAP_W) hx = MAP_W - 1;
    if (hy < 0) hy = 0; else if (hy >= MAP_H) hy = MAP_H - 1;

    static int queue[MAP_W * MAP_H];
    int qh = 0, qt = 0;
    int start = hy * MAP_W + hx;
    m_dist[start] = 0;
    queue[qt++] = start;

    static const int DX[4] = { 1, -1, 0, 0 };
    static const int DY[4] = { 0, 0, 1, -1 };

    while (qh < qt) {
        int cur = queue[qh++];
        int cx = cur % MAP_W;
        int cy = cur / MAP_W;
        for (int k = 0; k < 4; ++k) {
            int nx = cx + DX[k];
            int ny = cy + DY[k];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            int nidx = ny * MAP_W + nx;
            if (m_dist[nidx] != -1) continue;
            /* the hero's own tile is allowed even if it is (rarely) inside a
               wall, so a monster always has a goal to walk toward */
            if (IsBlocking(TileAt(nx, ny)) && !(nx == hx && ny == hy)) continue;
            m_dist[nidx] = m_dist[cur] + 1;
            m_next[nidx] = cur;
            queue[qt++] = nidx;
        }
    }
}

int World::DistAt(int tx, int ty) const
{
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) return -1;
    return m_dist[ty * MAP_W + tx];
}

int World::NextAt(int idx) const
{
    if (idx < 0 || idx >= MAP_W * MAP_H) return -1;
    return m_next[idx];
}

/* The point a monster should walk toward: the centre of the next tile on the
   path to the hero, so it folds around rock/water/tree instead of jamming
   against them.  A monster wedged in a wall (or with no path) simply aims
   straight at the hero; resolveMonsterTiles() pops it back out of the wall on
   the same frame, after which the field takes over again. */
static void flowGoal(const World &w, const Monster &m, float &gx, float &gy)
{
    int tx = (int)(m.x / (float)TILE_SIZE);
    int ty = (int)(m.y / (float)TILE_SIZE);
    if (tx < 0) tx = 0; else if (tx >= MAP_W) tx = MAP_W - 1;
    if (ty < 0) ty = 0; else if (ty >= MAP_H) ty = MAP_H - 1;

    int idx = ty * MAP_W + tx;
    if (w.DistAt(tx, ty) > 0) {
        int nx = w.NextAt(idx);
        if (nx >= 0) {
            gx = (float)(nx % MAP_W) * TILE_SIZE + TILE_SIZE * 0.5f;
            gy = (float)(nx / MAP_W) * TILE_SIZE + TILE_SIZE * 0.5f;
            return;
        }
    }
    gx = w.Hero().x;
    gy = w.Hero().y;
}

/* The world position a new monster spawns at: on the ring, but only where the
   flood from the hero actually reached and the 14 px body fits.  Falls back to
   the nearest such tile if the ring is blocked, and (last resort) to any open
   ground so a wave never fails to spawn.  This is what keeps every live
   monster de-Reachable -- i.e. catchable -- for the rest of its life. */
bool World::SpawnPos(float cx, float cy, float r0, float r1,
                     float &ox, float &oy) const
{
    const int ANGLES = 40, RADII = 6;
    for (int ai = 0; ai < ANGLES; ++ai) {
        float a = (float)ai * 6.2831853f / (float)ANGLES;
        for (int ri = 0; ri < RADII; ++ri) {
            float t   = (RADII > 1) ? (float)ri / (float)(RADII - 1) : 0.0f;
            float rr  = r0 + (r1 - r0) * t;
            float x   = cx + cosf(a) * rr;
            float y   = cy + sinf(a) * rr;
            if (x < 48.0f || x > (float)WORLD_W - 48.0f) continue;
            if (y < 48.0f || y > (float)WORLD_H - 48.0f) continue;
            int tx = (int)(x / (float)TILE_SIZE);
            int ty = (int)(y / (float)TILE_SIZE);
            if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) continue;
            if (m_dist[ty * MAP_W + tx] < 0) continue;        /* walled in */
            if (!FootprintClear(*this, x, y)) continue;
            ox = x; oy = y;
            return true;
        }
    }

    /* not on the ring: the nearest reachable tile at least 120 px away */
    float best = -1.0f, bx = 0.0f, by = 0.0f;
    for (int ty = 0; ty < MAP_H; ++ty)
        for (int tx = 0; tx < MAP_W; ++tx) {
            if (m_dist[ty * MAP_W + tx] < 0) continue;
            float wx = (float)tx * TILE_SIZE + TILE_SIZE * 0.5f;
            float wy = (float)ty * TILE_SIZE + TILE_SIZE * 0.5f;
            float ddx = wx - cx, ddy = wy - cy;
            float dd  = ddx * ddx + ddy * ddy;
            if (dd < 120.0f * 120.0f) continue;
            if (best < 0.0f || dd < best) { best = dd; bx = wx; by = wy; }
        }
    if (best >= 0.0f) { ox = bx; oy = by; return true; }

    return false;
}

float World::CameraX() const
{
    float cx = m_hero.x - FIELD_W * 0.5f;
    if (cx < 0.0f) cx = 0.0f;
    if (cx > (float)(WORLD_W - FIELD_W)) cx = (float)(WORLD_W - FIELD_W);
    return cx;
}

float World::CameraY() const
{
    float cy = m_hero.y - FIELD_H * 0.5f;
    if (cy < 0.0f) cy = 0.0f;
    if (cy > (float)(WORLD_H - FIELD_H)) cy = (float)(WORLD_H - FIELD_H);
    return cy;
}

/* ----------------------------------------------------------------- counts */

int World::LiveMonsters() const
{
    int n = 0;
    for (int i = 0; i < MAX_MONSTERS; ++i) if (m_mons[i].alive) ++n;
    return n;
}

int World::LiveArrows() const
{
    int n = 0;
    for (int i = 0; i < MAX_ARROWS; ++i) if (m_arrows[i].alive) ++n;
    return n;
}

int World::LiveDrops() const
{
    int n = 0;
    for (int i = 0; i < MAX_DROPS; ++i) if (m_drops[i].alive) ++n;
    return n;
}

int World::LiveFx() const
{
    int n = 0;
    for (int i = 0; i < MAX_FX; ++i) if (m_fx[i].alive) ++n;
    return n;
}

/* ---------------------------------------------------------------- spawning */

void World::SpawnMonster(int kind, float x, float y)
{
    Monster *m = allocMonster();
    if (!m) return;

    const MonDef &d = MON_DEF[kind];
    memset(m, 0, sizeof(*m));

    /* Wave scaling.  Both multipliers are exactly 1.0 on wave 1, so a test
       that spawns straight into a fresh World still gets the raw MON_DEF
       numbers; only waves 2+ are scaled up. */
    int hp  = (int)((float)d.hp  * m_waveHpMul  + 0.5f);
    int atk = (int)((float)d.atk * m_waveAtkMul + 0.5f);
    if (hp  < 1) hp  = 1;
    if (atk < 1) atk = 1;

    m->x      = x;
    m->y      = y;
    m->kind   = kind;
    m->hp     = hp;
    m->maxhp  = hp;
    m->atk    = atk;
    m->speed  = d.speed;
    m->radius = d.radius;
    m->reach  = d.reach;
    m->windup = d.windup;
    m->cool   = d.cool / 2;
    m->facing = FACE_DOWN;
    m->state  = MS_IDLE;
    m->hitSwing = -1;
    m->alive  = 1;
}

void World::AddFx(int kind, float x, float y, int value, int crit, unsigned long color)
{
    Fx *f = allocFx();
    if (!f) return;

    memset(f, 0, sizeof(*f));
    f->x     = x;
    f->y     = y;
    f->kind  = kind;
    f->value = value;
    f->crit  = crit;
    f->color = color;
    f->alive = 1;

    switch (kind) {
    case FX_TEXT:
        f->life    = crit ? 60 : 44;
        f->vy      = -1.05f;
        f->vx      = (crit ? 0.0f : ((wf_rand(m_rng) & 1u) ? 0.45f : -0.45f));
        break;
    case FX_SPARK: {
        f->life = 12 + (int)(wf_rand(m_rng) % 8u);
        float a = wf_randf(m_rng) * 6.2831853f;
        float s = 1.2f + wf_randf(m_rng) * 2.6f;
        f->vx   = cosf(a) * s;
        f->vy   = sinf(a) * s;
        break;
    }
    case FX_RING:
        f->life = 20;
        break;
    case FX_SLASH:
        f->life = 12;
        f->facing = m_hero.facing;
        break;
    case FX_WHIRL:
        f->life = 6;
        break;
    default:
        f->life = 16;
        break;
    }
    f->maxlife = f->life;
}

/* --------------------------------------------------------------- waves */

void World::StartWave(int wave)
{
    m_st.wave            = wave;
    m_st.spawnTimer      = 30;
    m_st.banner          = 110;
    m_st.bannerWave      = wave;
    m_st.monstersThisWave = 0;
    m_st.killsThisWave    = 0;

    /* Waves 2+ make the monsters themselves tougher, not just more numerous.
       This is the threat curve the run was missing: without it the hero's
       output outgrew the enemies every wave and the fights kept getting
       lighter.  Wave 1 is left untouched (both factors 1.0). */
    float n = (wave > 1) ? (float)(wave - 1) : 0.0f;
    m_waveHpMul  = 1.0f + WAVE_HP_SLOPE  * n;
    m_waveAtkMul = 1.0f + WAVE_ATK_SLOPE * n;

    int count = 4 + wave * 2;
    if (count > 34) count = 34;
    if ((wave % 5) == 0) ++count;                 /* the boss rides along */
    m_st.toSpawn = count;
}

/* ------------------------------------------------------------ monster AI */

void World::shootArrow(const Monster &m)
{
    Arrow *a = allocArrow();
    if (!a) return;

    Player &p = m_hero;
    float dx = p.x - m.x, dy = p.y - m.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) { dx = 0.0f; dy = 1.0f; d = 1.0f; }

    const float spd = 5.2f;
    a->x    = m.x;
    a->y    = m.y;
    a->vx   = dx / d * spd;
    a->vy   = dy / d * spd;
    a->dmg  = m.atk;
    a->life = 150;
    a->alive = 1;
}

void World::resolveMonsterTiles(Monster &m)
{
    const float r = m.radius;
    if (!IsBlocking(TileAtWorld(m.x, m.y))) {
        if (!IsBlocking(TileAtWorld(m.x - r, m.y)) && !IsBlocking(TileAtWorld(m.x + r, m.y)) &&
            !IsBlocking(TileAtWorld(m.x, m.y - r)) && !IsBlocking(TileAtWorld(m.x, m.y + r)))
            return;
    }

    int tx0 = (int)((m.x - r) / (float)TILE_SIZE), tx1 = (int)((m.x + r) / (float)TILE_SIZE);
    int ty0 = (int)((m.y - r) / (float)TILE_SIZE), ty1 = (int)((m.y + r) / (float)TILE_SIZE);

    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            if (!IsBlocking((int)TileAt(tx, ty))) continue;
            float cx = tx * (float)TILE_SIZE + TILE_SIZE * 0.5f;
            float cy = ty * (float)TILE_SIZE + TILE_SIZE * 0.5f;
            float dx = m.x - cx, dy = m.y - cy;
            float half = TILE_SIZE * 0.5f + r;
            float ox = half - fabsf(dx);
            float oy = half - fabsf(dy);
            if (ox <= 0.0f || oy <= 0.0f) continue;
            if (ox < oy) m.x += (dx > 0.0f ? ox : -ox);
            else         m.y += (dy > 0.0f ? oy : -oy);
        }
}

void World::stepMonsters()
{
    Player &p = m_hero;

    for (int i = 0; i < MAX_MONSTERS; ++i) {
        Monster &m = m_mons[i];
        if (!m.alive) continue;

        ++m.anim;
        if (m.flash > 0) --m.flash;
        if (m.cool  > 0) --m.cool;

        /* knock-back impulse decays every frame */
        m.x += m.knockX;
        m.y += m.knockY;
        m.knockX *= 0.80f;
        m.knockY *= 0.80f;
        if (fabsf(m.knockX) < 0.05f) m.knockX = 0.0f;
        if (fabsf(m.knockY) < 0.05f) m.knockY = 0.0f;

        float dx = p.x - m.x, dy = p.y - m.y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > 0.001f) { dx /= dist; dy /= dist; }
        else               { dx = 0.0f; dy = 1.0f; }

        if (fabsf(dx) > fabsf(dy)) m.facing = (dx > 0.0f) ? FACE_RIGHT : FACE_LEFT;
        else                       m.facing = (dy > 0.0f) ? FACE_DOWN  : FACE_UP;

        switch (m.state) {
        case MS_IDLE:
            if (p.alive && dist < 300.0f) m.state = MS_CHASE;
            break;

        case MS_CHASE: {
            if (!p.alive) { m.state = MS_IDLE; break; }

            /* default goal: straight at the hero */
            float gx = p.x, gy = p.y;

            if (m.kind == MON_ARCHER) {
                const float keep = 180.0f;
                if (dist > keep + 24.0f) {
                    flowGoal(*this, m, gx, gy);          /* approach around terrain */
                } else if (dist < keep - 24.0f) {
                    gx = m.x - dx; gy = m.y - dy;        /* retreat straight back  */
                } /* else hold this range */
                if (m.cool <= 0 && dist < 380.0f) { shootArrow(m); m.cool = MON_DEF[m.kind].cool; }
            } else {
                flowGoal(*this, m, gx, gy);              /* path around terrain    */
                if (dist <= m.reach && m.cool <= 0) {
                    m.state = MS_WINDUP;
                    m.timer = m.windup;
                }
            }

            float gnx = gx - m.x, gny = gy - m.y;
            float gl  = sqrtf(gnx * gnx + gny * gny);
            if (gl > 0.001f) {
                gnx /= gl; gny /= gl;
                m.x += gnx * m.speed;
                m.y += gny * m.speed;
            }
            break;
        }

        case MS_WINDUP:
            if (--m.timer <= 0) {
                m.state = MS_STRIKE;
                m.timer = MON_DEF[m.kind].strike;

                /* the blow lands on the first frame of the strike */
                if (p.alive) {
                    float hx = p.x - m.x, hy = p.y - m.y;
                    float rr = m.reach + 16.0f;
                    if (hx * hx + hy * hy <= rr * rr) DamageHero(m.atk);
                }
                AddFx(FX_RING, m.x + dx * m.reach * 0.7f, m.y + dy * m.reach * 0.7f,
                      0, 0, m.kind == MON_BOSS ? COL_BOSS_A : COL_UI_EDGE);
            }
            break;

        case MS_STRIKE:
            if (--m.timer <= 0) {
                m.state = MS_RECOVER;
                m.timer = MON_DEF[m.kind].recover;
                m.cool  = MON_DEF[m.kind].cool;
            }
            break;

        case MS_RECOVER:
            if (--m.timer <= 0) m.state = MS_CHASE;
            break;

        case MS_STUN:
            if (--m.timer <= 0) m.state = MS_CHASE;
            break;

        default:
            break;
        }

        resolveMonsterTiles(m);
    }

    /* keep them from stacking: a cheap O(n^2) separation pass */
    for (int i = 0; i < MAX_MONSTERS; ++i) {
        Monster &a = m_mons[i];
        if (!a.alive) continue;
        for (int j = i + 1; j < MAX_MONSTERS; ++j) {
            Monster &b = m_mons[j];
            if (!b.alive) continue;
            float dx = b.x - a.x, dy = b.y - a.y;
            float rr = a.radius + b.radius;
            float d2 = dx * dx + dy * dy;
            if (d2 >= rr * rr || d2 < 0.0001f) continue;
            float d = sqrtf(d2);
            float push = (rr - d) * 0.5f;
            float nx = dx / d, ny = dy / d;
            a.x -= nx * push; a.y -= ny * push;
            b.x += nx * push; b.y += ny * push;
        }
    }
}

void World::stepArrows()
{
    Player &p = m_hero;
    const float margin = 40.0f;

    for (int i = 0; i < MAX_ARROWS; ++i) {
        Arrow &a = m_arrows[i];
        if (!a.alive) continue;

        a.x += a.vx;
        a.y += a.vy;
        if (--a.life <= 0) { a.alive = 0; continue; }

        if (a.x < -margin || a.x > (float)WORLD_W + margin ||
            a.y < -margin || a.y > (float)WORLD_H + margin) { a.alive = 0; continue; }

        if (IsBlocking(TileAtWorld(a.x, a.y))) { a.alive = 0; continue; }

        if (!p.alive) continue;
        float dx = a.x - p.x, dy = a.y - p.y;
        if (dx * dx + dy * dy <= 15.0f * 15.0f) {
            a.alive = 0;
            DamageHero(a.dmg);
        }
    }
}

/* ------------------------------------------------------------------ hero */

void World::resolveHeroTiles()
{
    Player &p = m_hero;
    const float r = HERO_RADIUS;

    int tx0 = (int)((p.x - r) / (float)TILE_SIZE), tx1 = (int)((p.x + r) / (float)TILE_SIZE);
    int ty0 = (int)((p.y - r) / (float)TILE_SIZE), ty1 = (int)((p.y + r) / (float)TILE_SIZE);

    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            if (!IsBlocking((int)TileAt(tx, ty))) continue;
            float cx = tx * (float)TILE_SIZE + TILE_SIZE * 0.5f;
            float cy = ty * (float)TILE_SIZE + TILE_SIZE * 0.5f;
            float dx = p.x - cx, dy = p.y - cy;
            float half = TILE_SIZE * 0.5f + r;
            float ox = half - fabsf(dx);
            float oy = half - fabsf(dy);
            if (ox <= 0.0f || oy <= 0.0f) continue;
            if (ox < oy) { p.x += (dx > 0.0f ? ox : -ox); p.vx = 0.0f; }
            else         { p.y += (dy > 0.0f ? oy : -oy); p.vy = 0.0f; }
        }

    if (p.x < r) p.x = r;
    if (p.x > (float)WORLD_W - r) p.x = (float)WORLD_W - r;
    if (p.y < r) p.y = r;
    if (p.y > (float)WORLD_H - r) p.y = (float)WORLD_H - r;
}

void World::stepHero(const Input &in)
{
    Player &p = m_hero;
    if (!p.alive) return;

    ++p.anim;
    if (p.invuln   > 0) --p.invuln;
    if (p.hitFlash > 0) --p.hitFlash;
    if (p.dashCd   > 0) --p.dashCd;
    if (p.spinCd   > 0) --p.spinCd;

    /* ---- attack state machine ------------------------------------------ */
    if (p.attackPhase == 0) {
        if (in.attack) {
            int step = (p.comboWindow > 0 && p.comboStep < 3) ? p.comboStep + 1 : 1;
            p.comboStep   = step;
            p.attackPhase = 1;
            p.attackTimer = atkT(SWING_WINDUP[step], p.atkSpdMul);
            p.comboWindow = 0;
            ++p.swingId;
        }
    } else {
        if (--p.attackTimer <= 0) {
            if (p.attackPhase == 1) {
                p.attackPhase = 2;
                p.attackTimer = atkT(SWING_ACTIVE[p.comboStep], p.atkSpdMul);
                AttackSweep();
                AddFx(FX_SLASH, p.x, p.y, 0, 0, COL_BLADE);
                if (p.lifesteal > 0 && p.hp < p.maxhp) {   /* FANG perk        */
                    p.hp += p.lifesteal;
                    if (p.hp > p.maxhp) p.hp = p.maxhp;
                }
            } else if (p.attackPhase == 2) {
                AttackSweep();
                p.attackPhase = 3;
                p.attackTimer = atkT(SWING_RECOVER[p.comboStep], p.atkSpdMul);
                p.comboWindow = p.attackTimer + 12;
            } else {
                p.attackPhase = 0;
                p.attackTimer = 0;
            }
        } else if (p.attackPhase == 2) {
            AttackSweep();                       /* active frames keep testing */
        }
    }

    if (p.comboWindow > 0) {
        --p.comboWindow;
        if (p.comboWindow == 0) p.comboStep = 0;
    }

    /* ---- dash ----------------------------------------------------------- */
    if (in.dash && p.dashCd <= 0 && p.dashTimer <= 0) {
        p.dashTimer = DASH_LEN;
        p.dashCd    = (int)(DASH_CD * p.dashCdMul);
        p.invuln    = DASH_LEN + 6;
        float dx = in.dx, dy = in.dy;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.1f) {
            const float f[FACE_COUNT][2] = { {0,1}, {0,-1}, {-1,0}, {1,0} };
            dx = f[p.facing][0]; dy = f[p.facing][1];
        } else { dx /= len; dy /= len; }
        p.vx = dx * DASH_SPEED;
        p.vy = dy * DASH_SPEED;
        AddFx(FX_RING, p.x, p.y, 0, 0, COL_HERO_A);
    }

    /* ---- whirlwind ------------------------------------------------------ */
    if (in.spin && p.spinCd <= 0 && p.mp >= SPIN_COST) {
        p.spinTimer = SPIN_LEN;
        p.spinCd    = (int)(SPIN_CD * p.spinCdMul);
        p.mp       -= SPIN_COST;
        p.invuln    = SPIN_LEN;
        shake(10);
        AddFx(FX_WHIRL, p.x, p.y, 0, 0, COL_BLADE);
    }

    /* ---- movement ------------------------------------------------------- */
    float ax = in.dx, ay = in.dy;
    float len = sqrtf(ax * ax + ay * ay);
    if (len > 1.0f) { ax /= len; ay /= len; }

    if (p.dashTimer > 0) {
        --p.dashTimer;
        /* velocity is already set; just decay a little so the dash tapers */
        p.vx *= 0.94f;
        p.vy *= 0.94f;
    } else if (p.attackPhase != 0) {
        p.vx *= 0.86f;                          /* attacks slow you down      */
        p.vy *= 0.86f;
    } else if (len > 0.05f) {
        p.vx += ax * HERO_ACCEL;
        p.vy += ay * HERO_ACCEL;
        if (fabsf(ax) > fabsf(ay)) p.facing = (ax > 0.0f) ? FACE_RIGHT : FACE_LEFT;
        else                       p.facing = (ay > 0.0f) ? FACE_DOWN  : FACE_UP;
    } else {
        p.vx *= HERO_FRICTION;
        p.vy *= HERO_FRICTION;
        if (fabsf(p.vx) < 0.06f) p.vx = 0.0f;
        if (fabsf(p.vy) < 0.06f) p.vy = 0.0f;
    }

    if (p.dashTimer <= 0) {
        float sp = sqrtf(p.vx * p.vx + p.vy * p.vy);
        float maxspd = HERO_MAXSPD * p.spdMul;      /* BOOTS perk raises cap */
        if (sp > maxspd) { p.vx = p.vx / sp * maxspd; p.vy = p.vy / sp * maxspd; }
    }

    p.x += p.vx;
    p.y += p.vy;
    resolveHeroTiles();

    /* ---- whirlwind hit ticks -------------------------------------------- */
    if (p.spinTimer > 0) {
        --p.spinTimer;
        if ((p.spinTimer % SPIN_TICK) == 0) {
            ++p.swingId;                        /* fresh id -> fresh hits     */
            for (int i = 0; i < MAX_MONSTERS; ++i) {
                Monster &m = m_mons[i];
                if (!m.alive) continue;
                float dx = m.x - p.x, dy = m.y - p.y;
                float rr = SPIN_RANGE + p.spinRangeAdd + m.radius;   /* WHIRL perk */
                if (dx * dx + dy * dy <= rr * rr)
                    DamageMonster(i, (int)(p.atk * 1.25f), 0, p.x, p.y);
            }
            AddFx(FX_WHIRL, p.x, p.y, 0, 0, COL_BLADE);
        }
    }
}

void World::AttackSweep()
{
    Player &p = m_hero;
    const int step = (p.comboStep >= 1 && p.comboStep <= 3) ? p.comboStep : 1;

    const float dir[FACE_COUNT][2] = { {0,1}, {0,-1}, {-1,0}, {1,0} };
    float cx = p.x + dir[p.facing][0] * SWING_OFFSET[step];
    float cy = p.y + dir[p.facing][1] * SWING_OFFSET[step];
    float r  = SWING_RANGE[step];

    for (int i = 0; i < MAX_MONSTERS; ++i) {
        Monster &m = m_mons[i];
        if (!m.alive) continue;
        if (m.hitSwing == p.swingId) continue;   /* one hit per swing         */

        float dx = m.x - cx, dy = m.y - cy;
        float rr = r + m.radius;
        if (dx * dx + dy * dy > rr * rr) continue;

        m.hitSwing = p.swingId;

        int dmg  = (int)(p.atk * SWING_MUL[step]);
        float ch = CRIT_CHANCE + p.critAdd;      /* EDGE perk raises crit      */
        if (ch > 0.95f) ch = 0.95f;
        int crit = (wf_randf(m_rng) < ch);
        if (crit) dmg = (int)(dmg * CRIT_MULT);
        dmg += (int)(wf_rand(m_rng) % 3u) - 1;

        DamageMonster(i, dmg, crit, p.x, p.y);
        if (p.mp < p.maxmp) p.mp += 4;
        if (p.mp > p.maxmp) p.mp = p.maxmp;
    }
}

/* ---------------------------------------------------------------- damage */

void World::DamageMonster(int index, int dmg, int crit, float fromX, float fromY)
{
    if (index < 0 || index >= MAX_MONSTERS) return;
    Monster &m = m_mons[index];
    if (!m.alive) return;

    if (dmg < 1) dmg = 1;
    m.hp -= dmg;
    m.flash = 6;

    float dx = m.x - fromX, dy = m.y - fromY;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) { dx = 0.0f; dy = -1.0f; d = 1.0f; }

    float kb = 3.4f;
    if (m.kind == MON_BRUTE) kb = 1.5f;
    if (m.kind == MON_BOSS)  kb = 0.5f;
    if (crit) kb *= 1.5f;
    m.knockX += dx / d * kb;
    m.knockY += dy / d * kb;

    if (m.kind != MON_BOSS) {
        m.state = MS_STUN;
        m.timer = crit ? 18 : 11;
    }

    AddFx(FX_TEXT, m.x, m.y - m.radius - 12.0f, dmg, crit,
          crit ? COL_DMG_CRIT : COL_DMG);
    for (int s = 0; s < (crit ? 7 : 4); ++s)
        AddFx(FX_SPARK, m.x, m.y - 6.0f, 0, 0, crit ? COL_DMG_CRIT : COL_BLADE);

    m_st.hitstop = crit ? 6 : 3;
    shake(crit ? 7 : 3);

    if (m.hp <= 0) killMonster(index);
}

void World::killMonster(int index)
{
    Monster &m = m_mons[index];
    m.alive = 0;

    ++m_st.kills;
    ++m_st.killsThisWave;

    const MonDef &d = MON_DEF[m.kind];
    gainExp(d.exp);

    int gold = d.goldMin + (int)(wf_rand(m_rng) % (unsigned)(d.goldMax - d.goldMin + 1));
    Drop *dr = allocDrop();
    if (dr) {
        memset(dr, 0, sizeof(*dr));
        dr->x     = m.x;
        dr->y     = m.y;
        dr->kind  = DROP_COIN;
        dr->value = gold;
        dr->life  = 900;
        dr->alive = 1;
    }

    /* potions drop more often from the big ones */
    unsigned chance = (m.kind == MON_BOSS) ? 100u : (m.kind == MON_BRUTE ? 26u : 9u);
    if ((wf_rand(m_rng) % 100u) < chance) {
        Drop *p = allocDrop();
        if (p) {
            memset(p, 0, sizeof(*p));
            p->x     = m.x + 10.0f;
            p->y     = m.y;
            p->kind  = DROP_POTION;
            p->value = (m.kind == MON_BOSS) ? 60 : 25;
            p->life  = 900;
            p->alive = 1;
        }
    }

    for (int s = 0; s < 10; ++s)
        AddFx(FX_SPARK, m.x, m.y - 6.0f, 0, 0,
              m.kind == MON_BOSS ? COL_BOSS_A : COL_DMG);
    AddFx(FX_RING, m.x, m.y, 0, 0, COL_DMG_CRIT);
    shake(m.kind == MON_BOSS ? 16 : 4);
}

/* Test seam.  Gold only ever arrives by walking over a drop, so without this
   a test that wants to exercise the forge would have to simulate a whole
   wave of pickups first.  The shell never calls it. */
void World::AddGold(int amount)
{
    if (amount <= 0) return;
    m_hero.gold += amount;
}

void World::DamageHero(int dmg)
{
    Player &p = m_hero;
    if (!p.alive || p.invuln > 0) return;

    if (dmg < 1) dmg = 1;

    /* armour bought at the forge shrinks what actually lands.  Rounded up, so
       stacking plates can never reduce an incoming hit to nothing. */
    if (p.dmgMul < 0.999f) {
        dmg = (int)((float)dmg * p.dmgMul + 0.999f);
        if (dmg < 1) dmg = 1;
    }

    p.hp -= dmg;
    p.hitFlash = 14;
    p.invuln   = 40;
    m_st.hurtFlash = 12;
    shake(9);

    AddFx(FX_TEXT, p.x, p.y - 26.0f, dmg, 0, COL_DMG_HURT);

    /* being hit knocks the swing out of your hands */
    p.attackPhase = 0;
    p.attackTimer = 0;
    p.comboStep   = 0;
    p.comboWindow = 0;

    if (p.hp <= 0) {
        p.hp    = 0;
        p.alive = 0;
        m_st.app      = APP_DEAD;
        m_st.gameover = 1;
        AddFx(FX_RING, p.x, p.y, 0, 0, COL_HP);
        shake(20);
        endRun();
    }
}

void World::gainExp(int n)
{
    Player &p = m_hero;
    p.exp += n;
    int gained = 0;
    while (p.exp >= p.expNext) {
        p.exp -= p.expNext;
        ++p.level;
        p.expNext  = 60 + p.level * 45;
        p.maxhp   += 22;
        p.hp       = p.maxhp;
        p.maxmp   += 6;
        p.mp       = p.maxmp;
        p.atk     += 3;
        ++gained;
        AddFx(FX_RING, p.x, p.y, 0, 0, COL_EXP);
        shake(5);
    }
    m_st.levelFlash = 70;

    /* every level offers a pick-one-of-three perk; the shell pauses while a
       pick is owed (so the sim itself keeps running for headless play). */
    if (gained > 0) {
        if (m_st.pending == 0) rollOffers();
        m_st.pending += gained;
        m_st.app = APP_LEVELUP;
    }
}

/* -------------------------------------------------------- perks (product) */

void World::applyPerk(int perk)
{
    Player &p = m_hero;
    if (perk < 0 || perk >= PERK_COUNT) return;
    ++m_perkCount[perk];

    switch (perk) {
    case PERK_BLADE: p.atk          += 5;                          break;
    case PERK_IRON:  p.maxhp        += 26; p.hp += 26;             break;
    case PERK_BOOTS: p.spdMul       += 0.12f;
                    if (p.spdMul > 1.8f) p.spdMul = 1.8f;          break;
    case PERK_HASTE: p.atkSpdMul    -= 0.10f;
                    if (p.atkSpdMul < 0.5f) p.atkSpdMul = 0.5f;    break;
    case PERK_EDGE:  p.critAdd      += 0.06f;
                    if (p.critAdd > 0.70f) p.critAdd = 0.70f;      break;
    case PERK_FANG:  p.lifesteal    += 2;                          break;
    case PERK_WIND:  p.dashCdMul    -= 0.18f;
                    if (p.dashCdMul < 0.4f) p.dashCdMul = 0.4f;    break;
    case PERK_WHIRL: p.spinCdMul    -= 0.15f;
                    if (p.spinCdMul < 0.4f) p.spinCdMul = 0.4f;
                    p.spinRangeAdd  += 8.0f;                       break;
    default:                                                  break;
    }

    if (p.hp > p.maxhp) p.hp = p.maxhp;
    m_st.levelFlash = 24;
    AddFx(FX_RING, p.x, p.y, 0, 0, COL_EXP);
    shake(3);
}

/* --------------------------------------------------------- cooldown sizes */

int World::DashCdMax() const
{
    int t = (int)((float)DASH_CD * m_hero.dashCdMul + 0.5f);
    return (t < 1) ? 1 : t;
}

int World::SpinCdMax() const
{
    int t = (int)((float)SPIN_CD * m_hero.spinCdMul + 0.5f);
    return (t < 1) ? 1 : t;
}

/* ---------------------------------------------------------- the forge */

/* What the NEXT one of `slot` costs: the base price compounded by the growth
   rate once for every one already bought.  Deterministic, so the tests can
   pin the exact numbers rather than a range. */
int World::ForgePrice(int slot) const
{
    if (slot < 0 || slot >= FORGE_COUNT) return 0;
    float price = (float)FORGE_BASE[slot];
    for (int i = 0; i < m_forgeCount[slot]; ++i) price *= FORGE_GROWTH[slot];
    int p = (int)(price + 0.5f);
    return (p < 1) ? 1 : p;
}

int World::ForgeBought(int slot) const
{
    if (slot < 0 || slot >= FORGE_COUNT) return 0;
    return m_forgeCount[slot];
}

void World::applyForge(int slot)
{
    Player &p = m_hero;
    switch (slot) {
    case FORGE_TEMPER:
        p.atk += FORGE_TEMPER_ATK;
        break;
    case FORGE_PLATE:
        p.dmgMul *= FORGE_PLATE_MUL;
        if (p.dmgMul < FORGE_PLATE_MIN) p.dmgMul = FORGE_PLATE_MIN;
        break;
    case FORGE_ELIXIR:
        p.hp += FORGE_ELIXIR_HP;
        if (p.hp > p.maxhp) p.hp = p.maxhp;
        break;
    default:
        break;
    }
}

/* Buy one.  Only allowed while the level-up overlay is up: that is the only
   pause the player ever gets, so the shop never interrupts a fight.  Returns
   false -- touching nothing at all -- when the gold is short, which is what
   lets the shell show the "cannot afford" state without a second query. */
bool World::BuyForge(int slot)
{
    if (m_st.app != APP_LEVELUP) return false;
    if (slot < 0 || slot >= FORGE_COUNT) return false;

    int price = ForgePrice(slot);
    if (m_hero.gold < price) return false;

    m_hero.gold -= price;
    ++m_forgeCount[slot];
    applyForge(slot);
    return true;
}

/* What a perk would do to the one number the player actually reads.  Fills
   before/after with that stat and unit with 0 (a plain count) or 1 (a
   percentage), so the card can print "34 -> 39" rather than only "attack
   +5" -- without it the offer is a guess, because +5 attack means something
   very different at level 2 and at level 9.  Returns false for a bad perk. */
bool World::PerkPreview(int perk, int &before, int &after, int &unit) const
{
    const Player &p = m_hero;
    before = 0;
    after  = 0;
    unit   = 0;

    switch (perk) {
    case PERK_BLADE:
        before = p.atk;   after = p.atk + 5;                          break;
    case PERK_IRON:
        before = p.maxhp; after = p.maxhp + 26;                       break;

    case PERK_BOOTS: {
        float n = p.spdMul + 0.12f;
        if (n > 1.8f) n = 1.8f;
        before = (int)(p.spdMul * 100.0f + 0.5f);
        after  = (int)(n        * 100.0f + 0.5f);
        unit   = 1;
        break;
    }
    case PERK_HASTE: {
        float n = p.atkSpdMul - 0.10f;
        if (n < 0.5f) n = 0.5f;
        /* atkSpdMul scales the attack TIMER, so the speed the player feels is
           its reciprocal -- print that, or "-10%" reads as a downgrade */
        before = (int)(100.0f / p.atkSpdMul + 0.5f);
        after  = (int)(100.0f / n           + 0.5f);
        unit   = 1;
        break;
    }
    case PERK_EDGE: {
        float n = p.critAdd + 0.06f;
        if (n > 0.70f) n = 0.70f;
        before = (int)((CRIT_CHANCE + p.critAdd) * 100.0f + 0.5f);
        after  = (int)((CRIT_CHANCE + n        ) * 100.0f + 0.5f);
        unit   = 1;
        break;
    }
    case PERK_FANG:
        before = p.lifesteal; after = p.lifesteal + 2;                 break;

    case PERK_WIND: {
        float n = p.dashCdMul - 0.18f;
        if (n < 0.4f) n = 0.4f;
        before = (int)(p.dashCdMul * 100.0f + 0.5f);
        after  = (int)(n           * 100.0f + 0.5f);
        unit   = 1;
        break;
    }
    case PERK_WHIRL: {
        float n = p.spinCdMul - 0.15f;
        if (n < 0.4f) n = 0.4f;
        before = (int)(p.spinCdMul * 100.0f + 0.5f);
        after  = (int)(n           * 100.0f + 0.5f);
        unit   = 1;
        break;
    }
    default:
        return false;
    }
    return true;
}

/* Pick three distinct perks, weighting the less-taken ones so the offers feel
   varied across a run.  Deterministic: driven by the world's xorshift rng. */
void World::rollOffers()
{
    bool used[PERK_COUNT];
    for (int k = 0; k < PERK_COUNT; ++k) used[k] = false;

    for (int s = 0; s < 3; ++s) {
        int w[PERK_COUNT], total = 0;
        for (int k = 0; k < PERK_COUNT; ++k) {
            if (used[k]) { w[k] = 0; continue; }
            w[k] = 4 - 2 * m_perkCount[k];
            if (w[k] < 1) w[k] = 1;
            total += w[k];
        }
        int r = (int)(wf_rand(m_rng) % (unsigned)total);
        int pick = -1;
        for (int k = 0; k < PERK_COUNT; ++k) {
            if (used[k]) continue;
            r -= w[k];
            if (r < 0) { pick = k; break; }
        }
        if (pick < 0)
            for (int k = 0; k < PERK_COUNT; ++k) if (!used[k]) { pick = k; break; }
        used[pick] = true;
        m_offer[s] = pick;
    }
}

void World::ChoosePerk(int slot)
{
    if (m_st.app != APP_LEVELUP || m_st.pending <= 0) return;
    if (slot < 0 || slot > 2) slot = 0;
    int perk = m_offer[slot];
    applyPerk(perk);
    --m_st.pending;
    if (m_st.pending > 0) {
        rollOffers();
    } else {
        m_st.app = APP_PLAY;
        m_offer[0] = m_offer[1] = m_offer[2] = -1;
    }
}

/* ---------------------------------------------------------------- save */

void World::LoadSave()
{
    FILE *f = fopen("warfire.sav", "r");
    if (!f) return;
    int wv = 0, kl = 0;
    if (fscanf(f, "%d %d", &wv, &kl) == 2) {
        if (wv >= 0 && wv < 1000000) m_bestWave  = wv;
        if (kl >= 0 && kl < 1000000) m_bestKills = kl;
    }
    fclose(f);
}

void World::SaveSave()
{
    FILE *f = fopen("warfire.sav", "w");
    if (!f) return;
    fprintf(f, "%d %d\n", m_bestWave, m_bestKills);
    fclose(f);
}

/* Update the high score and persist it if this run beat it.  Only touches the
   file when EnableSave(1) was set -- the headless tests never write one. */
void World::endRun()
{
    if (m_st.wave > m_bestWave ||
        (m_st.wave == m_bestWave && m_st.kills > m_bestKills)) {
        m_bestWave  = m_st.wave;
        m_bestKills = m_st.kills;
        if (m_save) SaveSave();
    }
}



/* ---------------------------------------------------------------- effects */

void World::stepFx()
{
    for (int i = 0; i < MAX_FX; ++i) {
        Fx &f = m_fx[i];
        if (!f.alive) continue;

        f.x += f.vx;
        f.y += f.vy;

        if (f.kind == FX_TEXT) {
            f.vy *= 0.94f;
            f.vy -= 0.012f;
        } else if (f.kind == FX_SPARK) {
            f.vx *= 0.90f;
            f.vy *= 0.90f;
        }

        if (--f.life <= 0) f.alive = 0;
    }
}

void World::stepDrops()
{
    Player &p = m_hero;

    for (int i = 0; i < MAX_DROPS; ++i) {
        Drop &d = m_drops[i];
        if (!d.alive) continue;

        if (--d.life <= 0) { d.alive = 0; continue; }

        if (!p.alive) continue;
        float dx = p.x - d.x, dy = p.y - d.y;
        float d2 = dx * dx + dy * dy;

        if (d2 < 90.0f * 90.0f) {
            /* drift towards the hero once close enough */
            float dist = sqrtf(d2);
            if (dist > 1.0f) {
                float pull = (d.kind == DROP_COIN) ? 3.6f : 2.2f;
                d.x += dx / dist * pull;
                d.y += dy / dist * pull;
            }
        }
        if (d2 < 18.0f * 18.0f) {
            d.alive = 0;
            if (d.kind == DROP_COIN) {
                p.gold += d.value;
                AddFx(FX_TEXT, p.x, p.y - 30.0f, d.value, 0, COL_DMG_CRIT);
            } else {
                p.hp += d.value;
                if (p.hp > p.maxhp) p.hp = p.maxhp;
                AddFx(FX_TEXT, p.x, p.y - 30.0f, d.value, 0, COL_HP);
            }
        }
    }
}

/* ------------------------------------------------------------------ waves */

void World::stepWave()
{
    Player &p = m_hero;

    if (m_st.toSpawn > 0) {
        if (--m_st.spawnTimer <= 0) {
            m_st.spawnTimer = 26;

            /* spawn on a ring around the hero, far enough to be fair, and
               always on open ground that the hero can actually path to -- a
               monster born inside a rock or a walled-off pocket used to be
               uncatchable, so the wave could never end. */
            float a  = wf_randf(m_rng) * 6.2831853f;
            float rr = 340.0f + wf_randf(m_rng) * 160.0f;
            float x  = p.x + cosf(a) * rr;
            float y  = p.y + sinf(a) * rr;

            float fx, fy;
            if      (SpawnPos(p.x, p.y, 340.0f, 500.0f, fx, fy)) { x = fx; y = fy; }
            else if (FreeSpot(p.x, p.y, a, rr, fx, fy))          { x = fx; y = fy; }
            if (x < 40.0f) x = 40.0f;
            if (x > (float)WORLD_W - 40.0f) x = (float)WORLD_W - 40.0f;
            if (y < 40.0f) y = 40.0f;
            if (y > (float)WORLD_H - 40.0f) y = (float)WORLD_H - 40.0f;

            int kind = MON_GRUNT;
            int wave = m_st.wave;

            if ((wave % 5) == 0 && m_st.toSpawn == 1) {
                kind = MON_BOSS;
            } else {
                unsigned r = wf_rand(m_rng) % 100u;
                if (wave >= 3 && r < 22u)      kind = MON_BRUTE;
                else if (wave >= 2 && r < 55u) kind = MON_ARCHER;
            }

            SpawnMonster(kind, x, y);
            ++m_st.monstersThisWave;
            --m_st.toSpawn;
        }
        return;
    }

    if (LiveMonsters() == 0) {
        if (m_st.wave >= WIN_WAVES) {
            m_st.app = APP_WIN;
            m_st.gameover = 1;
            m_st.banner = 0;
            endRun();
        } else {
            StartWave(m_st.wave + 1);
        }
    }
}

/* ------------------------------------------------------------------- step */

void World::Step(const Input &in)
{
    if (m_st.gameover) return;    /* DEAD / WIN are frozen; LEVELUP is driven
                                     by the shell, which simply stops calling
                                     Step while a perk pick is owed          */

    ++m_st.frame;

    if (m_st.hitstop > 0) {
        --m_st.hitstop;                 /* everything freezes: that is the point */
        return;
    }

    stepHero(in);
    updateFlow();                      /* recompute the flood from the hero  */
    stepMonsters();
    stepArrows();
    stepDrops();
    stepFx();
    stepWave();

    if (m_st.shake     > 0) --m_st.shake;
    if (m_st.levelFlash > 0) --m_st.levelFlash;
    if (m_st.hurtFlash > 0) --m_st.hurtFlash;
    if (m_st.banner    > 0) --m_st.banner;
}

} /* namespace wf */
