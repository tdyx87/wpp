/* ===========================================================================
 *  test_warfire.cpp -- headless tests for the Warfire prototype
 * ---------------------------------------------------------------------------
 *  Two layers are covered:
 *
 *    [1]..[17]  wf_core  -- the simulation, driven directly with no window.
 *    [18]..[21] wf_render -- the renderer is driven into an off-screen
 *               WCanvas and the resulting pixels are read back with
 *               canvas.Update() + canvas.BlitTo(), so the drawing code is
 *               verified without DirectDraw or a visible window.
 *
 *  ASCII only, so it compiles under the default (GBK) source code page.
 * ===========================================================================
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"
#include "wsgeo.h"

#include "wstimer.h"

#include "wf_core.h"
#include "wf_art.h"
#include "wf_render.h"

using namespace wf;

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *msg)
{
    if (cond) { ++g_pass; printf("  [PASS] %s\n", msg); }
    else      { ++g_fail; printf("  [FAIL] %s\n", msg); }
}

static Input noInput(void)
{
    Input in;
    in.dx = 0.0f; in.dy = 0.0f;
    in.attack = 0; in.dash = 0; in.spin = 0;
    return in;
}

static Input hold(float dx, float dy)
{
    Input in = noInput();
    in.dx = dx; in.dy = dy;
    return in;
}

/* ====================================================================== */
/*  simulation                                                            */
/* ====================================================================== */

/* ---------------------------------------------------------------- [1] */
static void test_reset(void)
{
    printf("[1] World::Reset\n");

    World w;
    const Player &p = w.Hero();
    const Stats  &st = w.State();

    check(p.alive == 1,        "hero is alive");
    check(p.hp == 120,         "starts with 120 hp");
    check(p.level == 1,        "level 1");
    check(p.gold == 0,         "no gold yet");
    check(p.atk > 0,           "has an attack value");
    check(st.kills == 0,       "no kills yet");
    check(st.wave == 1,        "wave 1");
    check(st.toSpawn > 0,      "wave 1 has monsters queued");
    check(w.LiveMonsters() == 0, "nothing spawned on frame 0");
    check(w.LiveDrops() == 0,  "no drops yet");
    check(w.State().gameover == 0, "not game over");

    /* the spawn plaza must be walkable, or the hero starts inside a rock */
    check(w.Blocked(WORLD_W * 0.5f, WORLD_H * 0.5f) == 0, "spawn plaza is clear");
    check((int)p.x == WORLD_W / 2 && (int)p.y == WORLD_H / 2, "hero starts centred");
}

/* ---------------------------------------------------------------- [2] */
static void test_movement(void)
{
    printf("[2] movement and acceleration\n");

    World w;
    const float x0 = w.Hero().x;

    /* one frame of input must not teleport: the hero accelerates */
    w.Step(hold(1.0f, 0.0f));
    float d1 = w.Hero().x - x0;
    check(d1 > 0.0f, "pressing right moves the hero right");
    check(d1 < 2.5f, "one frame is a nudge, not a jump");

    /* speed keeps climbing for a while, then saturates */
    float prev = d1;
    for (int i = 0; i < 20; ++i) {
        float before = w.Hero().x;
        w.Step(hold(1.0f, 0.0f));
        float d = w.Hero().x - before;
        if (d > prev) prev = d;
    }
    printf("      first step %.2f px, top speed %.2f px/frame\n", d1, prev);
    check(prev > d1 * 1.5f, "the hero accelerates over several frames");
    check(prev <= 5.5f,     "top speed is capped");

    /* releasing the stick brings it to a stop, not an instant halt */
    float xr = w.Hero().x;
    w.Step(noInput());
    float coast = w.Hero().x - xr;
    check(coast > 0.0f, "the hero coasts after the key is released");

    for (int i = 0; i < 40; ++i) w.Step(noInput());
    check(w.Hero().vx == 0.0f || w.Hero().vx < 0.1f, "it does come to rest");
}

/* ---------------------------------------------------------------- [3] */
static void test_tiles(void)
{
    printf("[3] terrain\n");

    World w;

    /* the map must contain something to bump into */
    int rocks = 0, grass = 0;
    for (int ty = 0; ty < MAP_H; ++ty)
        for (int tx = 0; tx < MAP_W; ++tx) {
            int t = w.TileAt(tx, ty);
            if (t == TILE_ROCK || t == TILE_WATER || t == TILE_TREE) ++rocks;
            if (t == TILE_GRASS) ++grass;
        }
    printf("      map: %d blocking tiles, %d grass tiles\n", rocks, grass);
    check(rocks > 100, "the map has obstacles");
    check(grass > 100, "the map has open ground");

    /* TileAt is clamped: outside the map is solid */
    check(w.TileAt(-1, 0) == TILE_ROCK, "off-map reads as solid");
    check(w.TileAt(MAP_W, 0) == TILE_ROCK, "off-map reads as solid (right)");

    /* a known blocking tile must report Blocked() */
    int bx = -1, by = -1;
    for (int ty = 0; ty < MAP_H && bx < 0; ++ty)
        for (int tx = 0; tx < MAP_W; ++tx) {
            int t = w.TileAt(tx, ty);
            if (t == TILE_ROCK || t == TILE_WATER || t == TILE_TREE) { bx = tx; by = ty; break; }
        }
    if (bx >= 0)
        check(w.Blocked(bx * TILE_SIZE + 16.0f, by * TILE_SIZE + 16.0f) == 1,
              "Blocked() agrees with the tile type");

    /* and walking into it must not pass through */
    World w2;
    if (bx >= 0) {
        /* aim the hero at the obstacle by walking in its direction for a while */
        float tx = bx * TILE_SIZE + 16.0f;
        float ty = by * TILE_SIZE + 16.0f;
        float dx = tx - w2.Hero().x, dy = ty - w2.Hero().y;
        float len = (dx * dx + dy * dy);
        if (len > 1.0f) {
            float d = (float)((dx > 0) - (dx < 0));
            float e = (float)((dy > 0) - (dy < 0));
            for (int i = 0; i < 600; ++i) w2.Step(hold(d, e));
        }
        check(w2.Blocked(w2.Hero().x, w2.Hero().y) == 0,
              "the hero never ends up inside a blocking tile");
    }

    /* the camera stays inside the world */
    check(w.CameraX() >= 0.0f && w.CameraX() <= (float)(WORLD_W - FIELD_W),
          "camera x is clamped");
    check(w.CameraY() >= 0.0f && w.CameraY() <= (float)(WORLD_H - FIELD_H),
          "camera y is clamped");
}

/* ---------------------------------------------------------------- [4] */
static void test_attack_combo(void)
{
    printf("[4] attack state machine\n");

    World w;
    Input atk = noInput();
    atk.attack = 1;

    check(w.Hero().attackPhase == 0, "idle to start with");

    w.Step(atk);
    check(w.Hero().attackPhase == 1, "pressing attack starts the wind-up");
    check(w.Hero().comboStep == 1,   "it is the first hit of the combo");

    /* the wind-up must last more than one frame, or there is no telegraph */
    int windup = 1;
    while (w.Hero().attackPhase == 1 && windup < 60) { w.Step(atk); ++windup; }
    printf("      wind-up lasted %d frames\n", windup);
    check(windup >= 4, "the wind-up is long enough to read");
    check(w.Hero().attackPhase == 2, "then it goes active");

    /* chaining: keep holding and the combo must advance to 2 and 3 */
    int seen[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 400; ++i) {
        w.Step(atk);
        int s = w.Hero().comboStep;
        if (s >= 1 && s <= 3) seen[s] = 1;
    }
    check(seen[1] && seen[2] && seen[3], "holding attack chains all three hits");

    /* and it must wrap back to 1 rather than run off the end */
    int maxStep = 0;
    for (int i = 0; i < 400; ++i) {
        w.Step(atk);
        if (w.Hero().comboStep > maxStep) maxStep = w.Hero().comboStep;
    }
    check(maxStep <= 3, "the combo never exceeds three hits");

    /* letting go resets it */
    for (int i = 0; i < 200; ++i) w.Step(noInput());
    check(w.Hero().comboStep == 0, "the combo resets when you stop");
}

/* ---------------------------------------------------------------- [5] */
static void test_attack_hits(void)
{
    printf("[5] landing a hit\n");

    World w;
    const float px = w.Hero().x;
    const float py = w.Hero().y;

    w.SpawnMonster(MON_GRUNT, px, py + 40.0f);
    check(w.LiveMonsters() == 1, "a monster is in front of the hero");

    int hp0 = w.Monsters()[0].hp;

    Input atk = noInput();
    atk.attack = 1;

    /* wind-up is 6 frames, so the hit lands on the 7th step */
    for (int i = 0; i < 7; ++i) w.Step(atk);

    int hp1 = w.Monsters()[0].hp;
    printf("      monster hp %d -> %d\n", hp0, hp1);
    check(hp1 < hp0, "the swing connects");
    check(w.State().hitstop > 0 || hp1 < hp0, "the hit registers");

    /* one swing, one hit: the active frames must not re-apply the damage */
    for (int i = 0; i < 4; ++i) w.Step(atk);
    check(w.Monsters()[0].hp == hp1, "a single swing only damages once");

    /* the monster should have been knocked back and stunned */
    check(w.Monsters()[0].state == MS_STUN || w.Monsters()[0].knockX != 0.0f ||
          w.Monsters()[0].knockY != 0.0f || w.Monsters()[0].flash > 0,
          "the hit produced knock-back / hit-stun / a flash");

    /* out of reach: nothing happens */
    World w2;
    w2.SpawnMonster(MON_GRUNT, w2.Hero().x + 260.0f, w2.Hero().y);
    int far0 = w2.Monsters()[0].hp;
    for (int i = 0; i < 8; ++i) w2.Step(atk);
    check(w2.Monsters()[0].hp == far0, "swings do not reach across the screen");
}

/* ---------------------------------------------------------------- [6] */
static void test_kill_and_loot(void)
{
    printf("[6] kills, experience and loot\n");

    World w;
    const float px = w.Hero().x;
    const float py = w.Hero().y;

    w.SpawnMonster(MON_GRUNT, px, py + 40.0f);
    w.DamageMonster(0, 1000, 0, px, py);
    check(w.LiveMonsters() == 0, "a big hit kills it");
    check(w.State().kills == 1, "the kill counter advanced");
    check(w.Hero().exp > 0, "killing grants experience");
    check(w.LiveDrops() >= 1, "something dropped");

    /* a coin is worth picking up */
    int gold = w.Hero().gold;
    for (int i = 0; i < 200; ++i) w.Step(noInput());
    printf("      gold %d -> %d\n", gold, w.Hero().gold);
    check(w.Hero().gold > gold, "walking over the drop collects it");

    /* a boss is worth far more */
    World w2;
    w2.SpawnMonster(MON_BOSS, w2.Hero().x, w2.Hero().y + 40.0f);
    int exp0 = w2.Hero().exp;
    w2.DamageMonster(0, 100000, 0, w2.Hero().x, w2.Hero().y);
    check(w2.Hero().exp > exp0, "bosses give experience");
    check(w2.State().kills == 1, "and count as a kill");
}

/* ---------------------------------------------------------------- [7] */
static void test_level_up(void)
{
    printf("[7] levelling up\n");

    World w;
    int level0 = w.Hero().level;
    int atk0   = w.Hero().atk;
    int hp0    = w.Hero().maxhp;

    /* farm enough experience by killing a pile of monsters outright */
    for (int n = 0; n < 40 && w.Hero().level == level0; ++n) {
        w.SpawnMonster(MON_BRUTE, w.Hero().x, w.Hero().y + 40.0f);
        int idx = -1;
        for (int i = 0; i < MAX_MONSTERS; ++i)
            if (w.Monsters()[i].alive) { idx = i; break; }
        if (idx >= 0) w.DamageMonster(idx, 100000, 0, w.Hero().x, w.Hero().y);
    }

    printf("      level %d -> %d, atk %d -> %d, maxhp %d -> %d\n",
           level0, w.Hero().level, atk0, w.Hero().atk, hp0, w.Hero().maxhp);
    check(w.Hero().level > level0, "the hero levels up");
    check(w.Hero().atk > atk0,     "levelling raises attack");
    check(w.Hero().maxhp > hp0,    "levelling raises max hp");
    check(w.Hero().hp == w.Hero().maxhp, "and heals to full");
}

/* ---------------------------------------------------------------- [8] */
static void test_taking_damage(void)
{
    printf("[8] taking damage\n");

    World w;
    int hp0 = w.Hero().hp;

    w.DamageHero(15);
    check(w.Hero().hp == hp0 - 15, "damage is subtracted");
    check(w.Hero().invuln > 0,     "being hit grants invulnerability");
    check(w.State().hurtFlash > 0, "and a screen flash");

    /* invulnerability must actually block the next hit */
    int hp1 = w.Hero().hp;
    w.DamageHero(15);
    check(w.Hero().hp == hp1, "a second hit is blocked while invulnerable");

    /* the swing is interrupted -- on a fresh hero, because the one above is
       still inside its invulnerability window and DamageHero() would no-op */
    World w3;
    Input atk = noInput();
    atk.attack = 1;
    for (int i = 0; i < 4; ++i) w3.Step(atk);
    check(w3.Hero().attackPhase != 0, "the swing is running before the hit");
    w3.DamageHero(5);
    check(w3.Hero().attackPhase == 0, "being hit cancels the swing");

    /* death */
    World w2;
    for (int i = 0; i < 40 && !w2.State().gameover; ++i) {
        w2.DamageHero(30);
        for (int k = 0; k < 45 && w2.Hero().invuln > 0; ++k) w2.Step(noInput());
    }
    check(w2.Hero().hp == 0,        "hp floors at zero");
    check(w2.Hero().alive == 0,     "the hero dies");
    check(w2.State().gameover == 1, "game over is flagged");

    int frame = w2.State().frame;
    for (int i = 0; i < 30; ++i) w2.Step(noInput());
    check(w2.State().frame == frame, "Step is inert after game over");

    w2.Reset();
    check(w2.State().gameover == 0 && w2.Hero().hp == 120, "Reset restarts");
}

/* ---------------------------------------------------------------- [9] */
static void test_dash_and_spin(void)
{
    printf("[9] dash and whirlwind\n");

    World w;
    const float x0 = w.Hero().x;

    Input d = noInput();
    d.dx = 1.0f;
    d.dash = 1;
    w.Step(d);

    check(w.Hero().dashTimer > 0, "dashing sets a dash timer");
    check(w.Hero().invuln > 0,    "and grants i-frames");

    for (int i = 0; i < 10; ++i) w.Step(hold(1.0f, 0.0f));
    float travelled = w.Hero().x - x0;
    printf("      dash covered %.1f px in 11 frames\n", travelled);
    check(travelled > 40.0f, "the dash covers real ground fast");

    /* the dash has a cooldown */
    World w2;
    Input d2 = noInput();
    d2.dash = 1;
    w2.Step(d2);
    check(w2.Hero().dashCd > 0, "the dash goes on cooldown");
    w2.Step(d2);
    check(w2.Hero().dashCd > 0, "spamming it does not reset the cooldown");

    /* whirlwind costs rage and clears the area */
    World w3;
    const float px = w3.Hero().x;
    const float py = w3.Hero().y;
    for (int i = 0; i < 4; ++i)
        w3.SpawnMonster(MON_GRUNT, px + 20.0f * (i + 1), py);

    int mp0 = w3.Hero().mp;
    Input s = noInput();
    s.spin = 1;
    w3.Step(s);
    check(w3.Hero().spinTimer > 0, "whirlwind starts");
    check(w3.Hero().mp == mp0 - 30, "it costs 30 rage");
    check(w3.Hero().spinCd > 0,    "and goes on a long cooldown");

    int hpBefore[4];
    for (int i = 0; i < 4; ++i) hpBefore[i] = w3.Monsters()[i].hp;
    for (int i = 0; i < 34; ++i) w3.Step(noInput());

    int damaged = 0;
    for (int i = 0; i < 4; ++i)
        if (w3.Monsters()[i].hp < hpBefore[i] || !w3.Monsters()[i].alive) ++damaged;
    printf("      whirlwind hit %d of 4 monsters\n", damaged);
    check(damaged >= 3, "whirlwind sweeps everything nearby");

    /* without enough rage it does nothing */
    World w4;
    Input q = noInput();
    q.spin = 1;
    for (int round = 0; round < 10; ++round) {
        w4.Step(q);
        for (int i = 0; i < 320; ++i) w4.Step(noInput());
        if (w4.Hero().mp < 30) break;
    }
    printf("      rage after spinning out: %d\n", w4.Hero().mp);
    check(w4.Hero().mp < 30, "whirlwind drains the rage bar");

    int mpLow = w4.Hero().mp;
    w4.Step(q);
    check(w4.Hero().mp == mpLow, "not enough rage means no whirlwind");
}

/* --------------------------------------------------------------- [10] */
/* Places a monster on open ground at roughly `dist` px from the hero.  The
   centre of the map is a cleared plaza, but anything past ~150 px can land
   inside a rock, and a monster wedged in a rock measures terrain, not AI. */
static void spawnOpen(World &w, int kind, float ang, float dist)
{
    float x, y;
    if (!w.FreeSpot(w.Hero().x, w.Hero().y, ang, dist, x, y)) {
        x = w.Hero().x + cosf(ang) * dist;
        y = w.Hero().y + sinf(ang) * dist;
    }
    w.SpawnMonster(kind, x, y);
}

static float distToHero(const World &w, int idx)
{
    float dx = w.Monsters()[idx].x - w.Hero().x;
    float dy = w.Monsters()[idx].y - w.Hero().y;
    return (float)sqrt(dx * dx + dy * dy);
}

static void test_monster_ai(void)
{
    printf("[10] monster AI\n");

    /* a grunt should close the distance */
    World w;
    spawnOpen(w, MON_GRUNT, 0.0f, 220.0f);
    float d0 = distToHero(w, 0);

    for (int i = 0; i < 90; ++i) w.Step(noInput());
    float d1 = distToHero(w, 0);
    printf("      grunt closed from %.0f to %.0f px\n", d0, d1);
    check(d1 < d0 - 20.0f, "a grunt chases the hero down");

    /* a grunt that gets close must wind up before it hits, not instantly */
    World w2;
    spawnOpen(w2, MON_GRUNT, 0.0f, 60.0f);
    int sawWindup = 0;
    for (int i = 0; i < 200 && !sawWindup; ++i) {
        w2.Step(noInput());
        if (w2.Monsters()[0].state == MS_WINDUP) sawWindup = 1;
    }
    check(sawWindup, "the grunt telegraphs its blow with a wind-up");

    /* An archer keeps its range and shoots.  It is placed due east of the
       hero at 100 px -- well inside the cleared plaza -- so the arrow has a
       clear lane and the test measures the archer, not the terrain. */
    World w3;
    spawnOpen(w3, MON_ARCHER, 0.0f, 100.0f);
    int sawArrow = 0, hit = 0;
    int hp0 = w3.Hero().hp;
    for (int i = 0; i < 600 && !hit; ++i) {
        w3.Step(noInput());
        if (w3.LiveArrows() > 0) sawArrow = 1;
        if (w3.Hero().hp < hp0) hit = 1;
    }
    printf("      archer fired after %s\n", sawArrow ? "a while" : "never");
    check(sawArrow, "the archer shoots");
    check(hit, "and its arrow reaches the hero");
    check(distToHero(w3, 0) > 120.0f, "the archer backs off instead of charging");

    /* idle monsters far away must stay idle */
    World w4;
    spawnOpen(w4, MON_GRUNT, 0.0f, 700.0f);
    w4.Step(noInput());
    check(w4.Monsters()[0].state == MS_IDLE, "a distant monster stays idle");
}

/* --------------------------------------------------------------- [11] */
static void test_waves(void)
{
    printf("[11] wave progression\n");

    World w;
    check(w.State().wave == 1, "starts on wave 1");
    int first = w.State().toSpawn;
    check(first > 0, "wave 1 queues monsters");

    /* Drain the spawn queue, killing each arrival on sight.  Standing still
       for the 1000+ frames a wave takes to trickle in would just get the
       hero killed, and a dead hero makes Step() a no-op -- which is what
       this test is not about. */
    int guard = 0;
    while (w.State().toSpawn > 0 && guard < 6000) {
        ++guard;
        w.Step(noInput());
        for (int i = 0; i < MAX_MONSTERS; ++i)
            if (w.Monsters()[i].alive)
                w.DamageMonster(i, 100000, 0, w.Hero().x, w.Hero().y);
    }
    printf("      wave 1 spawned %d monsters\n", w.State().monstersThisWave);
    check(w.State().toSpawn == 0, "the wave finishes spawning");
    check(!w.State().gameover, "the hero is still standing");
    check(w.State().monstersThisWave == first, "and spawned exactly what it queued");

    /* the field is already empty -- the next step must roll the next wave.
       Step() a few times because a kill sets hit-stop, and a hit-stop frame
       returns before stepWave() ever runs. */
    check(w.LiveMonsters() == 0, "the field is clear");
    for (int i = 0; i < 10 && w.State().wave == 1; ++i) w.Step(noInput());
    check(w.State().wave == 2, "clearing the wave advances the counter");
    check(w.State().banner > 0, "and shows a banner");
    check(w.State().toSpawn > 0, "wave 2 queues monsters");
    check(w.State().toSpawn > first, "later waves are bigger");

    /* the boss rides in on every fifth wave */
    World w2;
    w2.StartWave(5);
    for (int i = 0; i < 40 * 60; ++i) {
        w2.Step(noInput());
        for (int k = 0; k < MAX_MONSTERS; ++k)
            if (w2.Monsters()[k].alive && w2.Monsters()[k].kind != MON_BOSS)
                w2.DamageMonster(k, 100000, 0, w2.Hero().x, w2.Hero().y);
    }
    int boss = 0;
    for (int i = 0; i < MAX_MONSTERS; ++i)
        if (w2.Monsters()[i].alive && w2.Monsters()[i].kind == MON_BOSS) boss = 1;
    check(boss, "wave 5 sends a boss");
}

/* --------------------------------------------------------------- [12] */
static void test_determinism(void)
{
    printf("[12] determinism\n");

    World a, b;
    for (int i = 0; i < 600; ++i) {
        Input in = noInput();
        in.dx = (float)(((i / 23) % 3) - 1);
        in.dy = (float)(((i / 41) % 3) - 1);
        in.attack = ((i % 11) < 7) ? 1 : 0;
        in.dash = ((i % 97) == 0) ? 1 : 0;
        in.spin = ((i % 211) == 0) ? 1 : 0;
        a.Step(in);
        b.Step(in);
    }

    check(a.Hero().x == b.Hero().x && a.Hero().y == b.Hero().y,
          "identical inputs give an identical hero position");
    check(a.Hero().hp == b.Hero().hp, "identical hp");
    check(a.Hero().gold == b.Hero().gold, "identical gold");
    check(a.State().kills == b.State().kills, "identical kills");
    check(a.State().wave == b.State().wave, "identical wave");
    check(a.LiveMonsters() == b.LiveMonsters(), "identical monster count");
    printf("      after 600 frames: hp=%d gold=%d kills=%d wave=%d monsters=%d\n",
           a.Hero().hp, a.Hero().gold, a.State().kills,
           a.State().wave, a.LiveMonsters());
}

/* --------------------------------------------------------------- [13] */
static void test_pools(void)
{
    printf("[13] pools and limits\n");

    World w;

    /* overfill the monster pool: it must stop, not overrun */
    for (int i = 0; i < MAX_MONSTERS * 3; ++i)
        w.SpawnMonster(MON_GRUNT, w.Hero().x + 300.0f, w.Hero().y);
    printf("      spawned up to %d monsters (pool %d)\n",
           w.LiveMonsters(), MAX_MONSTERS);
    check(w.LiveMonsters() == MAX_MONSTERS, "the monster pool caps out");

    /* and effects too */
    for (int i = 0; i < MAX_FX * 3; ++i)
        w.AddFx(FX_SPARK, w.Hero().x, w.Hero().y, 0, 0, 0xFFFFFF);
    check(w.LiveFx() == MAX_FX, "the effect pool caps out");

    /* the world survives a long run with a full screen of monsters */
    for (int i = 0; i < 120; ++i) w.Step(noInput());
    check(1, "120 frames with a full pool did not crash");
}

/* --------------------------------------------------------------- [14] */
static void test_hero_never_inside_walls(void)
{
    printf("[14] the hero cannot walk into walls\n");

    /* drive the hero hard in every direction and check it never ends up
       inside a blocking tile */
    World w;
    int bad = 0;
    for (int phase = 0; phase < 4; ++phase) {
        float dx = (float)((phase == 0) - (phase == 2));
        float dy = (float)((phase == 1) - (phase == 3));
        for (int i = 0; i < 900; ++i) {
            w.Step(hold(dx, dy));
            if (w.Blocked(w.Hero().x, w.Hero().y)) { ++bad; break; }
        }
    }
    printf("      violations: %d\n", bad);
    check(bad == 0, "the hero stayed out of blocking tiles");
    check(w.Hero().x > 0.0f && w.Hero().x < (float)WORLD_W, "and inside the world");
    check(w.Hero().y > 0.0f && w.Hero().y < (float)WORLD_H, "and inside the world (y)");
}

/* ====================================================================== */
/*  renderer                                                              */
/* ====================================================================== */

static Atlas *g_atlas = 0;

static DWORD renderPixel(Renderer &r, const World &w, int px, int py, int *items)
{
    WCanvas cv(FIELD_W, FIELD_H);
    r.Draw(cv, w, *g_atlas);
    if (items) *items = r.ItemCount();

    cv.Update();
    cv.SetPos(0, 0);

    WBitmap out(FIELD_W, FIELD_H);
    out.Clear(WPixel(0, 0, 0));
    cv.BlitTo(out);

    return (DWORD)out[py][px];
}

static int renderFrame(Renderer &r, const World &w, WBitmap &out)
{
    WCanvas cv(FIELD_W, FIELD_H);
    r.Draw(cv, w, *g_atlas);
    cv.Update();
    cv.SetPos(0, 0);
    out.Clear(WPixel(0, 0, 0));
    cv.BlitTo(out);
    return r.ItemCount();
}

/* Dumps a frame as a PPM so it can be eyeballed outside the test run. */
static void dumpFrame(WBitmap &out, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;

    fprintf(f, "P6\n%d %d\n255\n", FIELD_W, FIELD_H);
    for (int y = 0; y < FIELD_H; ++y)
        for (int x = 0; x < FIELD_W; ++x) {
            DWORD q = (DWORD)out[y][x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)((q >> 16) & 0xFF);
            rgb[1] = (unsigned char)((q >> 8) & 0xFF);
            rgb[2] = (unsigned char)(q & 0xFF);
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("      frame dumped to %s\n", path);
}

/* --------------------------------------------------------------- [15] */
static void test_render_terrain(void)
{
    printf("[15] render: terrain\n");

    World w;
    Renderer r;
    WBitmap out(FIELD_W, FIELD_H);

    int items = renderFrame(r, w, out);
    printf("      %d draw calls\n", items);
    check(items > 300, "a full screen of tiles is submitted");

    /* every pixel must be painted: the terrain has no holes */
    int blank = 0, shown = 0;
    for (int y = 0; y < FIELD_H; y += 7)
        for (int x = 0; x < FIELD_W; x += 7)
            if ((DWORD)out[y][x] == 0) {
                if (shown < 8) { printf("      blank at (%d,%d)\n", x, y); ++shown; }
                ++blank;
            }
    printf("      blank pixels: %d\n", blank);
    check(blank == 0, "the terrain covers the whole viewport");

    if (const char *p = getenv("WF_DUMP")) dumpFrame(out, p);
}

/* --------------------------------------------------------------- [16] */
static void test_render_actors(void)
{
    printf("[16] render: actors\n");

    World w;
    Renderer r;

    /* the hero is somewhere near the middle of the screen */
    int px = FIELD_W / 2;
    int py = FIELD_H / 2;

    /* scan a box around the hero and count pixels that are not terrain */
    WBitmap out(FIELD_W, FIELD_H);
    renderFrame(r, w, out);

    int nonTerrain = 0;
    for (int y = py - 24; y < py + 12; ++y)
        for (int x = px - 16; x < px + 16; ++x) {
            DWORD p = (DWORD)out[y][x];
            if (p == 0) continue;
            /* terrain greens/browns/blues are dark; the hero is bright */
            unsigned r0 = (p >> 16) & 0xFF, g0 = (p >> 8) & 0xFF, b0 = p & 0xFF;
            if (r0 > 150 && g0 > 150 && b0 > 150) ++nonTerrain;
        }
    printf("      bright hero pixels: %d\n", nonTerrain);
    check(nonTerrain > 20, "the hero is drawn at the screen centre");

    /* a monster must be drawn where the world says it is */
    World w2;
    w2.SpawnMonster(MON_BRUTE, w2.Hero().x + 120.0f, w2.Hero().y - 80.0f);
    Renderer r2;
    int mx = FIELD_W / 2 + 120;
    int my = FIELD_H / 2 - 80;

    WBitmap out2(FIELD_W, FIELD_H);
    renderFrame(r2, w2, out2);

    int reds = 0;
    for (int y = my - 30; y < my + 10; ++y)
        for (int x = mx - 20; x < mx + 20; ++x) {
            DWORD p = (DWORD)out2[y][x];
            unsigned r0 = (p >> 16) & 0xFF, g0 = (p >> 8) & 0xFF, b0 = p & 0xFF;
            if (r0 > 120 && g0 < 110 && b0 < 110) ++reds;   /* the brute is red */
        }
    printf("      brute-red pixels: %d\n", reds);
    check(reds > 30, "the monster is drawn where it stands");

    /* one of each kind in a row -- also serves as an art sheet that can be
       dumped with WF_DUMP_ACTORS */
    World w3;
    static const int KINDS[MON_KIND_COUNT] = { MON_GRUNT, MON_ARCHER, MON_BRUTE, MON_BOSS };
    for (int i = 0; i < MON_KIND_COUNT; ++i)
        w3.SpawnMonster(KINDS[i], w3.Hero().x - 140.0f + i * 95.0f, w3.Hero().y - 70.0f);
    w3.AddFx(FX_TEXT, w3.Hero().x + 60.0f, w3.Hero().y - 90.0f, 42, 0, COL_DMG);
    w3.AddFx(FX_TEXT, w3.Hero().x + 150.0f, w3.Hero().y - 110.0f, 128, 1, COL_DMG_CRIT);

    Renderer r3;
    WBitmap out3(FIELD_W, FIELD_H);
    renderFrame(r3, w3, out3);
    check(w3.LiveMonsters() == MON_KIND_COUNT, "all four kinds can stand together");

    if (const char *p = getenv("WF_DUMP_ACTORS")) dumpFrame(out3, p);
}

/* --------------------------------------------------------------- [17] */
static void test_render_layers(void)
{
    printf("[17] render: layer order\n");

    /* the HUD is submitted first, so it wins over anything under it */
    World w;
    Renderer r;
    WBitmap out(FIELD_W, FIELD_H);
    renderFrame(r, w, out);

    int hudPixels = 0;
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 240; ++x) {
            DWORD p = (DWORD)out[y][x];
            unsigned r0 = (p >> 16) & 0xFF, g0 = (p >> 8) & 0xFF, b0 = p & 0xFF;
            /* the HUD text is near-white / gold on dark panel colours */
            if (r0 > 200 && g0 > 200 && b0 > 200) ++hudPixels;
        }
    printf("      HUD pixels in the top-left corner: %d\n", hudPixels);
    check(hudPixels > 40, "the HUD is drawn on top of the terrain");

    /* a monster standing north of the hero is further away, so the hero wins
       the overlap: larger y is submitted first and lands on top */
    World w2;
    float hx = w2.Hero().x, hy = w2.Hero().y;
    w2.SpawnMonster(MON_BRUTE, hx, hy - 10.0f);

    Renderer r2;
    WBitmap out2(FIELD_W, FIELD_H);
    renderFrame(r2, w2, out2);

    /* the hero's own pixels must still be there where the brute overlaps */
    int heroPixels = 0;
    int px = FIELD_W / 2, py = FIELD_H / 2;
    for (int y = py - 20; y < py + 10; ++y)
        for (int x = px - 12; x < px + 12; ++x) {
            DWORD p = (DWORD)out2[y][x];
            unsigned r0 = (p >> 16) & 0xFF, g0 = (p >> 8) & 0xFF, b0 = p & 0xFF;
            if (r0 > 150 && g0 > 150 && b0 > 150) ++heroPixels;
        }
    printf("      hero pixels with a brute on top of him: %d\n", heroPixels);
    check(heroPixels > 15, "the nearer actor is drawn over the further one");
}

/* --------------------------------------------------------------- [18] */
static void test_render_effects(void)
{
    printf("[18] render: effects\n");

    World w;
    w.AddFx(FX_TEXT, w.Hero().x, w.Hero().y - 20.0f, 1234, 0, COL_DMG);
    w.AddFx(FX_SPARK, w.Hero().x, w.Hero().y - 10.0f, 0, 0, COL_BLADE);
    w.AddFx(FX_RING, w.Hero().x, w.Hero().y, 0, 0, COL_BLADE);
    w.AddFx(FX_SLASH, w.Hero().x, w.Hero().y, 0, 0, COL_BLADE);

    Renderer r;
    int items = 0;
    renderPixel(r, w, FIELD_W / 2, FIELD_H / 2, &items);
    printf("      effect frame: %d draw calls\n", items);
    check(items > 300, "effects are drawn without breaking the frame");

    /* the damage number must add pixels above the hero */
    World w2;
    Renderer r2;
    WBitmap before(FIELD_W, FIELD_H);
    renderFrame(r2, w2, before);

    w2.AddFx(FX_TEXT, w2.Hero().x, w2.Hero().y - 30.0f, 999, 1, COL_DMG_CRIT);
    Renderer r3;
    WBitmap after(FIELD_W, FIELD_H);
    renderFrame(r3, w2, after);

    int diff = 0;
    for (int y = FIELD_H / 2 - 60; y < FIELD_H / 2 - 10; ++y)
        for (int x = FIELD_W / 2 - 60; x < FIELD_W / 2 + 60; ++x)
            if ((DWORD)before[y][x] != (DWORD)after[y][x]) ++diff;
    printf("      damage-number pixels: %d\n", diff);
    check(diff > 30, "the floating damage number is visible");

    /* the shake must actually shift the world */
    World w3;
    Renderer r4;
    WBitmap still(FIELD_W, FIELD_H);
    renderFrame(r4, w3, still);

    w3.DamageHero(1);                    /* triggers shake */
    check(w3.State().shake > 0, "taking a hit shakes the screen");
}

/* --------------------------------------------------------------- [19] */
static void test_render_stress(void)
{
    printf("[19] render: stress\n");

    World w;
    for (int i = 0; i < 90; ++i)
        w.SpawnMonster(MON_GRUNT, w.Hero().x + (float)((i % 10) * 70 - 350),
                       w.Hero().y + (float)((i / 10) * 60 - 270));
    for (int i = 0; i < 150; ++i)
        w.AddFx(FX_SPARK, w.Hero().x, w.Hero().y, 0, 0, 0xFFFFFF);

    printf("      %d monsters, %d effects\n", w.LiveMonsters(), w.LiveFx());

    Renderer r;
    WBitmap out(FIELD_W, FIELD_H);
    int items = renderFrame(r, w, out);
    printf("      display list = %d draw calls\n", items);
    check(items > 0, "a crowded frame still produced a display list");
    check(items < 4000, "the display list stays inside its budget");

    if (const char *p = getenv("WF_DUMP_STRESS")) dumpFrame(out, p);

    /* many frames in a row must not leak or drift */
    WCanvas cv(FIELD_W, FIELD_H);
    for (int f = 0; f < 40; ++f) {
        w.Step(noInput());
        r.Draw(cv, w, *g_atlas);
        cv.Update();
        cv.SetPos(0, 0);
    }
    check(1, "40 consecutive crowded frames rendered without crashing");
}

/* --------------------------------------------------------------- [20] */
/*  frame budget                                                           */
/* ---------------------------------------------------------------------------
 *  "it feels laggy" is not a bug report.  This measures the CPU cost of one
 *  frame -- renderer display-list build and canvas.Update() separately -- so
 *  the budget can be argued about with numbers.  A 60 Hz frame is 16.6 ms and
 *  the whole thing has to fit in there with room for DirectDraw.
 * ------------------------------------------------------------------------ */

static double nowMs(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

/* Measures the real per-frame CPU cost: the same four calls the game makes
   in Idle().  A 60 Hz frame is 16.67 ms and this has to fit inside it with
   room to spare, otherwise the frame limiter has nothing to limit. */
static void budgetFrame(const char *label, World &w, int frames,
                        double *drawMs, double *updateMs, int *calls)
{
    Renderer r;
    WCanvas  cv(FIELD_W, FIELD_H);
    cv.Reset(0);                            /* no limiter: raw cost only */

    for (int i = 0; i < 8; ++i) {           /* warm the caches / pools */
        r.Draw(cv, w, *g_atlas);
        cv.Update();
        cv.NextFrame();
    }

    /* display-list build only */
    double t0 = nowMs();
    for (int i = 0; i < frames; ++i) {
        r.Draw(cv, w, *g_atlas);
        cv.NextFrame();                     /* keep the block state sane */
    }
    double t1 = nowMs();

    /* the whole frame, exactly as the game runs it */
    double t2 = nowMs();
    for (int i = 0; i < frames; ++i) {
        r.Draw(cv, w, *g_atlas);
        cv.Update();
        cv.NextFrame();
    }
    double t3 = nowMs();

    *drawMs   = (t1 - t0) / frames;
    *updateMs = (t3 - t2) / frames - *drawMs;
    *calls    = r.ItemCount();

    printf("      %-12s list %6.3f ms | update %6.3f ms | frame %6.3f ms | %d calls\n",
           label, *drawMs, *updateMs, *drawMs + *updateMs, *calls);
}

static void test_frame_budget(void)
{
    printf("[20] frame budget\n");

    double d1, u1, d2, u2;
    int c1, c2;

    /* --- a quiet frame: terrain only ---------------------------------- */
    World quiet;
    budgetFrame("quiet", quiet, 60, &d1, &u1, &c1);

    /* --- a crowded frame: 90 monsters + 150 effects -------------------- */
    World busy;
    for (int i = 0; i < 90; ++i)
        busy.SpawnMonster(MON_GRUNT, busy.Hero().x + (float)((i % 10) * 70 - 350),
                          busy.Hero().y + (float)((i / 10) * 60 - 270));
    for (int i = 0; i < 150; ++i)
        busy.AddFx(FX_SPARK, busy.Hero().x, busy.Hero().y, 0, 0, 0xFFFFFF);
    budgetFrame("crowded", busy, 60, &d2, &u2, &c2);

    double quietMs   = d1 + u1;
    double crowdedMs = d2 + u2;

    printf("      budget for 60 Hz is 16.67 ms/frame\n");
    check(quietMs   < 16.67, "a terrain-only frame fits in a 60 Hz budget");
    check(crowdedMs < 33.00, "a crowded frame still fits in a 30 Hz budget");

    /* The display list is dominated by the terrain: one call per visible
       tile.  If this climbs much past a screenful the culling is broken. */
    check(c1 > 0 && c1 < 1200, "the quiet display list stays near one screenful");
}

/* --------------------------------------------------------------- [21] */
/*  WTimer fidelity                                                       */
/* ---------------------------------------------------------------------------
 *  The frame limiter of round 8 was broken because the library clock and the
 *  wall clock disagreed.  WCanvas::SleepFrame() reasons entirely in
 *  WTimer::GetTime() units ("0.1 ms"), so a clock that runs at the wrong
 *  speed makes the limiter sleep twice or not at all.  Measure it.
 * ------------------------------------------------------------------------ */

static void test_timer_fidelity(void)
{
    printf("[21] WTimer fidelity\n");

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);

    WTimer t;
    t.Reset();
    LARGE_INTEGER q0, q1;

    /* Sleep(100) is deliberately NOT the reference: the OS rounds it up to
       whole timer quanta.  The performance counter is the ground truth. */
    for (int i = 0; i < 3; ++i) {
        t.Reset();
        QueryPerformanceCounter(&q0);
        Sleep(120);
        QueryPerformanceCounter(&q1);

        double wall = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)f.QuadPart;
        DWORD   d    = t.GetTime();
        double  self = d / 10.0;

        printf("      120 ms sleep : wall %7.2f ms, WTimer %7.2f ms, error %+5.2f%%\n",
               wall, self, (self - wall) / wall * 100.0);

        check(fabs(self - wall) < wall * 0.05,
              "WTimer's 0.1 ms units track the wall clock");
    }

    /* Reset(0) must really zero the clock -- the limiter depends on it */
    t.Reset(0);
    DWORD z = t.GetTime();
    printf("      right after Reset(0) -> %u units\n", z);
    check(z < 50, "Reset(0) restarts the clock at zero");

    /* ...and a second reading must be strictly later, never earlier */
    Sleep(10);
    DWORD z2 = t.GetTime();
    check(z2 > z, "the clock never runs backwards");
}

/* ----------------------------------------------------------------------
 *  Regression test for the "moving the character flickers" bug.
 *
 *  WCanvas::Draw() allocates its per-block link nodes from a pool that
 *  CANVAS_LINKPAGE::clearobj() resets to the SAME addresses every frame.
 *  WCanvas::NextFrame() used to carry the previous obj pointer into `last`,
 *  so a block whose link-list head landed on the same slot two frames
 *  running read last==obj and was skipped -- it kept showing the previous
 *  frame.  While the camera is still that is invisible; while it scrolls the
 *  terrain blocks freeze on stale pixels and the split back-buffers blit a
 *  half-new / half-old frame, so the screen flickers.
 *
 *  This test pins a sprite inside ONE 64x32 block and nudges it 6 px.  The
 *  block's link head address is identical in both frames, so without the fix
 *  the whole frame is pixel-identical (the sprite never repaints).  With the
 *  fix every drawn block is dirty each frame, so the nudged sprite repaints
 *  and the two frames differ.
 * ---------------------------------------------------------------------- */
static void test_canvas_dirty_scroll(void)
{
    printf("[22] canvas repaints a block when its content moves\n");

    WCanvas cv(FIELD_W, FIELD_H);
    cv.Reset(0);                       /* no frame limiter: raw cost only */

    WBitmap spr;
    spr.Create(16, 16);
    spr.Clear(WPixel(255, 0, 0));      /* a solid red sprite -- BLIT_COPY */

    WBitmap shot;
    shot.Create(FIELD_W, FIELD_H);

    /* frame A: sprite sits fully inside one 64x32 block (col 3, row 9) */
    cv.SetPos(224, 304);
    cv.Draw(spr, BLIT_COPY);
    cv.Update();
    cv.BlitTo(shot);
    unsigned short *a = new unsigned short[FIELD_W * FIELD_H];
    for (int y = 0; y < FIELD_H; ++y)
        for (int x = 0; x < FIELD_W; ++x)
            a[y * FIELD_W + x] = (unsigned short)shot[y][x].color;

    cv.NextFrame();

    /* frame B: sprite nudged 6 px, still inside the same block */
    cv.SetPos(230, 304);
    cv.Draw(spr, BLIT_COPY);
    cv.Update();
    cv.BlitTo(shot);
    unsigned short *b = new unsigned short[FIELD_W * FIELD_H];
    for (int y = 0; y < FIELD_H; ++y)
        for (int x = 0; x < FIELD_W; ++x)
            b[y * FIELD_W + x] = (unsigned short)shot[y][x].color;

    int diff = 0;
    for (int i = 0; i < FIELD_W * FIELD_H; ++i)
        if (a[i] != b[i]) ++diff;

    printf("      pixels that changed after a 6 px nudge: %d\n", diff);
    check(diff > 50,
          "a sprite moved inside the same block still repaints (no stale freeze)");

    delete[] a;
    delete[] b;
}

/* --------------------------------------------------------------- [23] */
/*  Regression for "terrain makes the hero never catch the monsters".
 *
 *  A map can wall a region off completely.  Before the fix a monster could
 *  spawn inside such a pocket (or slide into one), where it was alive but
 *  unreachable: the hero could never close the distance, the monster could
 *  never die, and because a wave only advances when LiveMonsters()==0 the
 *  whole run stalled there forever.
 *
 *  The fix floods the walkable tiles from the hero each frame and only puts
 *  monsters where that flood reached, and gives them a gradient to walk back
 *  along.  This test proves both halves:
 *   - SpawnPos() only ever returns a tile the flood reaches, and
 *   - the flood is a genuine gradient: following NextAt() from any reached
 *     tile walks monotonically back onto the hero.
 * ------------------------------------------------------------------------ */
static void test_reachable_pathing(void)
{
    printf("[23] reachable spawns + flow field\n");

    World w;

    int tried = 0, ok = 0, got = 0;
    for (int k = 0; k < 400; ++k) {
        float x, y;
        if (w.SpawnPos(w.Hero().x, w.Hero().y, 340.0f, 500.0f, x, y)) {
            ++got; ++tried;
            int tx = (int)(x / TILE_SIZE);
            int ty = (int)(y / TILE_SIZE);
            if (w.DistAt(tx, ty) >= 0) ++ok;
        }
    }
    printf("      SpawnPos returned a reachable tile on %d of %d tries\n", ok, tried);
    check(got > 0, "SpawnPos finds candidate spawns");
    check(ok == tried, "every SpawnPos result is reachable from the hero");

    /* the gradient: from any reached tile, DistAt strictly decreases along
       NextAt() until it lands on the hero (dist 0) */
    int checked = 0, gradient = 0;
    for (int ty = 0; ty < MAP_H; ++ty)
        for (int tx = 0; tx < MAP_W; ++tx) {
            if (w.DistAt(tx, ty) < 0) continue;
            int cur = ty * MAP_W + tx;
            int reach = 0, steps = 0;
            while (steps < MAP_W * MAP_H) {
                int d = w.DistAt(cur % MAP_W, cur / MAP_W);
                if (d == 0) { reach = 1; break; }
                int nx = w.NextAt(cur);
                if (nx < 0) break;
                if (w.DistAt(nx % MAP_W, nx / MAP_W) >= d) break;  /* must shrink */
                cur = nx;
                ++steps;
            }
            ++checked;
            if (reach) ++gradient;
        }
    printf("      flow gradient returns to the hero from %d/%d reachable tiles\n",
           gradient, checked);
    check(checked > 0, "the map has reachable tiles");
    check(gradient == checked, "always walkable back to the hero, no dead pockets");
}

/* --------------------------------------------------------------- [24] */
/*  The enlarged full-map view (M).  Rendered headless just like the other
 *  renderer tests: build the world, DrawMap() into an off-screen canvas,
 *  Update() + BlitTo() to read the pixels.  Checks it fills the screen and
 *  that the hero marker and a boss marker land at their world positions.
 * ------------------------------------------------------------------------ */
static void test_render_bigmap(void)
{
    printf("[24] render: enlarged map\n");

    World w;
    w.SpawnMonster(MON_BOSS, w.Hero().x + 400.0f, w.Hero().y - 300.0f);

    Renderer r;
    WCanvas cv(FIELD_W, FIELD_H);
    r.DrawMap(cv, w, *g_atlas);

    int items = r.ItemCount();
    cv.Update();
    cv.SetPos(0, 0);

    WBitmap out(FIELD_W, FIELD_H);
    out.Clear(WPixel(0, 0, 0));
    cv.BlitTo(out);

    int blank = 0;
    for (int y = 0; y < FIELD_H; y += 7)
        for (int x = 0; x < FIELD_W; x += 7)
            if ((DWORD)out[y][x] == 0) ++blank;
    printf("      %d draw calls, %d blank samples\n", items, blank);
    check(items > 0 && items < 4000, "the enlarged map stays inside its draw budget");
    check(blank == 0, "the enlarged map fills the whole screen (no blanks)");

    /* hero sits at the world centre -> big-map pixel (400,300) */
    int hero = 0;
    for (int y = 292; y <= 308; ++y)
        for (int x = 392; x <= 408; ++x) {
            DWORD pv = (DWORD)out[y][x];
            unsigned r0 = (pv >> 16) & 0xFF, g0 = (pv >> 8) & 0xFF, b0 = pv & 0xFF;
            if (r0 > 150 && g0 > 150 && b0 > 150) ++hero;
        }
    printf("      hero-marker bright pixels: %d\n", hero);
    check(hero > 20, "the hero marker is drawn at the map centre");

    /* the boss is at world (hero.x+400, hero.y-300); scale it the same way
       the renderer does and look for the orange dot there */
    int bx = (FIELD_W - 640) / 2, by = (FIELD_H - 480) / 2;
    int mx = bx + (int)((w.Hero().x + 400.0f) / (float)WORLD_W * 640.0f);
    int my = by + (int)((w.Hero().y - 300.0f) / (float)WORLD_H * 480.0f);
    int orange = 0;
    for (int y = my - 10; y <= my + 10; ++y)
        for (int x = mx - 10; x <= mx + 10; ++x) {
            if (x < 0 || y < 0 || x >= FIELD_W || y >= FIELD_H) continue;
            DWORD pv = (DWORD)out[y][x];
            unsigned r0 = (pv >> 16) & 0xFF, g0 = (pv >> 8) & 0xFF, b0 = pv & 0xFF;
            if (r0 > 200 && g0 > 120 && g0 < 195 && b0 < 80) ++orange;
        }
    printf("      boss-marker orange pixels at (%d,%d): %d\n", mx, my, orange);
    check(orange > 8, "the boss marker is drawn at its world position");

    if (const char *q = getenv("WF_DUMP_BIG")) dumpFrame(out, q);
}

/* --------------------------------------------------------------- [25] */
/*  Product loop: every level owes a pick-one-of-three perk, and clearing the
 *  final wave wins the run and records a best score.  Driven headless.
 * ------------------------------------------------------------------------ */
static void test_progression(void)
{
    printf("[25] progression: perks, victory, best score\n");

    World w;
    check(w.App() == APP_PLAY, "a run starts in the play state");
    check(w.PendingPerks() == 0, "no perk owed at level 1");

    /* farm exactly one level: five grunts = 60 exp = level 2 */
    int idx = -1;
    for (int n = 0; n < 5; ++n) {
        w.SpawnMonster(MON_GRUNT, w.Hero().x, w.Hero().y + 40.0f);
        for (int i = 0; i < MAX_MONSTERS; ++i)
            if (w.Monsters()[i].alive) { idx = i; break; }
        if (idx >= 0) w.DamageMonster(idx, 100000, 0, w.Hero().x, w.Hero().y);
    }
    printf("      level %d, pending perks %d\n", w.Hero().level, w.PendingPerks());
    check(w.Hero().level > 1, "killing grants a level");
    check(w.PendingPerks() == 1, "the level owes exactly one perk");
    check(w.App() == APP_LEVELUP, "and flips to the level-up overlay state");

    const int *offers = w.PerkOffers();
    check(offers[0] >= 0 && offers[0] < PERK_COUNT &&
          offers[1] >= 0 && offers[1] < PERK_COUNT &&
          offers[2] >= 0 && offers[2] < PERK_COUNT, "three perks on offer");
    check(offers[0] != offers[1] && offers[1] != offers[2] &&
          offers[0] != offers[2], "the three offers are distinct");

    /* choose slot 0 and confirm the matching hero buff moved */
    int chosen = offers[0];
    int atk0 = w.Hero().atk, hp0 = w.Hero().maxhp;
    float spd0 = w.Hero().spdMul, atkSpd0 = w.Hero().atkSpdMul, crit0 = w.Hero().critAdd;
    float dash0 = w.Hero().dashCdMul, spin0 = w.Hero().spinCdMul;
    w.ChoosePerk(0);
    check(w.PerkTaken(chosen) == 1, "the chosen perk is recorded");
    check(w.PendingPerks() == 0, "the pick is consumed");
    check(w.App() == APP_PLAY, "and the run resumes");
    switch (chosen) {
    case PERK_BLADE: check(w.Hero().atk == atk0 + 5, "blade raises attack"); break;
    case PERK_IRON:  check(w.Hero().maxhp == hp0 + 26, "iron raises max hp"); break;
    case PERK_BOOTS: check(w.Hero().spdMul > spd0, "boots raise move speed"); break;
    case PERK_HASTE: check(w.Hero().atkSpdMul < atkSpd0, "haste quickens attacks"); break;
    case PERK_EDGE:  check(w.Hero().critAdd > crit0, "edge raises crit chance"); break;
    case PERK_FANG:  check(w.Hero().lifesteal > 0, "fang grants lifesteal"); break;
    case PERK_WIND:  check(w.Hero().dashCdMul < dash0, "wind quickens the dash"); break;
    case PERK_WHIRL: check(w.Hero().spinCdMul < spin0 && w.Hero().spinRangeAdd > 0.0f,
                           "whirl cheapens and widens the spin"); break;
    default: check(0, "unknown perk");
    }

    /* victory: drain the final wave and the run must end in a win, recording
       the best wave/kills in memory (save file untouched here) */
    World w2;
    w2.StartWave(WIN_WAVES);
    int guard = 0;
    while (w2.App() != APP_WIN && guard < 20000) {
        ++guard;
        w2.Step(noInput());
        if (w2.PendingPerks() > 0) w2.ChoosePerk(0);
        for (int i = 0; i < MAX_MONSTERS; ++i)
            if (w2.Monsters()[i].alive)
                w2.DamageMonster(i, 100000, 0, w2.Hero().x, w2.Hero().y);
    }
    printf("      cleared wave %d in %d frames -> app=%d\n",
           w2.State().wave, guard, w2.App());
    check(w2.App() == APP_WIN, "clearing the last wave wins the run");
    check(w2.State().gameover == 1, "a win also ends the run");
    check(w2.BestWave() == WIN_WAVES, "the best wave is recorded");
    check(w2.BestKills() >= w2.State().monstersThisWave, "and the best kill tally");
}

/* --------------------------------------------------------------- [26] */
/*  Product shell screens: title menu, controls page, the perk cards and the
 *  run summary.  All of them are renderer-only (they touch WCanvas, not the
 *  window), so each one is rendered headless and read back, and the two
 *  selectable states of the menu and the perk cards must actually differ --
 *  that is what proves the cursor moves.
 * ------------------------------------------------------------------------ */
static void renderInto(WBitmap &out, Renderer &r, int which,
                       const World &w, int arg, int *items)
{
    WCanvas cv(FIELD_W, FIELD_H);
    switch (which) {
    case 0: r.DrawTitle(cv, w, *g_atlas, 123u, arg); break;
    case 1: r.DrawHowto(cv, *g_atlas);               break;
    default:
        /* the perk overlay is meant to sit on top of a frozen world, so the
           world sheet goes down LAST (submitted last = bottom), exactly the
           order Draw() uses */
        r.DrawPerks(cv, w, *g_atlas, arg);
        cv.SetPos(0, 0);
        cv.Draw(WBox(FIELD_W, FIELD_H, WPixel((DWORD)COL_GRASS_A)), BLIT_COPY);
        break;
    }
    if (items) *items = r.ItemCount();
    cv.Update();
    cv.SetPos(0, 0);
    out.Clear(WPixel(0, 0, 0));
    cv.BlitTo(out);
}

/* counts pixels whose RGB is "mostly this colour" */
static int countColor(const WBitmap &img, int r, int g, int b, int tol)
{
    int n = 0;
    for (int y = 0; y < FIELD_H; y += 3)
        for (int x = 0; x < FIELD_W; x += 3) {
            DWORD pv = (DWORD)img[y][x];
            int r0 = (int)((pv >> 16) & 0xFF), g0 = (int)((pv >> 8) & 0xFF),
                b0 = (int)(pv & 0xFF);
            if (abs(r0 - r) < tol && abs(g0 - g) < tol && abs(b0 - b) < tol) ++n;
        }
    return n;
}

static int countDiff(const WBitmap &a, const WBitmap &b)
{
    int n = 0;
    for (int y = 0; y < FIELD_H; y += 2)
        for (int x = 0; x < FIELD_W; x += 2)
            if ((DWORD)a[y][x] != (DWORD)b[y][x]) ++n;
    return n;
}

static void test_render_shell(void)
{
    printf("[26] render: title / howto / perks / run summary\n");

    World w;
    WBitmap a(FIELD_W, FIELD_H), b(FIELD_W, FIELD_H);

    /* ---- title menu ----------------------------------------------------- */
    {
        Renderer r;
        int items = 0;
        renderInto(a, r, 0, w, 0, &items);
        printf("      title: %d draw calls\n", items);
        check(items > 20, "the title screen submits its layers");
    }
    {
        int blank = 0;
        for (int y = 0; y < FIELD_H; y += 7)
            for (int x = 0; x < FIELD_W; x += 7)
                if ((DWORD)a[y][x] == 0) ++blank;
        printf("      title: %d blank samples\n", blank);
        check(blank == 0, "the title screen fills the viewport");
    }
    int gold = countColor(a, 0xFF, 0xD2, 0x3C, 40);
    printf("      title: %d gold samples\n", gold);
    check(gold > 60, "the gold logo is drawn");
    if (const char *p = getenv("WF_DUMP_TITLE")) dumpFrame(a, p);

    /* the cursor must visibly move between the two rows */
    {
        Renderer r;
        renderInto(b, r, 0, w, 1, 0);
    }
    int moved = countDiff(a, b);
    printf("      title: %d samples differ between row 0 and row 1\n", moved);
    check(moved > 40, "selecting the other menu row changes the screen");

    /* ---- controls page -------------------------------------------------- */
    {
        WCanvas cv(FIELD_W, FIELD_H);
        Renderer r;
        r.DrawHowto(cv, *g_atlas);
        cv.Update();
        cv.SetPos(0, 0);
        a.Clear(WPixel(0, 0, 0));
        cv.BlitTo(a);

        int blank = 0;
        for (int y = 0; y < FIELD_H; y += 7)
            for (int x = 0; x < FIELD_W; x += 7)
                if ((DWORD)a[y][x] == 0) ++blank;
        printf("      howto: %d blank samples\n", blank);
        check(blank == 0, "the controls page fills the viewport");
        if (const char *p = getenv("WF_DUMP_HOWTO")) dumpFrame(a, p);
    }

    /* ---- perk cards ----------------------------------------------------- */
    World lp;
    for (int n = 0; n < 5; ++n) {
        lp.SpawnMonster(MON_GRUNT, lp.Hero().x, lp.Hero().y + 40.0f);
        for (int i = 0; i < MAX_MONSTERS; ++i)
            if (lp.Monsters()[i].alive) {
                lp.DamageMonster(i, 100000, 0, lp.Hero().x, lp.Hero().y);
                break;
            }
    }
    check(lp.App() == APP_LEVELUP, "a level flips the world into the perk state");

    {
        Renderer r;
        int items = 0;
        renderInto(a, r, 2, lp, 0, &items);
        printf("      perks: %d draw calls\n", items);
        check(items > 20, "the perk cards submit their layers");
    }
    {
        Renderer r;
        renderInto(b, r, 2, lp, 2, 0);
    }
    int cards = countDiff(a, b);
    if (const char *p = getenv("WF_DUMP_PERKS")) dumpFrame(a, p);
    printf("      perks: %d samples differ between card 0 and card 2\n", cards);
    check(cards > 60, "the selected perk card is highlighted");

    /* ---- run summary (dead) --------------------------------------------- */
    World dp;
    for (int i = 0; i < 40 && !dp.State().gameover; ++i) {
        dp.DamageHero(30);
        for (int k = 0; k < 45 && dp.Hero().invuln > 0; ++k) dp.Step(noInput());
    }
    check(dp.State().gameover == 1, "the hero dies for the summary test");
    {
        WCanvas cv(FIELD_W, FIELD_H);
        Renderer r;
        r.Draw(cv, dp, *g_atlas);          /* Draw() dispatches the summary */
        cv.Update();
        cv.SetPos(0, 0);
        a.Clear(WPixel(0, 0, 0));
        cv.BlitTo(a);

        int red = countColor(a, 0xE0, 0x3C, 0x3C, 30);
        printf("      summary: %d red samples\n", red);
        check(red > 20, "the defeat banner is drawn");
        if (const char *p = getenv("WF_DUMP_OVER")) dumpFrame(a, p);
    }

    /* ---- run summary (won) ---------------------------------------------- */
    World wp2;
    wp2.StartWave(WIN_WAVES);
    int guard = 0;
    while (wp2.App() != APP_WIN && guard < 20000) {
        ++guard;
        wp2.Step(noInput());
        if (wp2.PendingPerks() > 0) wp2.ChoosePerk(0);
        for (int i = 0; i < MAX_MONSTERS; ++i)
            if (wp2.Monsters()[i].alive)
                wp2.DamageMonster(i, 100000, 0, wp2.Hero().x, wp2.Hero().y);
    }
    check(wp2.App() == APP_WIN, "clearing the last wave wins the run");
    {
        WCanvas cv(FIELD_W, FIELD_H);
        Renderer r;
        r.Draw(cv, wp2, *g_atlas);
        cv.Update();
        cv.SetPos(0, 0);
        a.Clear(WPixel(0, 0, 0));
        cv.BlitTo(a);

        int gold2 = countColor(a, 0xFF, 0xD2, 0x3C, 40);
        printf("      victory: %d gold samples\n", gold2);
        check(gold2 > 20, "the victory banner is drawn");
        if (const char *p = getenv("WF_DUMP_WIN")) dumpFrame(a, p);
    }
}

/* ---------------------------------------------------------------------- */
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("=== Warfire headless test ===\n");
    printf("    simulation + renderer, no window, no DirectDraw\n\n");

    test_reset();
    test_movement();
    test_tiles();
    test_attack_combo();
    test_attack_hits();
    test_kill_and_loot();
    test_level_up();
    test_taking_damage();
    test_dash_and_spin();
    test_monster_ai();
    test_waves();
    test_determinism();
    test_pools();
    test_hero_never_inside_walls();
    test_progression();

    /* The renderer needs the sprite atlas, which needs a GDI font. */
    Atlas atlas;
    if (!atlas.Build()) {
        printf("\n[!] atlas build failed -- skipping the renderer tests\n");
    } else {
        g_atlas = &atlas;
        test_render_terrain();
        test_render_actors();
        test_render_layers();
        test_render_effects();
        test_render_bigmap();
        test_render_shell();
        test_render_stress();
        test_frame_budget();
    }

    test_timer_fidelity();
    test_canvas_dirty_scroll();
    test_reachable_pathing();

    printf("\n=== RESULT: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
