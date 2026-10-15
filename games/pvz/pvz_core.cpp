/* ===========================================================================
 *  pvz_core.cpp -- Lawn Defense : the simulation
 * ---------------------------------------------------------------------------
 *  Pure arithmetic; no window, renderer or assertions. One Step() is one
 *  60 Hz frame. All random choices use a local xorshift state for deterministic
 *  replay and headless testing.
 * ===========================================================================
 */
#include "pvz_core.h"

namespace pvz {

static int absInt(int v) { return v < 0 ? -v : v; }

int Lawn::PlantCost(int kind)
{
    switch (kind) {
    case PL_PEASHOOTER: return COST_PEASHOOTER;
    case PL_SUNFLOWER:  return COST_SUNFLOWER;
    case PL_WALLNUT:    return COST_WALLNUT;
    case PL_SNOWPEA:    return COST_SNOWPEA;
    case PL_CHERRY:     return COST_CHERRY;
    default:            return 0;
    }
}

static int plantHP(int kind)
{
    switch (kind) {
    case PL_PEASHOOTER: return PLANT_HP_PEASHOOTER;
    case PL_SUNFLOWER:  return PLANT_HP_SUNFLOWER;
    case PL_WALLNUT:    return PLANT_HP_WALLNUT;
    case PL_SNOWPEA:    return PLANT_HP_SNOWPEA;
    case PL_CHERRY:     return PLANT_HP_CHERRY;
    default:            return 0;
    }
}

int Lawn::PlantW(int kind)
{
    switch (kind) {
    case PL_PEASHOOTER: return 64;
    case PL_SUNFLOWER:  return 60;
    case PL_WALLNUT:    return 58;
    case PL_SNOWPEA:    return 64;
    case PL_CHERRY:     return 58;
    default:            return 0;
    }
}

int Lawn::PlantH(int kind)
{
    switch (kind) {
    case PL_PEASHOOTER: return 80;
    case PL_SUNFLOWER:  return 76;
    case PL_WALLNUT:    return 66;
    case PL_SNOWPEA:    return 80;
    case PL_CHERRY:     return 66;
    default:            return 0;
    }
}

int Lawn::WaveSize(int wave)
{
    if (wave < 1 || wave > LEVEL_WAVES) return 0;
    int n = 2 + wave;                   /* 3..12 enemies across ten waves */
    if (wave == 5 || wave == 10) n += 2; /* set-piece large waves */
    return n;
}

int Lawn::ZombieHp(int kind)
{
    switch (kind) {
    case ZK_CONE:   return CONE_HP;
    case ZK_BUCKET: return BUCKET_HP;
    default:        return ZOMBIE_HP;
    }
}

int Lawn::WaveSize() const { return WaveSize(m_wave); }

Lawn::Lawn()
    : m_zc(0), m_pc(0), m_sc(0), m_sdc(0), m_bc(0),
      m_sun(0), m_sel(PL_PEASHOOTER), m_auto(1), m_phase(PHASE_PLAY),
      m_tick(0), m_spawned(0), m_sunAcc(0), m_killed(0), m_collected(0),
      m_wave(1), m_waveSpawned(0), m_waveTimer(SPAWN_DELAY), m_rng(1)
{
    for (int i = 0; i < ROWS * COLS; ++i) {
        m_plant[i] = PL_NONE;
        m_hp[i] = 0;
        m_shoot[i] = 0;
        m_stimer[i] = 0;
        m_btimer[i] = 0;
    }
    for (int i = 0; i < MAX_ZOMBIE; ++i) m_z[i].alive = 0;
    for (int i = 0; i < MAX_PEA; ++i) m_p[i].alive = 0;
    for (int i = 0; i < MAX_SPLAT; ++i) m_s[i].alive = 0;
    for (int i = 0; i < MAX_SUN_DROP; ++i) m_sd[i].alive = 0;
    for (int i = 0; i < MAX_BLAST; ++i) m_b[i].alive = 0;
}

void Lawn::Reset(unsigned seed)
{
    for (int i = 0; i < ROWS * COLS; ++i) {
        m_plant[i] = PL_NONE;
        m_hp[i] = 0;
        m_shoot[i] = 0;
        m_stimer[i] = 0;
        m_btimer[i] = 0;
    }
    m_zc = m_pc = m_sc = m_sdc = m_bc = 0;
    m_sun = SUN_START;
    m_sel = PL_PEASHOOTER;
    m_auto = 1;
    m_phase = PHASE_PLAY;
    m_tick = 0;
    m_spawned = 0;
    m_sunAcc = 0;
    m_killed = 0;
    m_collected = 0;
    m_wave = 1;
    m_waveSpawned = 0;
    m_waveTimer = SPAWN_DELAY;
    m_rng = seed ? seed : 1u;
}

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
    const int n = PL_COUNT - 1;
    for (int i = 0; i < n; ++i) {
        const int sx = SEED_X0 + i * (SEED_W + SEED_GAP);
        if (px >= sx && px < sx + SEED_W) return i;
    }
    return -1;
}

void Lawn::SetSel(int kind)
{
    if (kind == PL_NONE || (kind >= PL_PEASHOOTER && kind < PL_COUNT))
        m_sel = kind;
}

bool Lawn::TryPlant(int c, int r)
{
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return false;
    if (m_sel == PL_NONE || m_plant[r * COLS + c] != PL_NONE) return false;
    const int cost = PlantCost(m_sel);
    if (m_sun < cost) return false;

    const int idx = r * COLS + c;
    m_plant[idx] = (unsigned char)m_sel;
    m_hp[idx] = plantHP(m_sel);
    m_shoot[idx] = SHOOT_INTERVAL;
    m_stimer[idx] = 0;
    m_btimer[idx] = (m_sel == PL_CHERRY) ? CHERRY_FUSE : 0;
    m_sun -= cost;
    return true;
}

bool Lawn::TestPlant(int kind, int c, int r)
{
    if (kind < PL_PEASHOOTER || kind >= PL_COUNT) return false;
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return false;
    const int idx = r * COLS + c;
    m_plant[idx] = (unsigned char)kind;
    m_hp[idx] = plantHP(kind);
    m_shoot[idx] = SHOOT_INTERVAL;
    m_stimer[idx] = 0;
    m_btimer[idx] = (kind == PL_CHERRY) ? CHERRY_FUSE : 0;
    return true;
}

void Lawn::AddSun(int v)
{
    m_sun += v;
    if (m_sun < 0) m_sun = 0;
    if (m_sun > SUN_CAP) m_sun = SUN_CAP;
}

int Lawn::SpawnOne(int row, int kind)
{
    if (row < 0 || row >= ROWS || m_zc >= MAX_ZOMBIE) return -1;
    if (kind < ZK_NORMAL || kind > ZK_BUCKET) kind = ZK_NORMAL;
    Zombie &z = m_z[m_zc];
    z.row = row;
    z.x = (float)(FIELD_W - 40);
    z.kind = kind;
    z.hp = ZombieHp(kind);
    z.chill = 0;
    z.anim = 0;
    z.eating = 0;
    z.flash = 0;
    z.alive = 1;
    ++m_zc;
    return m_zc - 1;
}

bool Lawn::TestSunDrop(int x, int y, int amount)
{
    if (amount <= 0) return false;
    dropSun(x, y, amount);
    return m_sdc > 0;
}

void Lawn::ClearPeas()
{
    for (int i = 0; i < m_pc; ++i) m_p[i].alive = 0;
}

int Lawn::enemyInRow(int row, int fromX) const
{
    for (int i = 0; i < m_zc; ++i)
        if (m_z[i].alive && m_z[i].row == row && (int)m_z[i].x >= fromX)
            return 1;
    return 0;
}

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

void Lawn::spawnZombie(int row, int kind)
{
    if (m_zc >= MAX_ZOMBIE) return;
    Zombie &z = m_z[m_zc];
    z.row = row;
    z.x = (float)(FIELD_W + 24);
    z.kind = kind;
    z.hp = ZombieHp(kind);
    z.chill = 0;
    z.anim = 0;
    z.eating = 0;
    z.flash = 0;
    z.alive = 1;
    ++m_zc;
}

int Lawn::chooseZombieKind(int wave, int order) const
{
    if (wave == 5 || wave == 10) {
        if (order % 3 == 2) return ZK_BUCKET;
        if (order % 2 == 1) return ZK_CONE;
        return ZK_NORMAL;
    }
    if (wave >= 7 && (order + wave) % 7 == 0) return ZK_BUCKET;
    if (wave >= 3 && (order + wave) % 4 == 0) return ZK_CONE;
    return ZK_NORMAL;
}

void Lawn::dropSun(int x, int y, int amount)
{
    int slot = -1;
    for (int i = 0; i < m_sdc; ++i)
        if (!m_sd[i].alive) { slot = i; break; }
    if (slot < 0) {
        if (m_sdc >= MAX_SUN_DROP) return;
        slot = m_sdc++;
    }
    m_sd[slot].x = x;
    m_sd[slot].y = y;
    m_sd[slot].life = SUN_DROP_LIFE;
    m_sd[slot].amount = amount;
    m_sd[slot].alive = 1;
}

void Lawn::makeBlast(int cx, int cy)
{
    int slot = -1;
    for (int i = 0; i < m_bc; ++i)
        if (!m_b[i].alive) { slot = i; break; }
    if (slot < 0) {
        if (m_bc >= MAX_BLAST) return;
        slot = m_bc++;
    }
    m_b[slot].cx = cx;
    m_b[slot].cy = cy;
    m_b[slot].life = BLAST_TICKS;
    m_b[slot].alive = 1;
}

void Lawn::damageZombie(int index, int amount, int chill)
{
    if (index < 0 || index >= m_zc || !m_z[index].alive) return;
    Zombie &z = m_z[index];
    z.hp -= amount;
    z.flash = 6;
    if (chill > z.chill) z.chill = chill;
    if (z.hp <= 0) {
        z.alive = 0;
        ++m_killed;
        dropSun((int)z.x, FeetY(z.row) - 48, SUN_ADD);
    }
}

bool Lawn::TryCollectSun(int px, int py)
{
    int best = -1;
    int bestD = SUN_PICK_RADIUS * SUN_PICK_RADIUS + 1;
    for (int i = 0; i < m_sdc; ++i) {
        if (!m_sd[i].alive) continue;
        const int dx = px - m_sd[i].x;
        const int dy = py - m_sd[i].y;
        const int d2 = dx * dx + dy * dy;
        if (d2 <= SUN_PICK_RADIUS * SUN_PICK_RADIUS && d2 < bestD) {
            best = i;
            bestD = d2;
        }
    }
    if (best < 0) return false;
    AddSun(m_sd[best].amount);
    m_collected += m_sd[best].amount;
    m_sd[best].alive = 0;
    return true;
}

void Lawn::Step()
{
    if (m_phase != PHASE_PLAY) return;
    ++m_tick;

    if (++m_sunAcc >= SUN_PERIOD) {
        m_sunAcc = 0;
        AddSun(SUN_ADD);
    }
    for (int i = 0; i < ROWS * COLS; ++i) {
        if (m_plant[i] == PL_SUNFLOWER && ++m_stimer[i] >= SUNFLOWER_PERIOD) {
            m_stimer[i] = 0;
            AddSun(SUN_ADD);
        }
    }

    /* Cherry bombs arm on planting, then clear a 3x3 tile neighborhood. */
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c) {
            const int idx = r * COLS + c;
            if (m_plant[idx] != PL_CHERRY || m_btimer[idx] <= 0) continue;
            if (--m_btimer[idx] != 0) continue;
            const int cx = CellCX(c), cy = FeetY(r) - 36;
            m_plant[idx] = PL_NONE;
            m_hp[idx] = 0;
            makeBlast(cx, cy);
            for (int z = 0; z < m_zc; ++z) {
                if (!m_z[z].alive || absInt(m_z[z].row - r) > 1) continue;
                if (absInt((int)m_z[z].x - cx) <= COL_W + COL_W / 2)
                    damageZombie(z, CHERRY_DMG, 0);
            }
        }

    /* Both pea plants share the lane-targeting cadence; ice is carried by the
       pea so a hit applies its slow even if another shooter supplied damage. */
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c) {
            const int idx = r * COLS + c;
            const int kind = m_plant[idx];
            if (kind != PL_PEASHOOTER && kind != PL_SNOWPEA) continue;
            if (++m_shoot[idx] > SHOOT_INTERVAL) m_shoot[idx] = SHOOT_INTERVAL;
            if (m_shoot[idx] >= SHOOT_INTERVAL && enemyInRow(r, CellCX(c) - 8) &&
                m_pc < MAX_PEA) {
                Pea &p = m_p[m_pc++];
                p.row = r;
                p.x = (float)(CellCX(c) + 16);
                p.kind = kind;
                p.alive = 1;
                m_shoot[idx] = 0;
            }
        }

    for (int i = 0; i < m_pc; ++i) {
        if (!m_p[i].alive) continue;
        m_p[i].x += (float)PEA_SPEED;
        if (m_p[i].x > (float)(FIELD_W + 12)) { m_p[i].alive = 0; continue; }
        for (int j = 0; j < m_zc; ++j) {
            if (!m_z[j].alive || m_z[j].row != m_p[i].row) continue;
            const int zx = (int)m_z[j].x;
            if (m_p[i].x >= zx - 18 && m_p[i].x <= zx + 16) {
                damageZombie(j, PEA_DMG,
                             m_p[i].kind == PL_SNOWPEA ? CHILL_TICKS : 0);
                m_p[i].alive = 0;
                if (m_sc < MAX_SPLAT) {
                    Splat &s = m_s[m_sc++];
                    s.cx = (int)m_p[i].x;
                    s.cy = FeetY(m_p[i].row) - 24;
                    s.life = 8;
                    s.alive = 1;
                }
                break;
            }
        }
    }

    for (int i = 0; i < m_zc; ++i) {
        if (!m_z[i].alive) continue;
        Zombie &z = m_z[i];
        const int bc = findBiteCell(z.row, (int)z.x);
        if (bc >= 0) {
            z.eating = 1;
            const int idx = z.row * COLS + bc;
            m_hp[idx] -= EAT;
            if (m_hp[idx] <= 0) {
                m_plant[idx] = PL_NONE;
                m_hp[idx] = 0;
                m_btimer[idx] = 0;
            }
        } else {
            z.eating = 0;
            z.x -= z.chill > 0 ? 0.15f : 0.30f;
            if (z.x <= (float)(HOUSE_W + 8)) {
                z.alive = 0;
                m_phase = PHASE_LOSE;
            }
        }
        if (z.chill > 0) --z.chill;
        if (z.anim < 0x7fff) ++z.anim;
        if (z.flash > 0) --z.flash;
    }

    for (int i = 0; i < m_sc; ++i)
        if (m_s[i].alive && --m_s[i].life <= 0) m_s[i].alive = 0;
    for (int i = 0; i < m_sdc; ++i)
        if (m_sd[i].alive && --m_sd[i].life <= 0) m_sd[i].alive = 0;
    for (int i = 0; i < m_bc; ++i)
        if (m_b[i].alive && --m_b[i].life <= 0) m_b[i].alive = 0;

    if (m_auto && m_wave <= LEVEL_WAVES) {
        if (m_waveTimer > 0) --m_waveTimer;
        if (m_waveTimer == 0 && m_waveSpawned < WaveSize()) {
            m_rng ^= m_rng << 13;
            m_rng ^= m_rng >> 17;
            m_rng ^= m_rng << 5;
            const int row = (int)(m_rng % (unsigned)ROWS);
            const int kind = chooseZombieKind(m_wave, m_waveSpawned);
            const int before = m_zc;
            spawnZombie(row, kind);
            if (m_zc > before) {
                ++m_waveSpawned;
                ++m_spawned;
                m_waveTimer = SPAWN_INTERVAL;
            }
        }
        if (m_waveSpawned >= WaveSize() && Alive() == 0) {
            if (m_wave >= LEVEL_WAVES) {
                m_phase = PHASE_WIN;
            } else {
                ++m_wave;
                m_waveSpawned = 0;
                m_waveTimer = WAVE_BREAK;
            }
        }
    }

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
    w = 0;
    for (int k = 0; k < m_sdc; ++k)
        if (m_sd[k].alive) { if (w != k) m_sd[w] = m_sd[k]; ++w; }
    m_sdc = w;
    w = 0;
    for (int k = 0; k < m_bc; ++k)
        if (m_b[k].alive) { if (w != k) m_b[w] = m_b[k]; ++w; }
    m_bc = w;
}

int Lawn::ZombiesLeft() const
{
    const int left = ZOMBIE_TOTAL - m_killed;
    return left > 0 ? left : 0;
}

int Lawn::Alive() const
{
    int n = 0;
    for (int i = 0; i < m_zc; ++i) if (m_z[i].alive) ++n;
    return n;
}

} /* namespace pvz */
