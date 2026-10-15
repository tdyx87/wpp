/* ===========================================================================
 *  pvz_core.h -- Lawn Defense : grid / plant / zombie simulation
 * ---------------------------------------------------------------------------
 *  Pure logic for the cartoon lawn-defense prototype. It has no dependency on
 *  windows.h, DirectDraw or Windsoul, so the simulation runs headless in tests.
 *  One Step() is exactly 1/60 second. Positions are integer screen pixels except
 *  for the smoothly moving x coordinates of zombies and peas.
 *
 *  ASCII only, so it compiles under the default (GBK) source code page.
 * ===========================================================================
 */
#ifndef PVZ_CORE_H
#define PVZ_CORE_H

namespace pvz {

enum {
    FIELD_W = 800,
    FIELD_H = 600,
    TOP_H = 96,
    HOUSE_W = 76,
    GRID_X0 = 80,
    COLS = 9,
    COL_W = 80,
    LAWN_Y0 = 100,
    ROWS = 5,
    ROW_H = 100,

    SEED_X0 = 116,
    SEED_Y = 10,
    SEED_W = 66,
    SEED_H = 76,
    SEED_GAP = 10,

    PLANT_HP_PEASHOOTER = 300,
    PLANT_HP_SUNFLOWER = 300,
    PLANT_HP_WALLNUT = 1200,
    PLANT_HP_SNOWPEA = 300,
    PLANT_HP_CHERRY = 220,

    COST_PEASHOOTER = 100,
    COST_SUNFLOWER = 50,
    COST_WALLNUT = 100,
    COST_SNOWPEA = 175,
    COST_CHERRY = 150,

    SHOOT_INTERVAL = 90,
    PEA_SPEED = 6,
    PEA_DMG = 25,
    CHILL_TICKS = 240,
    CHERRY_FUSE = 36,
    CHERRY_DMG = 2000,
    BLAST_TICKS = 28,

    EAT = 2,
    ZOMBIE_HP = 200,
    CONE_HP = 560,
    BUCKET_HP = 1100,
    ZOMBIE_W = 40,
    ZOMBIE_H = 88,

    SUN_START = 100,
    SUN_PERIOD = 120,
    SUN_ADD = 25,
    SUN_CAP = 9990,
    SUNFLOWER_PERIOD = 300,
    SUN_DROP_LIFE = 600,
    SUN_PICK_RADIUS = 26,

    LEVEL_WAVES = 10,
    ZOMBIE_TOTAL = 79,
    SPAWN_DELAY = 90,
    SPAWN_INTERVAL = 110,
    WAVE_BREAK = 90,

    MAX_ZOMBIE = 96,
    MAX_PEA = 96,
    MAX_SPLAT = 32,
    MAX_SUN_DROP = 20,
    MAX_BLAST = 16,

    COL_MASK2 = 0xFF00FF
};

enum PlantKind {
    PL_NONE = 0,
    PL_PEASHOOTER,
    PL_SUNFLOWER,
    PL_WALLNUT,
    PL_SNOWPEA,
    PL_CHERRY,
    PL_COUNT
};

enum ZombieKind {
    ZK_NORMAL = 0,
    ZK_CONE,
    ZK_BUCKET
};

enum Phase {
    PHASE_PLAY = 0,
    PHASE_WIN,
    PHASE_LOSE
};

inline int CellCX(int c) { return GRID_X0 + c * COL_W + COL_W / 2; }
inline int CellCY(int r) { return LAWN_Y0 + r * ROW_H + ROW_H / 2; }
inline int CellTop(int r) { return LAWN_Y0 + r * ROW_H; }
inline int FeetY(int r) { return CellCY(r) + 38; }

struct Zombie {
    int row;
    float x;
    int hp;
    int kind;
    int chill;
    int anim;
    int eating;
    int flash;
    int alive;
};

struct Pea {
    int row;
    float x;
    int kind;
    int alive;
};

struct Splat {
    int cx, cy, life, alive;
};

struct SunDrop {
    int x, y, life, amount, alive;
};

struct Blast {
    int cx, cy, life, alive;
};

class Lawn {
public:
    Lawn();

    void Step();
    void Reset(unsigned seed);

    void SetSel(int kind);
    int Sel() const { return m_sel; }
    void SetAutoSpawn(int on) { m_auto = on ? 1 : 0; }
    int AutoSpawn() const { return m_auto; }
    bool TryPlant(int c, int r);
    bool TryCollectSun(int px, int py);

    int CellAt(int px, int py, int &c, int &r) const;
    int SeedIdx(int px, int py) const;

    int Sun() const { return m_sun; }
    int Phase() const { return m_phase; }
    int Tick() const { return m_tick; }
    int ZombiesLeft() const;
    int Spawned() const { return m_spawned; }
    int Alive() const;
    int Wave() const { return m_wave; }
    int WaveSpawned() const { return m_waveSpawned; }
    int WaveSize() const;
    int Kills() const { return m_killed; }
    int CollectedSun() const { return m_collected; }

    bool TestPlant(int kind, int c, int r);
    void AddSun(int v);
    int SpawnOne(int row, int kind = ZK_NORMAL);
    bool TestSunDrop(int x, int y, int amount);
    void ClearPeas();

    int PlantAt(int c, int r) const { return m_plant[r * COLS + c]; }
    int PlantHP(int c, int r) const { return m_hp[r * COLS + c]; }
    int PlantFuse(int c, int r) const { return m_btimer[r * COLS + c]; }

    int ZombieCount() const { return m_zc; }
    const Zombie &ZombieAt(int i) const { return m_z[i]; }
    int PeaCount() const { return m_pc; }
    const Pea &PeaAt(int i) const { return m_p[i]; }
    int SplatCount() const { return m_sc; }
    const Splat &SplatAt(int i) const { return m_s[i]; }
    int SunDropCount() const { return m_sdc; }
    const SunDrop &SunDropAt(int i) const { return m_sd[i]; }
    int BlastCount() const { return m_bc; }
    const Blast &BlastAt(int i) const { return m_b[i]; }

    static int PlantW(int kind);
    static int PlantH(int kind);
    static int PlantCost(int kind);
    static int WaveSize(int wave);
    static int ZombieHp(int kind);

private:
    void spawnZombie(int row, int kind);
    void damageZombie(int index, int amount, int chill);
    void dropSun(int x, int y, int amount);
    void makeBlast(int cx, int cy);
    int chooseZombieKind(int wave, int order) const;
    int enemyInRow(int row, int fromX) const;
    int findBiteCell(int row, int x) const;

    unsigned char m_plant[ROWS * COLS];
    int m_hp[ROWS * COLS];
    int m_shoot[ROWS * COLS];
    int m_stimer[ROWS * COLS];
    int m_btimer[ROWS * COLS];

    Zombie m_z[MAX_ZOMBIE];
    int m_zc;
    Pea m_p[MAX_PEA];
    int m_pc;
    Splat m_s[MAX_SPLAT];
    int m_sc;
    SunDrop m_sd[MAX_SUN_DROP];
    int m_sdc;
    Blast m_b[MAX_BLAST];
    int m_bc;

    int m_sun;
    int m_sel;
    int m_auto;
    int m_phase;
    int m_tick;
    int m_spawned;
    int m_sunAcc;
    int m_killed;
    int m_collected;
    int m_wave;
    int m_waveSpawned;
    int m_waveTimer;
    unsigned m_rng;
};

} /* namespace pvz */

#endif /* PVZ_CORE_H */
