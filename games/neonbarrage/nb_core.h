/* ===========================================================================
 *  nb_core.h -- Neon Barrage : game logic
 * ---------------------------------------------------------------------------
 *  Deliberately free of windows.h / DirectDraw / windsoul so that the entire
 *  simulation can be exercised headless (see tests/test_neonbarrage.cpp).
 *  nb_render.cpp only reads the state exposed here; nb_game.cpp owns the
 *  window and the frame loop.
 *
 *  Time is a fixed 60 Hz step: one World::Step() call is exactly 1/60 s.
 *  Colours are 0xRRGGBB; the renderer converts them to WPixel.
 * ===========================================================================
 */
#ifndef NB_CORE_H
#define NB_CORE_H

namespace nb {

enum {
    FIELD_W     = 640,      /* playfield, same as the original Ex01 sample   */
    FIELD_H     = 480,

    MAX_BULLETS = 2048,     /* enemy bullets                                  */
    MAX_SHOTS   = 256,      /* player shots                                   */
    MAX_ENEMIES = 32,

    WAVE_LEN    = 900       /* frames per wave: 15 s at 60 Hz                 */
};

/* Cosmetic only -- the renderer maps each shape onto a primitive. */
enum Shape {
    SHAPE_DOT = 0,
    SHAPE_DIAMOND,
    SHAPE_RING,
    SHAPE_SHARD
};

/* Attack patterns an enemy can run. */
enum Pattern {
    PAT_NONE = 0,
    PAT_FAN,
    PAT_RING,
    PAT_SPIRAL,
    PAT_AIMED
};

/* Neon palette, 0xRRGGBB. */
enum {
    COL_BG        = 0x06070F,
    COL_GRID      = 0x101A33,
    COL_PLAYER    = 0x9CFFFF,
    COL_CORE      = 0xFFFFFF,
    COL_SHOT      = 0x8CFFE0,
    COL_ENEMY     = 0xFF3C9E,
    COL_ENEMY_DIM = 0x5A1440,
    COL_BOSS      = 0xFFE24A,
    COL_BOSS_DIM  = 0x5C4410,
    COL_HUD       = 0x7FD4FF,
    COL_HUD_DIM   = 0x3A6A8C,
    COL_WARN      = 0xFF5A5A,

    COL_PAT_FAN    = 0xFF3C9E,
    COL_PAT_RING   = 0xFFE24A,
    COL_PAT_SPIRAL = 0x9C5CFF,
    COL_PAT_AIMED  = 0x40E0FF
};

struct Bullet {
    float x, y;
    float vx, vy;
    float r;
    unsigned long color;
    unsigned char shape;
    unsigned char grazed;   /* the player already earned graze on this one   */
    unsigned char alive;
};

struct Enemy {
    float x, y;
    float vx, vy;
    float r;
    int   hp;
    int   maxhp;
    int   age;
    int   pattern;
    int   boss;
    unsigned char alive;
};

struct Ship {
    float x, y;
    float r;                /* hitbox radius -- deliberately tiny            */
    float graze_r;
    int   invuln;           /* frames of invulnerability left                */
    int   cooldown;         /* frames until the next shot                    */
    int   power;            /* 1..3                                          */
    unsigned char alive;
};

struct Stats {
    int score;
    int lives;
    int bombs;
    int graze;
    int combo;
    int maxCombo;
    int comboTimer;
    int wave;
    int frame;
    int kills;
    int shotsFired;
    int bulletsFired;
    int grazedTotal;        /* every graze ever, for the end screen          */
    int gameover;
    int hitFlash;           /* frames of white flash after taking a hit      */
    int shake;              /* frames of screen shake                        */
    int bombFlash;
};

struct Input {
    float dx, dy;           /* -1..1                                         */
    int   fire;
    int   bomb;
    int   focus;            /* hold to slow down and reveal the hitbox       */
};

class World {
public:
    World();

    void Reset();
    void Step(const Input &in);

    const Ship   &Player()  const { return m_ship; }
    const Stats  &State()   const { return m_st; }
    const Bullet *Bullets() const { return m_bullets; }
    const Bullet *Shots()   const { return m_shots; }
    const Enemy  *Enemies() const { return m_enemies; }

    int LiveBullets() const;
    int LiveShots()   const;
    int LiveEnemies() const;

    /* --- emitters; public so the tests can drive them directly ---------- */
    int EmitFan(float x, float y, int count, float spread, float angle,
                float speed, unsigned long color, int shape);
    int EmitRing(float x, float y, int count, float phase, float speed,
                 unsigned long color, int shape);
    int EmitAimed(float x, float y, float speed, unsigned long color, int shape);
    int EmitSpiral(float x, float y, int arms, float phase, float speed,
                   unsigned long color, int shape);
    int EmitPlayerShot(float x, float y, float vx, float vy, float r);

    void SpawnEnemy(float x, float y, int hp, int pattern, int boss);
    void ClearBullets();            /* bomb, or death penalty              */

    float Difficulty() const;       /* 1.0 .. 4.0                          */

private:
    void stepPlayer(const Input &in);
    void stepBullets();
    void stepEnemies();
    void stepDirector();
    void collide();
    void hitPlayer();
    void bumpCombo();

    Bullet *allocBullet();
    Bullet *allocShot();
    Enemy  *allocEnemy();

    Ship   m_ship;
    Stats  m_st;
    Bullet m_bullets[MAX_BULLETS];
    Bullet m_shots[MAX_SHOTS];
    Enemy  m_enemies[MAX_ENEMIES];

    int    m_waveTimer;
    int    m_spawnTimer;
    float  m_spiralPhase;
    int    m_bossPending;
    int    m_bulletCursor;
    int    m_shotCursor;
    int    m_enemyCursor;
    unsigned m_rng;
};

/* xorshift32 -- deterministic, so the tests can rely on the exact sequence. */
unsigned nb_rand(unsigned &state);
float    nb_randf(unsigned &state);         /* [0,1)                       */

} /* namespace nb */

#endif /* NB_CORE_H */
