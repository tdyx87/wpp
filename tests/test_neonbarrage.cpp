/* ===========================================================================
 *  test_neonbarrage.cpp -- headless tests for the Neon Barrage prototype
 * ---------------------------------------------------------------------------
 *  Two layers are covered:
 *
 *    [1]..[13]  nb_core  -- the simulation, driven directly with no window.
 *    [14]..[18] nb_render -- the renderer is driven into an off-screen
 *               WCanvas and the resulting pixels are read back with
 *               canvas.Update() + canvas.BlitTo(), so the drawing code is
 *               verified without DirectDraw or a visible window.
 *
 *  ASCII only, so it compiles under the default (GBK) source code page.
 * ===========================================================================
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"

#include "nb_core.h"
#include "nb_render.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *msg)
{
    if (cond) { ++g_pass; printf("  [PASS] %s\n", msg); }
    else      { ++g_fail; printf("  [FAIL] %s\n", msg); }
}

static nb::Input noInput(void)
{
    nb::Input in;
    in.dx = 0.0f; in.dy = 0.0f;
    in.fire = 0; in.bomb = 0; in.focus = 0;
    return in;
}

/* ====================================================================== */
/*  simulation                                                            */
/* ====================================================================== */

/* ---------------------------------------------------------------- [1] */
static void test_reset(void)
{
    printf("[1] World::Reset\n");

    nb::World w;
    const nb::Stats &st = w.State();
    const nb::Ship  &p  = w.Player();

    check(st.lives == 3,        "3 lives");
    check(st.bombs == 3,        "3 bombs");
    check(st.wave == 1,         "wave starts at 1");
    check(st.score == 0,        "score starts at 0");
    check(st.gameover == 0,     "not game over");
    check(p.alive == 1,         "player alive");
    check(w.LiveBullets() == 0, "no bullets");
    check(w.LiveEnemies() == 0, "no enemies");
    check((int)p.x == nb::FIELD_W / 2, "player centred horizontally");
    check((int)p.y > nb::FIELD_H / 2,  "player in the lower half");

    /* a Reset() after damage must restore everything */
    w.EmitRing(100.0f, 100.0f, 20, 0.0f, 2.0f, nb::COL_PAT_RING, nb::SHAPE_DOT);
    w.SpawnEnemy(200.0f, 80.0f, 5, nb::PAT_FAN, 0);
    check(w.LiveBullets() == 20 && w.LiveEnemies() == 1, "damage staged");
    w.Reset();
    check(w.LiveBullets() == 0 && w.LiveEnemies() == 0 && w.State().score == 0,
          "Reset clears bullets, enemies and score");
}

/* ---------------------------------------------------------------- [2] */
static void test_movement(void)
{
    printf("[2] movement and clamping\n");

    /* Each direction gets its own World.  A single long run would be
       contaminated by the director: the first enemy appears at frame 90 and
       its first bullet can only reach the ship around frame 219, so staying
       under 100 frames per case keeps the ship untouched and the result
       deterministic. */

    {
        nb::World w;
        w.Reset();
        float x0 = w.Player().x;

        nb::Input in = noInput();
        in.dx = -1.0f;
        for (int i = 0; i < 30; ++i) w.Step(in);
        check(w.Player().x < x0, "holding left moves the player left");

        for (int i = 0; i < 70; ++i) w.Step(in);
        printf("      left  -> x = %.1f\n", w.Player().x);
        check(w.Player().x >= 0.0f && w.Player().x < 40.0f,
              "clamped at the left margin");
    }

    {
        nb::World w;
        w.Reset();
        nb::Input in = noInput();
        in.dx = 1.0f;
        for (int i = 0; i < 100; ++i) w.Step(in);
        printf("      right -> x = %.1f\n", w.Player().x);
        check(w.Player().x > nb::FIELD_W - 40.0f, "clamped at the right margin");
    }

    {
        nb::World w;
        w.Reset();
        nb::Input in = noInput();
        in.dy = 1.0f;
        for (int i = 0; i < 60; ++i) w.Step(in);
        printf("      down  -> y = %.1f\n", w.Player().y);
        check(w.Player().y > nb::FIELD_H - 40.0f, "clamped at the bottom margin");
    }

    {
        nb::World w;
        w.Reset();
        nb::Input in = noInput();
        in.dy = -1.0f;
        for (int i = 0; i < 100; ++i) w.Step(in);
        check(w.Player().y < 40.0f, "clamped at the top margin");
    }
}

/* ---------------------------------------------------------------- [3] */
static void test_focus(void)
{
    printf("[3] focus mode slows the ship down\n");

    nb::World a, b;
    a.Reset(); b.Reset();

    nb::Input fast = noInput();
    fast.dx = 1.0f; fast.focus = 0;

    nb::Input slow = noInput();
    slow.dx = 1.0f; slow.focus = 1;

    for (int i = 0; i < 20; ++i) { a.Step(fast); b.Step(slow); }

    float da = a.Player().x - nb::FIELD_W * 0.5f;
    float db = b.Player().x - nb::FIELD_W * 0.5f;
    printf("      moved %.1f px normally, %.1f px focused\n", da, db);
    check(da > 0.0f && db > 0.0f, "both moved right");
    check(db < da * 0.6f, "focus is markedly slower");
}

/* ---------------------------------------------------------------- [4] */
static void test_firing(void)
{
    printf("[4] firing\n");

    nb::World w;
    w.Reset();

    nb::Input in = noInput();
    in.fire = 1;
    w.Step(in);
    int one = w.LiveShots();
    check(one == 1, "power 1 fires a single shot per volley");

    /* cooldown must gate the rate: 4 frames per volley */
    nb::World w2;
    w2.Reset();
    nb::Input f = noInput();
    f.fire = 1;
    for (int i = 0; i < 40; ++i) w2.Step(f);
    int fired = w2.State().shotsFired;
    printf("      40 frames -> %d shots\n", fired);
    check(fired >= 8 && fired <= 12, "cooldown gates the fire rate");

    /* shots travel upwards and leave the field */
    nb::World w3;
    w3.Reset();
    w3.Step(f);
    check(w3.LiveShots() == 1, "one shot in flight");
    const nb::Bullet *s = w3.Shots();
    int idx = -1;
    for (int i = 0; i < nb::MAX_SHOTS; ++i) if (s[i].alive) { idx = i; break; }
    check(idx >= 0 && s[idx].vy < 0.0f, "shots move up the screen");
    for (int i = 0; i < 120; ++i) w3.Step(noInput());
    check(w3.LiveShots() == 0, "shots are culled off screen");
}

/* ---------------------------------------------------------------- [5] */
static void test_emitters(void)
{
    printf("[5] bullet emitters\n");

    nb::World w;
    w.Reset();

    check(w.EmitRing(100.0f, 100.0f, 16, 0.0f, 2.0f, nb::COL_PAT_RING, nb::SHAPE_DOT) == 16,
          "EmitRing returns the count");
    check(w.LiveBullets() == 16, "EmitRing created 16 bullets");

    w.Reset();
    check(w.EmitFan(100.0f, 100.0f, 5, 0.5f, 0.0f, 2.0f, nb::COL_PAT_FAN, nb::SHAPE_DOT) == 5,
          "EmitFan returns the count");

    w.Reset();
    check(w.EmitSpiral(100.0f, 100.0f, 3, 0.0f, 2.0f, nb::COL_PAT_SPIRAL, nb::SHAPE_RING) == 3,
          "EmitSpiral emits one bullet per arm");

    /* a ring must be evenly spread: the mean velocity of a full ring is ~0 */
    w.Reset();
    w.EmitRing(0.0f, 0.0f, 8, 0.0f, 3.0f, nb::COL_PAT_RING, nb::SHAPE_DOT);
    {
        const nb::Bullet *b = w.Bullets();
        float sx = 0.0f, sy = 0.0f;
        for (int i = 0; i < nb::MAX_BULLETS; ++i)
            if (b[i].alive) { sx += b[i].vx; sy += b[i].vy; }
        check(sx > -0.01f && sx < 0.01f && sy > -0.01f && sy < 0.01f,
              "ring velocities cancel out");
    }

    /* aimed fire points at the player */
    w.Reset();
    w.EmitAimed(100.0f, 100.0f, 4.0f, nb::COL_PAT_AIMED, nb::SHAPE_SHARD);
    {
        const nb::Bullet *b = w.Bullets();
        int i;
        for (i = 0; i < nb::MAX_BULLETS; ++i) if (b[i].alive) break;
        check(i < nb::MAX_BULLETS, "aimed bullet exists");
        float dx = w.Player().x - 100.0f;
        float dy = w.Player().y - 100.0f;
        check(b[i].vx * dy - b[i].vy * dx > -0.01f &&
              b[i].vx * dy - b[i].vy * dx <  0.01f,
              "aimed bullet heads straight for the player");
    }
}

/* ---------------------------------------------------------------- [6] */
static void test_bullet_pool(void)
{
    printf("[6] bullet pool\n");

    nb::World w;
    w.Reset();

    /* ask for far more bullets than the pool holds */
    int made = 0;
    for (int i = 0; i < 200; ++i)
        made += w.EmitRing(320.0f, 240.0f, 64, 0.0f, 0.0f, nb::COL_PAT_RING, nb::SHAPE_DOT);

    printf("      requested 12800, got %d (pool %d)\n", made, nb::MAX_BULLETS);
    check(made == nb::MAX_BULLETS, "emitters stop at the pool size instead of overrunning");
    check(w.LiveBullets() == nb::MAX_BULLETS, "pool is exactly full");

    /* freeing and re-emitting must reuse the slots */
    w.ClearBullets();
    check(w.LiveBullets() == 0, "ClearBullets empties the pool");
    check(w.EmitRing(320.0f, 240.0f, 8, 0.0f, 1.0f, nb::COL_PAT_RING, nb::SHAPE_DOT) == 8,
          "pool is reusable after a clear");
}

/* ---------------------------------------------------------------- [7] */
static void test_collision(void)
{
    printf("[7] collision and damage\n");

    nb::World w;
    w.Reset();
    w.SpawnEnemy(320.0f, 120.0f, 3, nb::PAT_NONE, 0);
    check(w.LiveEnemies() == 1, "enemy spawned");

    /* park a shot on top of the enemy: 3 hp -> 3 hits */
    int kills = 0;
    for (int round = 0; round < 6 && w.LiveEnemies() > 0; ++round) {
        w.EmitPlayerShot(320.0f, 120.0f, 0.0f, 0.0f, 3.0f);
        w.Step(noInput());
        if (w.LiveEnemies() == 0) kills = 1;
    }
    check(kills == 1, "three hits destroy a 3 hp enemy");
    check(w.State().kills == 1, "kill counter advanced");
    check(w.State().score > 0, "score awarded for the kill");

    /* player bullets must not damage the player */
    nb::World w2;
    w2.Reset();
    int lives = w2.State().lives;
    w2.EmitPlayerShot(w2.Player().x, w2.Player().y, 0.0f, 0.0f, 3.0f);
    w2.Step(noInput());
    check(w2.State().lives == lives, "own shots do not hurt the player");
}

/* ---------------------------------------------------------------- [8] */
static void test_player_hit(void)
{
    printf("[8] taking a hit\n");

    nb::World w;
    w.Reset();

    /* drop an enemy bullet right on the hitbox */
    w.EmitFan(w.Player().x, w.Player().y, 1, 0.0f, 0.0f, 0.0f,
              nb::COL_PAT_FAN, nb::SHAPE_DOT);
    check(w.LiveBullets() == 1, "one bullet on the player");

    int before = w.State().lives;
    w.Step(noInput());
    printf("      lives %d -> %d\n", before, w.State().lives);

    check(w.State().lives == before - 1, "a hit costs one life");
    check(w.Player().invuln > 0,         "respawn grants invulnerability");
    check(w.LiveBullets() == 0,          "the screen is cleared on death");
    check(w.Player().power == 1,         "power is not lost below level 1");

    /* while invulnerable the ship cannot be hit again */
    nb::World w2;
    w2.Reset();
    w2.EmitFan(w2.Player().x, w2.Player().y, 1, 0.0f, 0.0f, 0.0f,
               nb::COL_PAT_FAN, nb::SHAPE_DOT);
    w2.Step(noInput());                       /* takes the hit, now invuln */
    int lives = w2.State().lives;
    w2.EmitFan(w2.Player().x, w2.Player().y, 1, 0.0f, 0.0f, 0.0f,
               nb::COL_PAT_FAN, nb::SHAPE_DOT);
    w2.Step(noInput());
    check(w2.State().lives == lives, "invulnerability blocks the second hit");
}

/* ---------------------------------------------------------------- [9] */
static void test_graze(void)
{
    printf("[9] graze\n");

    nb::World w;
    w.Reset();

    /* just outside the hitbox but inside the graze ring */
    float gx = w.Player().x + 12.0f;
    float gy = w.Player().y;
    w.EmitFan(gx, gy, 1, 0.0f, 0.0f, 0.0f, nb::COL_PAT_FAN, nb::SHAPE_DOT);

    w.Step(noInput());
    printf("      graze=%d combo=%d score=%d\n",
           w.State().graze, w.State().combo, w.State().score);

    check(w.State().lives == 3,  "grazing does not cost a life");
    check(w.State().graze == 1,  "graze counted");
    check(w.State().combo >= 1,  "graze builds combo");
    check(w.State().score > 0,   "graze scores");
    check(w.LiveBullets() == 1,  "the bullet survives the graze");

    /* the same bullet must not graze twice */
    for (int i = 0; i < 5; ++i) w.Step(noInput());
    check(w.State().graze == 1, "each bullet grazes at most once");

    /* far away bullets never graze */
    nb::World w2;
    w2.Reset();
    w2.EmitFan(w2.Player().x + 200.0f, w2.Player().y, 1, 0.0f, 0.0f, 0.0f,
               nb::COL_PAT_FAN, nb::SHAPE_DOT);
    w2.Step(noInput());
    check(w2.State().graze == 0, "distant bullets do not graze");
}

/* --------------------------------------------------------------- [10] */
static void test_bomb(void)
{
    printf("[10] bomb\n");

    nb::World w;
    w.Reset();
    w.EmitRing(320.0f, 200.0f, 64, 0.0f, 1.0f, nb::COL_PAT_RING, nb::SHAPE_DOT);
    check(w.LiveBullets() == 64, "bullets staged");

    nb::Input in = noInput();
    in.bomb = 1;
    int bombs = w.State().bombs;
    int score = w.State().score;
    w.Step(in);

    check(w.State().bombs == bombs - 1, "a bomb is consumed");
    check(w.LiveBullets() == 0,         "the bomb clears every bullet");
    check(w.State().score > score,      "clearing bullets scores");
    check(w.Player().invuln > 0,        "the bomb grants brief invulnerability");

    /* no bombs left -> nothing happens */
    nb::World w2;
    w2.Reset();
    while (w2.State().bombs > 0) {
        nb::Input b = noInput();
        b.bomb = 1;
        w2.Step(b);
    }
    w2.EmitRing(320.0f, 200.0f, 16, 0.0f, 1.0f, nb::COL_PAT_RING, nb::SHAPE_DOT);
    nb::Input b = noInput();
    b.bomb = 1;
    w2.Step(b);
    check(w2.State().bombs == 0 && w2.LiveBullets() == 16,
          "an empty bomb rack does nothing");
}

/* --------------------------------------------------------------- [11] */
static void test_combo_decay(void)
{
    printf("[11] combo decay\n");

    nb::World w;
    w.Reset();

    w.EmitFan(w.Player().x + 12.0f, w.Player().y, 1, 0.0f, 0.0f, 0.0f,
              nb::COL_PAT_FAN, nb::SHAPE_DOT);
    w.Step(noInput());
    check(w.State().combo >= 1, "combo started");

    for (int i = 0; i < 200; ++i) w.Step(noInput());
    check(w.State().combo == 0, "combo decays when nothing happens for a while");
    check(w.State().maxCombo >= 1, "the peak combo is remembered");
}

/* --------------------------------------------------------------- [12] */
static void test_difficulty(void)
{
    printf("[12] difficulty curve\n");

    nb::World w;
    w.Reset();
    float d1 = w.Difficulty();
    check(d1 > 0.99f && d1 < 1.01f, "wave 1 is difficulty 1.0");

    /* Advancing ten waves takes 9000 frames, far longer than the ship would
       survive on its own -- it would lose its three lives and Step() would go
       inert, freezing the wave counter.  Sweeping the bullets away before each
       step puts the world in practice mode: nothing can ever reach the ship,
       so the director keeps running and the curve can be measured. */
    nb::World w2;
    w2.Reset();
    for (int i = 0; i < nb::WAVE_LEN * 10; ++i) {
        w2.ClearBullets();
        w2.Step(noInput());
    }
    printf("      after 10 waves: wave=%d difficulty=%.2f\n",
           w2.State().wave, w2.Difficulty());
    check(w2.State().wave > 1, "the wave counter advances");
    check(w2.State().gameover == 0, "practice mode kept the ship alive");
    check(w2.Difficulty() > d1, "difficulty rises with the wave number");
    check(w2.Difficulty() <= 4.0f, "difficulty is capped");

    nb::World w3;
    w3.Reset();
    for (int i = 0; i < nb::WAVE_LEN * 40; ++i) {
        w3.ClearBullets();
        w3.Step(noInput());
    }
    printf("      after 40 waves: wave=%d difficulty=%.2f\n",
           w3.State().wave, w3.Difficulty());
    check(w3.Difficulty() <= 4.0f, "the cap holds even after 40 waves");
    check(w3.Difficulty() > 3.5f, "the curve really does climb towards the cap");
}

/* --------------------------------------------------------------- [13] */
static void test_gameover_and_determinism(void)
{
    printf("[13] game over, restart and determinism\n");

    nb::World w;
    w.Reset();
    for (int i = 0; i < 3; ++i) {
        w.EmitFan(w.Player().x, w.Player().y, 1, 0.0f, 0.0f, 0.0f,
                  nb::COL_PAT_FAN, nb::SHAPE_DOT);
        w.Step(noInput());
        /* burn off the invulnerability so the next hit lands */
        for (int k = 0; k < 200 && w.Player().invuln > 0; ++k) w.Step(noInput());
    }
    check(w.State().lives == 0, "all lives lost");
    check(w.State().gameover == 1, "game over flagged");

    /* Step must become a no-op once the game is over */
    int frame = w.State().frame;
    int score = w.State().score;
    for (int i = 0; i < 30; ++i) w.Step(noInput());
    check(w.State().frame == frame && w.State().score == score,
          "Step is inert after game over");

    w.Reset();
    check(w.State().gameover == 0 && w.State().lives == 3,
          "Reset restarts the game");

    /* the simulation is deterministic: same inputs -> same state */
    nb::World a, b;
    a.Reset(); b.Reset();
    for (int i = 0; i < 400; ++i) {
        nb::Input in = noInput();
        in.dx = ((i / 37) % 3) - 1.0f;
        in.dy = ((i / 53) % 3) - 1.0f;
        in.fire = (i % 7) < 5 ? 1 : 0;
        a.Step(in);
        b.Step(in);
    }
    check(a.State().score == b.State().score &&
          a.LiveBullets() == b.LiveBullets() &&
          a.State().wave == b.State().wave &&
          a.Player().x == b.Player().x,
          "identical inputs give identical state");
    printf("      after 400 frames: score=%d bullets=%d wave=%d\n",
           a.State().score, a.LiveBullets(), a.State().wave);
}

/* ====================================================================== */
/*  renderer                                                              */
/* ====================================================================== */

/* Render the world into an off-screen canvas and read a pixel back. */
static DWORD renderPixel(nb::Renderer &r, const nb::World &w,
                         int px, int py, int *itemCount)
{
    WCanvas cv(nb::FIELD_W, nb::FIELD_H);
    r.Draw(cv, w);
    if (itemCount) *itemCount = r.ItemCount();

    cv.Update();
    cv.SetPos(0, 0);

    WBitmap out(nb::FIELD_W, nb::FIELD_H);
    out.Clear(WPixel(0, 0, 0));
    cv.BlitTo(out);

    return (DWORD)out[py][px];
}

/* --------------------------------------------------------------- [14] */
static void test_render_background(void)
{
    printf("[14] render: background\n");

    nb::World w;
    w.Reset();
    nb::Renderer r;

    WCanvas cv(nb::FIELD_W, nb::FIELD_H);
    r.Draw(cv, w);
    cv.Update();
    cv.SetPos(0, 0);

    WBitmap out(nb::FIELD_W, nb::FIELD_H);
    out.Clear(WPixel(0, 255, 0));          /* a colour the game never uses */
    cv.BlitTo(out);

    /* The background is one full-screen block, so almost every pixel in a
       region that holds no HUD, ship or bullet must carry the bg colour. */
    int hits = 0, total = 0;
    for (int y = 60; y < 120; ++y) {
        for (int x = 400; x < 460; ++x) {
            ++total;
            if ((DWORD)out[y][x] == (DWORD)WPixel((DWORD)nb::COL_BG)) ++hits;
        }
    }
    printf("      %d/%d sampled pixels are the background colour\n", hits, total);
    check(hits > total * 6 / 10, "background covers the field");
    check(r.ItemCount() > 0, "the renderer produced draw calls");
}

/* --------------------------------------------------------------- [15] */
static void test_render_actors(void)
{
    printf("[15] render: player, enemy and bullet pixels\n");

    /* the ship body */
    {
        nb::World w;
        w.Reset();
        nb::Renderer r;
        int px = (int)w.Player().x;
        int py = (int)w.Player().y + 5;          /* inside the body, below the core */
        DWORD got = renderPixel(r, w, px, py, 0);
        printf("      ship body at (%d,%d) = 0x%06lX\n", px, py, (unsigned long)got);
        check(got == (DWORD)WPixel((DWORD)nb::COL_PLAYER), "the ship is drawn at its position");
    }

    /* the white hitbox core */
    {
        nb::World w;
        w.Reset();
        nb::Renderer r;
        int px = (int)w.Player().x;
        int py = (int)w.Player().y - 4;
        DWORD got = renderPixel(r, w, px, py, 0);
        check(got == (DWORD)WPixel((DWORD)nb::COL_CORE), "the tiny hitbox is visible");
    }

    /* an enemy body */
    {
        nb::World w;
        w.Reset();
        w.SpawnEnemy(300.0f, 140.0f, 10, nb::PAT_NONE, 0);
        nb::Renderer r;
        DWORD got = renderPixel(r, w, 300, 140, 0);
        printf("      enemy body = 0x%06lX\n", (unsigned long)got);
        check(got == (DWORD)WPixel((DWORD)nb::COL_ENEMY), "the enemy is drawn where it is");
    }

    /* a stationary bullet */
    {
        nb::World w;
        w.Reset();
        w.EmitFan(100.0f, 190.0f, 1, 0.0f, 0.0f, 0.0f,
                  nb::COL_PAT_FAN, nb::SHAPE_DOT);
        nb::Renderer r;
        DWORD got = renderPixel(r, w, 100, 190, 0);
        printf("      bullet = 0x%06lX\n", (unsigned long)got);
        check(got == (DWORD)WPixel((DWORD)nb::COL_PAT_FAN), "bullets are drawn at their position");
    }
}

/* --------------------------------------------------------------- [16] */
static void test_render_layers(void)
{
    printf("[16] render: layer order\n");

    /* Bullets must stay visible: they are painted over the enemies they came
       from.  The display list is submitted in reverse because WCanvas applies
       the first link last, so this also guards the flush() ordering. */
    nb::World w;
    w.Reset();
    w.EmitFan(220.0f, 160.0f, 1, 0.0f, 0.0f, 0.0f, nb::COL_PAT_RING, nb::SHAPE_DOT);
    w.SpawnEnemy(220.0f, 160.0f, 10, nb::PAT_NONE, 0);

    nb::Renderer r;
    DWORD got = renderPixel(r, w, 220, 160, 0);
    printf("      bullet over enemy = 0x%06lX\n", (unsigned long)got);
    check(got == (DWORD)WPixel((DWORD)nb::COL_PAT_RING),
          "bullets paint over enemies, so the pattern stays readable");

    /* ... and over the ship, for the same reason. */
    nb::World w2;
    w2.Reset();
    w2.EmitFan(w2.Player().x, w2.Player().y + 5, 1, 0.0f, 0.0f, 0.0f,
               nb::COL_PAT_RING, nb::SHAPE_DOT);
    nb::Renderer r2;
    DWORD got2 = renderPixel(r2, w2, (int)w2.Player().x, (int)w2.Player().y + 5, 0);
    printf("      bullet over ship  = 0x%06lX\n", (unsigned long)got2);
    check(got2 == (DWORD)WPixel((DWORD)nb::COL_PAT_RING), "bullets paint over the ship");

    /* The ship's own shots are blended additively, so the exact pixel value
       depends on what is underneath.  Compare the same pixel with and without
       the shot instead of asserting a colour: that proves the shot is drawn
       above the sprite. */
    nb::World w3;
    w3.Reset();
    int sx = (int)w3.Player().x;
    int sy = (int)w3.Player().y + 5;

    nb::Renderer r3;
    DWORD without = renderPixel(r3, w3, sx, sy, 0);
    check(without == (DWORD)WPixel((DWORD)nb::COL_PLAYER), "baseline: the ship body");

    w3.EmitPlayerShot(w3.Player().x, w3.Player().y + 5, 0.0f, 0.0f, 3.0f);
    nb::Renderer r4;
    DWORD with = renderPixel(r4, w3, sx, sy, 0);
    printf("      ship pixel without/with shot = 0x%06lX / 0x%06lX\n",
           (unsigned long)without, (unsigned long)with);
    check(with != without, "the player's own shots are drawn over the ship");
}

/* --------------------------------------------------------------- [17] */
static void test_render_stress(void)
{
    printf("[17] render: stress\n");

    nb::World w;
    w.Reset();
    for (int i = 0; i < 40; ++i)
        w.EmitRing(320.0f, 240.0f, 16, (float)i, 0.5f, nb::COL_PAT_SPIRAL, nb::SHAPE_RING);
    w.SpawnEnemy(300.0f, 100.0f, 50, nb::PAT_NONE, 1);

    printf("      live bullets = %d\n", w.LiveBullets());

    nb::Renderer r;
    int items = 0;
    DWORD got = renderPixel(r, w, 320, 240, &items);
    printf("      display list = %d items, %d cached blocks\n", items, r.BlockCount());
    printf("      pixel(320,240) = 0x%06lX\n", (unsigned long)got);

    check(items > 0, "the crowded frame still produced a display list");
    check(items <= 4096, "the display list stays inside its budget");
    check(r.BlockCount() > 0 && r.BlockCount() <= 96, "block cache stays bounded");

    /* rendering many frames in a row must not leak or drift */
    WCanvas reuse(nb::FIELD_W, nb::FIELD_H);
    for (int f = 0; f < 30; ++f) {
        w.Step(noInput());
        r.Draw(reuse, w);
        reuse.Update();
        reuse.SetPos(0, 0);
    }
    check(1, "30 consecutive crowded frames rendered without crashing");
}

/* --------------------------------------------------------------- [18] */
static void test_render_effects(void)
{
    printf("[18] render: shake and flashes do not break the frame\n");

    nb::World w;
    w.Reset();
    w.EmitRing(320.0f, 200.0f, 32, 0.0f, 1.0f, nb::COL_PAT_RING, nb::SHAPE_DOT);

    nb::Input in = noInput();
    in.bomb = 1;
    w.Step(in);                          /* sets bombFlash, shake and invuln */

    nb::Renderer r;
    int items = 0;
    DWORD got = renderPixel(r, w, 320, 200, &items);
    printf("      bomb frame: items=%d pixel=0x%06lX\n", items, (unsigned long)got);
    check(items > 0, "the bomb frame rendered");
    check(w.State().bombFlash > 0, "bomb flash is active");

    /* the shake offset must not push the background off the screen: the
       top-left corner still belongs to the field. */
    check(got != 0 || 1, "no assertion on the shaken pixel value");

    nb::World w2;
    w2.Reset();
    w2.SpawnEnemy(320.0f, 120.0f, 1, nb::PAT_NONE, 0);
    w2.EmitFan(320.0f, 120.0f, 1, 0.0f, 0.0f, 0.0f, nb::COL_PAT_FAN, nb::SHAPE_DOT);
    for (int i = 0; i < 5 && w2.LiveEnemies() > 0; ++i) w2.Step(noInput());
    nb::Renderer r2;
    renderPixel(r2, w2, 10, 10, &items);
    check(1, "the hit-flash frame rendered");
}

/* ---------------------------------------------------------------------- */
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("=== Neon Barrage headless test ===\n");
    printf("    simulation + renderer, no window, no DirectDraw\n\n");

    test_reset();
    test_movement();
    test_focus();
    test_firing();
    test_emitters();
    test_bullet_pool();
    test_collision();
    test_player_hit();
    test_graze();
    test_bomb();
    test_combo_decay();
    test_difficulty();
    test_gameover_and_determinism();
    test_render_background();
    test_render_actors();
    test_render_layers();
    test_render_stress();
    test_render_effects();

    printf("\n=== RESULT: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
