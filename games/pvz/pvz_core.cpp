/* ===========================================================================
 *  pvz_core.cpp -- Lawn Defense : the simulation
 * ---------------------------------------------------------------------------
 *  Pure arithmetic; no window, no renderer, no asserts.  One Step() is one
 *  60 Hz frame.  The step order:
 *
 *      sun & sunflowers            the economy
 *      peashooters                 fire a pea when a zombie shares their row
 *      peas                        move right, damage the first zombie hit
 *      zombies                     walk left, or chew the plant at their feet
 *      splats                      the little green burst where a pea landed
 *      spawns / win                the scripted level
 *
 *  The win check runs AFTER the zombies so it sees this frame's deaths.
 * ===========================================================================
 */
#include "pvz_core.h"

namespace pvz {

/* --------------------------------------------------------------- spawn rng
   xorshift32 -- tiny, fast and, more to the point, reproducible: a test that
   does Reset(7) knows the exact zombie-row sequence forever. */

/* --------------------------------------------------------------- cost/hp */
int Lawn::PlantCost(int kind)
{
    switch (kind) {
    case PL_PEASHOOTER: return COST_PEASHOOTER;
    case PL_SUNFLOWER:  return COST_SUNFLOWER;
    case PL_WALLNUT:    return COST_WALLNUT;
    default:            return 0;
    }
}

static int plantHP(int kind)
{
    switch (kind) {
    case PL_PEASHOOTER: return PLANT_HP_PEASHOOTER;
    case PL_SUNFLOWER:  return PLANT_HP_SUNFLOWER;
    case PL_WALLNUT:    return PLANT_HP_WALLNUT;
    default:            return 0;
    }
}

int Lawn::PlantW(int kind)
{
    switch (kind) {
    case PL_PEASHOOTER: return 64;
    case PL_SUNFLOWER:  return 60;
    case PL_WALLNUT:    return 58;
    default:            return 0;
    }
}

int Lawn::PlantH(int kind)
{
    switch (kind) {
    case PL_PEASHOOTER: return 80;
    case PL_SUNFLOWER:  return 76;
    case PL_WALLNUT:    return 66;
    default:            return 0;
    }
}

/* --------------------------------------------------------------- lifecycle */

Lawn::Lawn()
    : m_zc(0), m_pc(0), m_sc(0),
      m_sun(0), m_sel(PL_PEASHOOTER), m_auto(1), m_phase(PHASE_PLAY),
      m_tick(0), m_spawned(0), m_sunAcc(0), m_killed(0), m_rng(1)
{
    for (int i = 0; i < ROWS * COLS; ++i) {
        m_plant[i] = PL_NONE;
        m_hp[i] = 0;
        m_shoot[i] = 0;
        m_stimer[i] = 0;
    }
    for (int i = 0; i < MAX_ZOMBIE; ++i) m_z[i].alive = 0;
    for (int i = 0; i < MAX_PEA; ++i)    m_p[i].alive = 0;
    for (int i = 0; i < MAX_SPLAT; ++i)  m_s[i].alive = 0;
}

void Lawn::Reset(unsigned seed)
{
    for (int i = 0; i < ROWS * COLS; ++i) {
        m_plant[i] = PL_NONE;
        m_hp[i] = 0;
        m_shoot[i] = 0;
        m_stimer[i] = 0;
    }
    m_zc = m_pc = m_sc = 0;
    m_sun = SUN_START;
    m_sel = PL_PEASHOOTER;
    m_auto = 1;
    m_phase = PHASE_PLAY;
    m_tick = 0;
    m_spawned = 0;
    m_sunAcc = 0;
    m_killed = 0;
    m_rng = seed ? seed : 1u;
}

/* ----------------------------------------------------------------- picking */

int Lawn::CellAt(int px, int py, int &c, int &r) const
{
    if (px < GRID_X0 || px >= GRID_X0 + COLS * COL_W) return 0;
    if (py < LAWN_Y0 || py >= LAWN_Y0 + ROWS * ROW_H) return 0;
    c = (px - GRID_X0) / COL_W;
    r = (py - LAWN_Y0) / ROW_H;
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return 0;
    return 1;
}

int Lawn::SeedIdx(int px, int py) const
{
    if (py < SEED_Y || py >= SEED_Y + SEED_H) return -1;
    const int n = PL_COUNT - 1;             /* three seeds */
    for (int i = 0; i < n; ++i) {
        const int sx = SEED_X0 + i * (SEED_W + SEED_GAP);
        if (px >= sx && px < sx + SEED_W) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ input */

void Lawn::SetSel(int kind)
{
    if (kind == PL_NONE || (kind >= PL_PEASHOOTER && kind < PL_COUNT))
        m_sel = kind;
}

bool Lawn::TryPlant(int c, int r)
{
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return false;
    if (m_sel == PL_NONE) return false;
    if (m_plant[r * COLS + c] != PL_NONE) return false;
    const int cost = PlantCost(m_sel);
    if (m_sun < cost) return false;

    m_plant[r * COLS + c] = (unsigned char)m_sel;
    m_hp[r * COLS + c]    = plantHP(m_sel);
    m_shoot[r * COLS + c] = SHOOT_INTERVAL;  /* ready the instant a zombie
                                                shows up in the row */
    m_stimer[r * COLS + c] = 0;
    m_sun -= cost;
    return true;
}

/* ------------------------------------------------------------ test seams */

bool Lawn::TestPlant(int kind, int c, int r)
{
    if (kind < PL_PEASHOOTER || kind >= PL_COUNT) return false;
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return false;
    m_plant[r * COLS + c] = (unsigned char)kind;
    m_hp[r * COLS + c]    = plantHP(kind);
    m_shoot[r * COLS + c] = SHOOT_INTERVAL;
    m_stimer[r * COLS + c] = 0;
    return true;
}

int Lawn::SpawnOne(int row)
{
    if (row < 0 || row >= ROWS) return -1;
    int idx = -1;
    for (int i = 0; i < m_zc; ++i) if (!m_z[i].alive) { idx = i; break; }
    Zombie &z = m_z[(idx >= 0) ? idx : m_zc];
    z.row = row;
    z.x = (float)(FIELD_W - 40);       /* just inside the right edge, so the
                                          tests control how far it walks */
    z.hp = ZOMBIE_HP;
    z.anim = 0;
    z.eating = 0;
    z.flash = 0;
    z.alive = 1;
    if (idx < 0) ++m_zc;
    return (idx >= 0) ? idx : m_zc - 1;
}

void Lawn::ClearPeas()
{
    for (int i = 0; i < m_pc; ++i) m_p[i].alive = 0;
}

/* --------------------------------------------------------------- the step */

int Lawn::enemyInRow(int row, int fromX) const
{
    for (int i = 0; i < m_zc; ++i)
        if (m_z[i].alive && m_z[i].row == row && (int)m_z[i].x >= fromX)
            return 1;
    return 0;
}

/*  The cell the zombie at x is chewing, or -1 if it is in the open.  A zombie
 *  walks LEFT, so the plant it is about to bite is the one just ahead and to
 *  the left -- the smallest positive d first.  A plant just *behind* it
 *  (d < -20) was already swallowed and does not count. */
int Lawn::findBiteCell(int row, int x) const
{
    int best = -1, bestD = 1 << 20;
    for (int c = 0; c < COLS; ++c) {
        if (m_plant[row * COLS + c] == PL_NONE) continue;
        const int d = x - CellCX(c);
        if (d >= -20 && d <= 48 && d < bestD) { bestD = d; best = c; }
    }
    return best;
}

void Lawn::spawnZombie(int row)
{
    if (m_zc >= MAX_ZOMBIE) return;
    Zombie &z = m_z[m_zc];
    z.row = row;
    z.x = (float)(FIELD_W + 24);
    z.hp = ZOMBIE_HP;
    z.anim = 0;
    z.eating = 0;
    z.flash = 0;
    z.alive = 1;
    ++m_zc;
    ++m_spawned;
}

void Lawn::Step()
{
    if (m_phase != PHASE_PLAY) return;
    ++m_tick;

    /* ---- the economy -------------------------------------------------- */
    if (++m_sunAcc >= SUN_PERIOD) {
        m_sunAcc = 0;
        if (m_sun < SUN_CAP) m_sun += SUN_ADD;
    }
    for (int i = 0; i < ROWS * COLS; ++i) {
        if (m_plant[i] != PL_SUNFLOWER) continue;
        if (++m_stimer[i] >= SUNFLOWER_PERIOD) {
            m_stimer[i] = 0;
            if (m_sun < SUN_CAP) m_sun += SUN_ADD;
        }
    }

    /* ---- peashooters -------------------------------------------------- */
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c) {
            if (m_plant[r * COLS + c] != PL_PEASHOOTER) continue;
            if (++m_shoot[r * COLS + c] > SHOOT_INTERVAL)
                m_shoot[r * COLS + c] = SHOOT_INTERVAL;
            if (m_shoot[r * COLS + c] >= SHOOT_INTERVAL &&
                enemyInRow(r, CellCX(c) - 8) && m_pc < MAX_PEA) {
                Pea &p = m_p[m_pc++];
                p.row = r;
                p.x = (float)(CellCX(c) + 16);
                p.alive = 1;
                m_shoot[r * COLS + c] = 0;
            }
        }

    /* ---- peas --------------------------------------------------------- */
    for (int i = 0; i < m_pc; ++i) {
        if (!m_p[i].alive) continue;
        m_p[i].x += (float)PEA_SPEED;
        if (m_p[i].x > (float)(FIELD_W + 12)) { m_p[i].alive = 0; continue; }
        for (int j = 0; j < m_zc; ++j) {
            if (!m_z[j].alive || m_z[j].row != m_p[i].row) continue;
            const int zx = (int)m_z[j].x;
            if (m_p[i].x >= zx - 18 && m_p[i].x <= zx + 16) {
                m_z[j].hp -= PEA_DMG;
                m_z[j].flash = 6;
                m_p[i].alive = 0;
                if (m_sc < MAX_SPLAT) {
                    Splat &s = m_s[m_sc++];
                    s.cx = (int)m_p[i].x;
                    s.cy = FeetY(m_p[i].row) - 24;
                    s.life = 8;
                    s.alive = 1;
                }
                if (m_z[j].hp <= 0) { m_z[j].alive = 0; ++m_killed; }
                break;
            }
        }
    }

    /* ---- zombies ------------------------------------------------------ */
    for (int i = 0; i < m_zc; ++i) {
        if (!m_z[i].alive) continue;
        const int bc = findBiteCell(m_z[i].row, (int)m_z[i].x);
        if (bc >= 0) {
            m_z[i].eating = 1;
            const int idx = m_z[i].row * COLS + bc;
            m_hp[idx] -= EAT;
            if (m_hp[idx] <= 0) {
                m_plant[idx] = PL_NONE;
                m_hp[idx] = 0;
            }
        } else {
            m_z[i].eating = 0;
            m_z[i].x -= 0.30f;                 /* walks left, steadily */
            if (m_z[i].x <= (float)(HOUSE_W + 8)) {
                m_z[i].alive = 0;
                m_phase = PHASE_LOSE;          /* a zombie broke through */
            }
        }
        if (m_z[i].anim < 0x7fff) ++m_z[i].anim;
    }
    for (int i = 0; i < m_zc; ++i)
        if (m_z[i].alive && m_z[i].flash > 0) --m_z[i].flash;

    /* ---- splats ------------------------------------------------------- */
    for (int i = 0; i < m_sc; ++i) {
        if (!m_s[i].alive) continue;
        if (--m_s[i].life <= 0) m_s[i].alive = 0;
    }

    /* ---- the scripted level ------------------------------------------- */
    if (m_auto && m_spawned < ZOMBIE_TOTAL && m_tick >= SPAWN_DELAY &&
        ((m_tick - SPAWN_DELAY) % SPAWN_INTERVAL) == 0) {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        spawnZombie((int)(m_rng % (unsigned)ROWS));
    }

    /* ---- win ---------------------------------------------------------- */
    if (m_phase == PHASE_PLAY && m_spawned >= ZOMBIE_TOTAL) {
        int alive = 0;
        for (int i = 0; i < m_zc; ++i) if (m_z[i].alive) ++alive;
        if (alive == 0) m_phase = PHASE_WIN;
    }

    /* ---- compact the dead out of the arrays --------------------------- */
    int w = 0;
    for (int k = 0; k < m_zc; ++k)
        if (m_z[k].alive) { if (w != k) m_z[w] = m_z[k]; ++w; }
    m_zc = w;
    w = 0;
    for (int k = 0; k < m_pc; ++k)
        if (m_p[k].alive) { if (w != k) m_p[w] = m_p[k]; ++w; }
    m_pc = w;
    w = 0;
    for (int k = 0; k < m_sc; ++k)
        if (m_s[k].alive) { if (w != k) m_s[w] = m_s[k]; ++w; }
    m_sc = w;
}

int Lawn::ZombiesLeft() const { return ZOMBIE_TOTAL - m_killed; }

int Lawn::Alive() const
{
    int n = 0;
    for (int i = 0; i < m_zc; ++i) if (m_z[i].alive) ++n;
    return n;
}

} /* namespace pvz */
