/* ===========================================================================
 *  pvz_core.h -- Lawn Defense : grid / plant / zombie simulation
 * ---------------------------------------------------------------------------
 *  A bright cartoon "plants vs. lawn-invaders" prototype.  This is the pure
 *  logic half: the lawn grid, the three plant kinds, the peashooter peas, the
 *  walking-eating zombies, the sun economy and the win/lose flags.  It is free
 *  of windows.h / DirectDraw / windsoul so the whole thing runs headless --
 *  see tests/test_pvz.cpp.  pvz_render.cpp only reads the state exposed here;
 *  pvz_game.cpp owns the window and the mouse.
 *
 *  Everything is integer screen pixels, except the zombies' and peas' x which
 *  are floats so the motion is smooth at the fixed 60 Hz step.  One Step() is
 *  exactly 1/60 s.  Colour is NOT dealt with here.
 *
 *  ASCII only, so it compiles under the default (GBK) source code page.
 * ===========================================================================
 */
#ifndef PVZ_CORE_H
#define PVZ_CORE_H

namespace pvz {

/* ------------------------------------------------------------------ layout */
enum {
    FIELD_W = 800,                     /* client area                       */
    FIELD_H = 600,

    TOP_H   = 96,                      /* seed bar + sun strip              */

    HOUSE_W = 76,                      /* the house column on the far left  */

    GRID_X0 = 80,                      /* the lawn grid's left edge         */
    COLS    = 9,
    COL_W   = 80,

    LAWN_Y0 = 100,                     /* the lawn grid's top edge          */
    ROWS    = 5,
    ROW_H   = 100,

    /* ---- seeds -- the seed packets live along the top strip ------------ */
    SEED_X0 = 116,
    SEED_Y  = 10,
    SEED_W  = 66,
    SEED_H  = 76,
    SEED_GAP = 10,

    /* ---- plants --------------------------------------------------------
       Hit points are expressed in "eaten units": a zombie removes EAT from
       the plant every tick, so a peashooter (300) survives 150 ticks = 2.5 s
       of chewing and a wall-nut (1200) survives 10 s. */
    PLANT_HP_PEASHOOTER = 300,
    PLANT_HP_SUNFLOWER  = 300,
    PLANT_HP_WALLNUT    = 1200,

    COST_PEASHOOTER = 100,
    COST_SUNFLOWER  = 50,
    COST_WALLNUT    = 100,

    /* ---- peashooter ---------------------------------------------------- */
    SHOOT_INTERVAL = 90,               /* 1.5 s between peas                */
    PEA_SPEED      = 6,                /* px per tick, to the right         */
    PEA_DMG        = 25,

    /* ---- zombies ------------------------------------------------------- */
    EAT            = 2,                /* plant HP lost per chewing tick    */
    ZOMBIE_HP      = 200,              /* ten peas, or a while of chewing  */
    ZOMBIE_W       = 40,
    ZOMBIE_H       = 88,

    /* ---- sun ----------------------------------------------------------- */
    SUN_START     = 100,              /* enough for one peashooter at t=0    */
    SUN_PERIOD     = 120,              /* auto sun drops every 2 s          */
    SUN_ADD        = 25,
    SUN_CAP        = 9990,
    SUNFLOWER_PERIOD = 300,            /* a sunflower gifts sun every 5 s   */

    /* ---- the level ----------------------------------------------------- */
    ZOMBIE_TOTAL   = 12,
    SPAWN_DELAY    = 90,               /* first zombie after 1.5 s          */
    SPAWN_INTERVAL = 170,

    MAX_ZOMBIE = 24,
    MAX_PEA    = 64,
    MAX_SPLAT  = 24,

    /* the 16-bit key colour for BLIT_MASK (see include/blit16.asm) */
    COL_MASK2 = 0xFF00FF
};

/* ---------------------------------------------------------------- plants */
enum PlantKind {
    PL_NONE = 0,
    PL_PEASHOOTER,
    PL_SUNFLOWER,
    PL_WALLNUT,
    PL_COUNT                          /* 4; kinds 1..PL_COUNT-1 place */
};

/* ---------------------------------------------------------------- phases */
enum Phase {
    PHASE_PLAY = 0,
    PHASE_WIN,
    PHASE_LOSE
};

/* --------------------------------------------------------- geometry helpers
   The lawn is a flat, un-rotated grid -- exactly the readability the cartoon
   look lives on.  These four functions are the single source of truth for the
   cell rectangles: the renderer, the mouse picking and the tests all use them,
   so they can never disagree about where a cell is. */
inline int CellCX(int c) { return GRID_X0 + c * COL_W + COL_W / 2; }
inline int CellCY(int r) { return LAWN_Y0 + r * ROW_H + ROW_H / 2; }
inline int CellTop(int r)    { return LAWN_Y0 + r * ROW_H; }
inline int FeetY(int r)      { return CellCY(r) + 38; }   /* the walk line */

/* -------------------------------------------------------------- entities */
struct Zombie {
    int   row;
    float x;
    int   hp;
    int   anim;        /* frame counter */
    int   eating;      /* 1 while chewing a plant */
    int   flash;       /* hit flash countdown */
    int   alive;
};

struct Pea {
    int   row;
    float x;
    int   alive;
};

struct Splat {
    int cx, cy, life, alive;
};

/* ------------------------------------------------------------------- lawn */
class Lawn {
public:
    Lawn();

    void Step();
    void Reset(unsigned seed);

    /* ---- input seam (pvz_game drives these) ---- */
    void SetSel(int kind);                    /* PlantKind, or PL_NONE       */
    int  Sel() const { return m_sel; }
    void SetAutoSpawn(int on) { m_auto = on ? 1 : 0; }
    int  AutoSpawn() const { return m_auto; }
    bool TryPlant(int c, int r);              /* current selection + sun;    */
                                              /* 1 on success                */

    /* ---- picking ------------------------------------------------------- */
    int CellAt(int px, int py, int &c, int &r) const;  /* 1 if in a cell */
    int SeedIdx(int px, int py) const;                 /* 0..2 or -1     */

    /* ---- read-outs the shell / renderer / tests use ------------------- */
    int  Sun() const { return m_sun; }
    int  Phase() const { return m_phase; }
    int  Tick() const { return m_tick; }
    int  ZombiesLeft() const;               /* not yet eaten AND alive      */
    int  Spawned() const { return m_spawned; }
    int  Alive() const;                      /* zombies currently walking    */

    /* ---- test seams (the shell never calls these) ---- */
    bool TestPlant(int kind, int c, int r);  /* ignore sun, force place      */
    void AddSun(int v) { m_sun += v; if (m_sun > SUN_CAP) m_sun = SUN_CAP; }
    int  SpawnOne(int row);                  /* force one zombie, return idx */
    void ClearPeas();

    /* ---- board queries (test seams) ---- */
    int  PlantAt(int c, int r) const { return m_plant[r * COLS + c]; }
    int  PlantHP(int c, int r) const { return m_hp[r * COLS + c]; }

    /* ---- render iteration ---- */
    int  ZombieCount() const { return m_zc; }
    const Zombie &ZombieAt(int i) const { return m_z[i]; }
    int  PeaCount() const { return m_pc; }
    const Pea &PeaAt(int i) const { return m_p[i]; }
    int  SplatCount() const { return m_sc; }
    const Splat &SplatAt(int i) const { return m_s[i]; }

    /* ---- per-kind art box (renderer anchors the sprite on it) ---- */
    static int PlantW(int kind);
    static int PlantH(int kind);
    static int PlantCost(int kind);

private:
    void spawnZombie(int row);
    int  enemyInRow(int row, int fromX) const;  /* a zombie at/right of fromX */
    int  findBiteCell(int row, int x) const;    /* cell being munched, or -1  */

    unsigned char m_plant[ROWS * COLS];
    int           m_hp[ROWS * COLS];
    int           m_shoot[ROWS * COLS];        /* peashooter cooldown        */
    int           m_stimer[ROWS * COLS];       /* sunflower sun cooldown     */

    Zombie m_z[MAX_ZOMBIE];
    int    m_zc;
    Pea    m_p[MAX_PEA];
    int    m_pc;
    Splat  m_s[MAX_SPLAT];
    int    m_sc;

    int      m_sun;
    int      m_sel;
    int      m_auto;
    int      m_phase;
    int      m_tick;
    int      m_spawned;
    int      m_sunAcc;
    int      m_killed;
    unsigned m_rng;
};

} /* namespace pvz */

#endif /* PVZ_CORE_H */
