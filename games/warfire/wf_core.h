/* ===========================================================================
 *  wf_core.h -- Warfire : game logic
 * ---------------------------------------------------------------------------
 *  Top-down action RPG combat.  Deliberately free of windows.h / DirectDraw /
 *  windsoul so the whole simulation can be exercised headless -- see
 *  tests/test_warfire.cpp.  wf_render.cpp only reads the state exposed here;
 *  wf_game.cpp owns the window, the frame loop and the input.
 *
 *  Time is a fixed 60 Hz step: one World::Step() call is exactly 1/60 s.
 *  Positions are world pixels, colours are 0xRRGGBB.
 * ===========================================================================
 */
#ifndef WF_CORE_H
#define WF_CORE_H

namespace wf {

enum {
    FIELD_W   = 800,                    /* client area                      */
    FIELD_H   = 600,

    MAP_W     = 64,                     /* tiles                            */
    MAP_H     = 48,
    TILE_SIZE = 32,
    WORLD_W   = MAP_W * TILE_SIZE,      /* 2048 x 1536 world                */
    WORLD_H   = MAP_H * TILE_SIZE,

    MAX_MONSTERS = 160,
    MAX_FX       = 320,
    MAX_DROPS    = 48,
    MAX_ARROWS   = 64,

    WIN_WAVES    = 10                 /* clear this wave -> victory      */
};

/* ------------------------------------------------------------------ tiles */
enum TileId {
    TILE_GRASS = 0,
    TILE_DIRT,
    TILE_ROCK,
    TILE_WATER,
    TILE_TREE,
    TILE_COUNT
};

/* --------------------------------------------------------------- monsters */
enum MonKind {
    MON_GRUNT = 0,      /* melee chaser, weak                                  */
    MON_ARCHER,         /* keeps its distance and shoots                       */
    MON_BRUTE,          /* slow, heavy wind-up, hurts a lot                    */
    MON_BOSS,
    MON_KIND_COUNT
};

enum MonState {
    MS_IDLE = 0,
    MS_CHASE,
    MS_WINDUP,          /* telegraph before the blow                          */
    MS_STRIKE,
    MS_RECOVER,
    MS_STUN,            /* hit-stun                                           */
    MS_DYING
};

/* ---------------------------------------------------------------- effects */
enum FxKind {
    FX_TEXT = 0,        /* floating damage number                              */
    FX_SPARK,           /* short-lived hit sparks                              */
    FX_RING,            /* expanding shock ring                                */
    FX_SLASH,           /* sword arc painted on the ground                     */
    FX_WHIRL             /* the spin attack's circle                            */
};

enum DropKind { DROP_COIN = 0, DROP_POTION };

enum Facing { FACE_DOWN = 0, FACE_UP, FACE_LEFT, FACE_RIGHT, FACE_COUNT };

/* ------------------------------------------------------------ app flow */
/* Run-level state.  Stored in the world so the shell (input) and the
   renderer (which only see a World) agree on what to show.  The title /
   controls screens live in the shell, before a run exists. */
enum AppState {
    APP_PLAY  = 0,   /* the simulation runs                            */
    APP_LEVELUP,     /* paused: pick one of three perks                 */
    APP_DEAD,        /* run over (lost)                                 */
    APP_WIN          /* run over (cleared every wave)                   */
};

/* The eight level-up perks.  applyPerk() maps each to a hero buff; the
   strings the player sees live in wf_text.h (WF_LBL_PERK0..). */
enum Perk {
    PERK_BLADE = 0,  /* more attack                                       */
    PERK_IRON,       /* more max hp                                       */
    PERK_BOOTS,      /* faster movement                                   */
    PERK_HASTE,      /* faster attacks                                    */
    PERK_EDGE,       /* more critical chance                              */
    PERK_FANG,       /* hits restore a little hp                          */
    PERK_WIND,       /* dash recharges faster                             */
    PERK_WHIRL,      /* whirlwind cheaper + wider                         */
    PERK_COUNT
};

/* --------------------------------------------------------------- palette */
enum {
    COL_GRASS_A  = 0x3E7A3A,
    COL_GRASS_B  = 0x356B33,
    COL_DIRT_A   = 0x6B5433,
    COL_DIRT_B   = 0x5C4729,
    COL_ROCK_A   = 0x7A7A82,
    COL_ROCK_B   = 0x5A5A62,
    COL_WATER_A  = 0x2A4E7A,
    COL_WATER_B  = 0x22406A,
    COL_TREE_A   = 0x1F4A24,
    COL_TREE_B   = 0x163618,

    COL_HERO_A   = 0xC8D8E8,            /* armour                                */
    COL_HERO_B   = 0x8C9CB0,
    COL_HERO_SKIN= 0xE8B98C,
    COL_HERO_HAIR= 0x4A3020,
    COL_BLADE    = 0xD8E4F0,

    /* Enemies are told apart by hue, and none of them may sit on top of the
       grass hue -- a green grunt on green grass is invisible at 24 px. */
    COL_GRUNT_A  = 0x4AA8A0,
    COL_GRUNT_B  = 0x2C7068,
    COL_ARCHER_A = 0x8A5CB8,
    COL_ARCHER_B = 0x63418A,
    COL_BRUTE_A  = 0xB84A3C,
    COL_BRUTE_B  = 0x84332A,
    COL_BOSS_A   = 0xE0A020,
    COL_BOSS_B   = 0xA07010,

    COL_DMG      = 0xFFF0C0,            /* normal damage number                  */
    COL_DMG_CRIT = 0xFFD23C,            /* critical                              */
    COL_DMG_HURT = 0xFF6A6A,            /* damage the hero takes                 */
    COL_SHADOW   = 0x101418,
    COL_HP       = 0xE03C3C,
    COL_HP_BG    = 0x401414,
    COL_MP       = 0x3C9CE0,
    COL_EXP      = 0xE0C83C,
    COL_UI_BG    = 0x141820,
    COL_UI_EDGE  = 0x5A6A80,
    COL_UI_TEXT  = 0xE8F0FF,
    COL_UI_DIM   = 0x8A9AB0,
    COL_BANNER   = 0xFFD23C
};

/* ------------------------------------------------------------------ types */
struct Monster {
    float x, y;
    float vx, vy;
    float knockX, knockY;
    float radius;
    float reach;                /* attack reach                              */
    int   hp, maxhp;
    int   kind;
    int   state;
    int   timer;                /* state timer                               */
    int   cool;                 /* attack cooldown                           */
    int   windup;
    int   facing;
    int   flash;                /* hit flash frames                          */
    int   anim;
    int   atk;
    float speed;
    int   hitSwing;             /* last hero swing that connected            */
    unsigned char alive;
};

struct Arrow {
    float x, y, vx, vy;
    int   dmg;
    int   life;
    unsigned char alive;
};

struct Fx {
    float x, y;
    float vx, vy;
    int   life, maxlife;
    int   kind;
    int   value;
    int   crit;
    int   facing;
    unsigned long color;
    unsigned char alive;
};

struct Drop {
    float x, y;
    int   kind;
    int   value;
    int   life;
    unsigned char alive;
};

struct Player {
    float x, y;
    float vx, vy;
    int   hp, maxhp;
    int   mp, maxmp;
    int   level, exp, expNext;
    int   gold;
    int   atk;
    int   facing;
    int   anim;
    int   invuln;
    int   hitFlash;

    int   attackPhase;          /* 0 none, 1 windup, 2 active, 3 recover      */
    int   attackTimer;
    int   comboStep;            /* 1..3                                       */
    int   comboWindow;
    int   swingId;              /* incremented per swing, stops double hits   */

    int   dashTimer;
    int   dashCd;
    int   spinTimer;
    int   spinCd;

    /* level-up perk buffs (1.0 = neutral), applied by applyPerk() */
    float spdMul;          /* movement speed multiplier                    */
    float atkSpdMul;       /* attack-timer multiplier (<1 = faster)        */
    float critAdd;         /* additive critical-chance                     */
    float dashCdMul;       /* dash cooldown multiplier (<1 = faster)       */
    float spinCdMul;       /* whirlwind cooldown multiplier                */
    float spinRangeAdd;    /* extra whirlwind reach in px                  */
    int   lifesteal;       /* hp restored when a swing connects            */

    unsigned char alive;
};

struct Stats {
    int frame;
    int wave;
    int kills;
    int toSpawn;                /* monsters still to spawn this wave          */
    int spawnTimer;
    int shake;
    int hitstop;
    int gameover;
    int levelFlash;
    int banner;                 /* wave banner frames left                    */
    int bannerWave;
    int monstersThisWave;
    int killsThisWave;
    int hurtFlash;
    int app;             /* AppState (PLAY / LEVELUP / DEAD / WIN)         */
    int pending;         /* perk picks still owed                          */
};

struct Input {
    float dx, dy;
    int   attack;
    int   dash;
    int   spin;
};

class World {
public:
    World();

    void Reset();
    void Step(const Input &in);

    const Player  &Hero()     const { return m_hero; }
    const Stats   &State()    const { return m_st; }
    const Monster *Monsters() const { return m_mons; }
    const Arrow   *Arrows()   const { return m_arrows; }
    const Fx      *Effects()  const { return m_fx; }
    const Drop    *Drops()    const { return m_drops; }
    const unsigned char *Tiles() const { return m_tile; }

    int LiveMonsters() const;
    int LiveArrows()   const;
    int LiveDrops()    const;
    int LiveFx()       const;

    unsigned char TileAt(int tx, int ty) const;
    int  TileAtWorld(float wx, float wy) const;
    int  Blocked(float wx, float wy) const;

    float CameraX() const;
    float CameraY() const;

    /* --- helpers the tests drive directly ------------------------------- */
    void SpawnMonster(int kind, float x, float y);
    void DamageMonster(int index, int dmg, int crit, float fromX, float fromY);
    void AddFx(int kind, float x, float y, int value, int crit, unsigned long color);
    void DamageHero(int dmg);
    void StartWave(int wave);
    void AttackSweep();                     /* run the active-frame hit test   */

    /* Walks the ring of radius `r` around (cx,cy) starting at bearing `ang`
       and fanning out either side, and returns the first point that is clear
       of blocking tiles -- including the 14 px footprint a monster occupies.
       Monsters are placed with this so they never spawn inside a rock (which
       used to wedge them in place, and made archers fire arrows into a wall
       on the very frame they were born).  Returns false if the whole ring is
       walled in. */
    bool FreeSpot(float cx, float cy, float ang, float r,
                  float &ox, float &oy) const;

    /* Pathfinding / reachability.  updateFlow() floods the walkable tiles
       from the hero every frame, so:
         - DistAt(tx,ty)  is the BFS distance to the hero (0 at the hero,
                          -1 if that tile can never be reached -> walled off);
         - NextAt(idx)    is the neighbouring tile to step to in order to walk
                          towards the hero, or -1 at the hero / unexplored.
       SpawnPos() only ever returns a tile the flood reached, which is the fix
       for "terrain makes the hero never catch the monsters": a monster born
       inside a pocket the hero cannot path into would never die, the wave
       would never end, and the player could never clear it. */
    int  DistAt(int tx, int ty) const;
    int  NextAt(int idx) const;
    bool SpawnPos(float cx, float cy, float r0, float r1,
                  float &ox, float &oy) const;

    /* --- run flow / progression (product) ---------------------------- */
    int  App() const { return m_st.app; }
    int  PendingPerks() const { return m_st.pending; }
    const int *PerkOffers() const { return m_offer; }   /* 3 perk ids   */
    int  PerkTaken(int perk) const
         { return (perk >= 0 && perk < PERK_COUNT) ? m_perkCount[perk] : 0; }
    void ChoosePerk(int slot);          /* apply one of the 3 offers      */

    int  BestWave()  const { return m_bestWave; }
    int  BestKills() const { return m_bestKills; }
    void EnableSave(int on) { m_save = on ? 1 : 0; }
    void LoadSave();
    void SaveSave();

private:
    void updateFlow();
    void applyPerk(int perk);
    void rollOffers();
    void endRun();
    void buildMap();
    void stepHero(const Input &in);
    void stepMonsters();
    void stepArrows();
    void stepFx();
    void stepDrops();
    void stepWave();
    void resolveHeroTiles();
    void resolveMonsterTiles(Monster &m);
    void killMonster(int index);
    void gainExp(int n);
    void shootArrow(const Monster &m);
    void shake(int n) { if (n > m_st.shake) m_st.shake = n; }

    Monster *allocMonster();
    Arrow   *allocArrow();
    Fx      *allocFx();
    Drop    *allocDrop();

    Player  m_hero;
    Stats   m_st;
    Monster m_mons[MAX_MONSTERS];
    Arrow   m_arrows[MAX_ARROWS];
    Fx      m_fx[MAX_FX];
    Drop    m_drops[MAX_DROPS];
    unsigned char m_tile[MAP_W * MAP_H];

    /* BFS flow field from the hero, recomputed every frame in updateFlow().
       m_dist is the tile distance to the hero (-1 = unreachable / walled in);
       m_next is the parent tile to step to when walking toward the hero. */
    int      m_dist[MAP_W * MAP_H];
    int      m_next[MAP_W * MAP_H];

    int      m_monCursor;
    int      m_arrowCursor;
    int      m_fxCursor;
    int      m_dropCursor;
    unsigned m_rng;

    /* progression */
    int      m_offer[3];               /* the 3 perks on offer right now   */
    int      m_perkCount[PERK_COUNT];  /* how many times each was taken    */
    int      m_bestWave, m_bestKills; /* high score (loaded / saved)      */
    int      m_save;                  /* 1 = actually touch the save file */
};

/* xorshift32 -- deterministic, so the tests can rely on the exact sequence. */
unsigned wf_rand(unsigned &state);
float    wf_randf(unsigned &state);         /* [0,1)                           */

} /* namespace wf */

#endif /* WF_CORE_H */
