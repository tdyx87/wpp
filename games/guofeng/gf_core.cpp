/* ===========================================================================
 *  gf_core.cpp -- Guofeng : isometric scene logic
 *  See gf_core.h.  No windows.h, no windsoul: this file must build and run
 *  with nothing but the C runtime so the tests can drive it headless.
 * ===========================================================================
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gf_core.h"

namespace gf {

/* ---------------------------------------------------------------- constants */

/* Tile-space speed.  A tile is one unit, so this is "tiles per second" and
   the walk/run ratio is what a player feels rather than what looks right on
   paper.  Dividing by 60 happens in Step(). */
static const float WALK_SPEED = 3.2f;
static const float RUN_SPEED  = 5.6f;

/* Frames between walk-cycle steps.  60 / 8 = 7.5 steps a second, which reads
   as a brisk walk without the feet blurring. */
static const int ANIM_PERIOD = 8;

/* A tiny LCG.  Deterministic and self-contained on purpose: the tests assert
   on generated content, so the generator may not depend on rand(), whose
   sequence is implementation defined. */
static unsigned lcg(unsigned &s)
{
    s = s * 1103515245u + 12345u;
    return (s >> 16) & 0x7FFFu;
}

/* ------------------------------------------------------------------- solids */

static bool DecoIsSolid(int d)
{
    switch (d) {
    case D_BAMBOO:
    case D_PINE:
    case D_ROCK:
    case D_HOUSE:
    case D_WELL:
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------- bounds */

ScreenBox WorldScreenBox()
{
    ScreenBox b;
    /* The extreme diamond centres, then grown by half a tile so the corners
       of the outermost diamonds are inside too. */
    b.x0 = -(MAP_H - 1) * TILE_HW - TILE_HW;
    b.x1 =  (MAP_W - 1) * TILE_HW + TILE_HW;
    b.y0 = 0 - TILE_HH;
    b.y1 = (MAP_W - 1 + MAP_H - 1) * TILE_HH + TILE_HH;
    return b;
}

TileRange VisibleRange(int camX, int camY, int vw, int vh)
{
    const int cx[4] = { camX, camX + vw, camX, camX + vw };
    const int cy[4] = { camY, camY, camY + vh, camY + vh };

    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < 4; ++i) {
        float tx, ty;
        ScreenToIsoF(cx[i], cy[i], tx, ty);
        if (tx < minX) minX = tx;
        if (tx > maxX) maxX = tx;
        if (ty < minY) minY = ty;
        if (ty > maxY) maxY = ty;
    }

    TileRange r;
    r.x0 = (int)floorf(minX) - 1;
    r.y0 = (int)floorf(minY) - 1;
    r.x1 = (int)ceilf (maxX) + 1;
    r.y1 = (int)ceilf (maxY) + 1;

    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > MAP_W - 1) r.x1 = MAP_W - 1;
    if (r.y1 > MAP_H - 1) r.y1 = MAP_H - 1;
    return r;
}

/* -------------------------------------------------------------------- scene */

Scene::Scene()
{
    Reset(1u);
}

void Scene::Reset(unsigned seed)
{
    memset(m_tile, 0, sizeof(m_tile));
    memset(m_deco, 0, sizeof(m_deco));
    m_tick = 0;

    m_hero.x = 15.5f;
    m_hero.y = 15.5f;
    m_hero.face = FACE_D;
    m_hero.frame = 0;
    m_hero.moving = 0;
    m_hero.animAcc = 0;

    genMap(seed);
    placeDecorations(seed);

    m_camX = 0;
    m_camY = 0;
    followCam();
}

void Scene::SetTile(int tx, int ty, TileId t)
{
    if (!InMap(tx, ty)) return;
    m_tile[ty * MAP_W + tx] = (unsigned char)t;
}

void Scene::SetDeco(int tx, int ty, DecoId d)
{
    if (!InMap(tx, ty)) return;
    m_deco[ty * MAP_W + tx] = (unsigned char)d;
}

void Scene::SetHero(float x, float y)
{
    m_hero.x = x;
    m_hero.y = y;
    followCam();
}

TileId Scene::TileAt(int tx, int ty) const
{
    if (!InMap(tx, ty)) return T_GRASS;
    return (TileId)m_tile[ty * MAP_W + tx];
}

DecoId Scene::DecoAt(int tx, int ty) const
{
    if (!InMap(tx, ty)) return D_NONE;
    return (DecoId)m_deco[ty * MAP_W + tx];
}

bool Scene::Blocked(int tx, int ty) const
{
    if (!InMap(tx, ty)) return true;
    if (TileAt(tx, ty) == T_WATER) return true;
    return DecoIsSolid(DecoAt(tx, ty));
}

/* ------------------------------------------------------------------- ground */
/*  Two straight roads crossing at the middle of the map.  A road along
 *  tx + ty = const is a HORIZONTAL line on screen and one along tx - ty =
 *  const is a VERTICAL one, so the two diagonals of the tile grid are the
 *  two axes of the picture -- which is exactly why the plaza where they meet
 *  is the natural centre of the scene.
 *
 *  Everything is measured off those two lines, so the village stays coherent
 *  no matter how the numbers are retuned. */
void Scene::genMap(unsigned seed)
{
    unsigned s = seed * 2654435761u + 1u;

    const int HW = MAP_W / 2;           /* 16: the crossing              */
    const int HH = MAP_H / 2;

    for (int ty = 0; ty < MAP_H; ++ty) {
        for (int tx = 0; tx < MAP_W; ++tx) {
            unsigned char t = (unsigned char)T_GRASS;

            const int d1 = tx + ty - (HW + HH - 1);   /* the horizontal road */
            const int d2 = tx - ty;                   /* the vertical road   */

            /* the two roads, two tiles wide */
            if (d1 == 0 || d1 == -1 || d2 == 0 || d2 == -1)
                t = (unsigned char)T_DIRT;

            /* the plaza: a diamond of cut stone around the crossing */
            if (abs(tx - HW) + abs(ty - HH) <= 4)
                t = (unsigned char)T_PAVE;

            /* the pond, in the left quadrant */
            {
                const float px = (float)(tx - 7) / 5.0f;
                const float py = (float)(ty - 23) / 3.5f;
                const float r  = px * px + py * py;
                if (r <= 1.0f)
                    t = (unsigned char)T_WATER;
                else if (r <= 1.55f)
                    t = (unsigned char)T_STONE;      /* the bank            */
            }

            /* a second, smaller pool up by the bamboo, to break the skyline */
            {
                const float px = (float)(tx - 26) / 3.0f;
                const float py = (float)(ty - 6) / 2.2f;
                if (px * px + py * py <= 1.0f)
                    t = (unsigned char)T_WATER;
            }

            /* speckle: single stray tiles so the ground never reads as a
               flat fill.  Skipped on the roads and the plaza. */
            if (t == T_GRASS) {
                unsigned r = lcg(s);
                if ((r & 31) == 0)      t = (unsigned char)T_STONE;
                else if ((r & 15) == 0) t = (unsigned char)T_DIRT;
            }

            m_tile[ty * MAP_W + tx] = t;
        }
    }
}

/* -------------------------------------------------------------- decorations */
void Scene::placeDecorations(unsigned seed)
{
    unsigned s = seed * 40503u + 7u;

    const int HW = MAP_W / 2;
    const int HH = MAP_H / 2;

    /* ---- the lanterns ----------------------------------------------------
       One every four tiles down the vertical road, alternating sides, so the
       road reads as a road even when the camera is tight on it.  They are
       the additive-light demo, so they are placed where the ground under
       them is dark enough for the glow to show. */
    for (int k = 0; k < 6; ++k) {
        int tx = 4 + k * 4;
        SetDeco(tx, tx - 3, D_LANTERN);
        SetDeco(tx + 3, tx, D_LANTERN);
    }

    /* ---- the houses ------------------------------------------------------
       Two rows, one on each side of the vertical road, plus a pair closing
       the far end of the horizontal one.  Kept off the plaza. */
    static const int house[6][2] = {
        { 11, 18 }, { 11, 19 }, { 21, 13 }, { 21, 14 }, { 13, 22 }, { 14, 22 }
    };
    for (int i = 0; i < 6; ++i)
        SetDeco(house[i][0], house[i][1], D_HOUSE);

    /* the well, at the plaza edge -- a landmark to walk to */
    SetDeco(HW + 3, HH - 1, D_WELL);

    /* ---- the bamboo groves -----------------------------------------------
       Two large groves in opposite corners, plus two clumps deliberately
       planted right beside the road.  The clumps are the point: the hero
       walks past them and they have to paint OVER him, which is the whole
       reason the depth sort exists. */
    for (int ty = 22; ty < MAP_H; ++ty) {
        for (int tx = 22; tx < MAP_W; ++tx) {
            if (lcg(s) % 100 < 62 && TileAt(tx, ty) == T_GRASS)
                SetDeco(tx, ty, D_BAMBOO);
        }
    }
    for (int ty = 1; ty < 8; ++ty) {
        for (int tx = 1; tx < 8; ++tx) {
            if (lcg(s) % 100 < 55 && TileAt(tx, ty) == T_GRASS)
                SetDeco(tx, ty, D_BAMBOO);
        }
    }
    static const int clump[4][2] = { { 14, 14 }, { 15, 13 }, { 13, 15 }, { 14, 13 } };
    for (int i = 0; i < 4; ++i)
        if (TileAt(clump[i][0], clump[i][1]) != T_WATER)
            SetDeco(clump[i][0], clump[i][1], D_BAMBOO);

    /* ---- pines and rocks -------------------------------------------------
       Scattered on open grass.  Deterministic, and never on a road, the
       plaza, the water or an existing decoration. */
    for (int ty = 0; ty < MAP_H; ++ty) {
        for (int tx = 0; tx < MAP_W; ++tx) {
            if (DecoAt(tx, ty) != D_NONE) continue;
            if (TileAt(tx, ty) != T_GRASS) continue;
            unsigned r = lcg(s);
            if ((r & 255) < 14)      SetDeco(tx, ty, D_PINE);
            else if ((r & 255) < 26) SetDeco(tx, ty, D_ROCK);
        }
    }

    /* the hero must not start inside anything */
    SetDeco(15, 15, D_NONE);
    SetDeco(15, 16, D_NONE);
    SetDeco(16, 15, D_NONE);
    SetDeco(16, 16, D_NONE);
}

/* ------------------------------------------------------------------- camera */
void Scene::followCam()
{
    const ScreenBox b = WorldScreenBox();

    m_camX = HeroSX() - FIELD_W / 2;
    m_camY = HeroSY() - FIELD_H / 2;

    /* Centre the world instead of clamping when it is smaller than the
       viewport -- otherwise the clamp below would invert and jump. */
    const int worldW = b.x1 - b.x0;
    const int worldH = b.y1 - b.y0;

    if (worldW <= FIELD_W) m_camX = b.x0 - (FIELD_W - worldW) / 2;
    else if (m_camX < b.x0)      m_camX = b.x0;
    else if (m_camX > b.x1 - FIELD_W) m_camX = b.x1 - FIELD_W;

    if (worldH <= FIELD_H) m_camY = b.y0 - (FIELD_H - worldH) / 2;
    else if (m_camY < b.y0)      m_camY = b.y0;
    else if (m_camY > b.y1 - FIELD_H) m_camY = b.y1 - FIELD_H;
}

/* The hero's screen position, including the fractional part of his tile
   position projected the same way -- otherwise the walk snaps tile to tile
   and looks like a stop-motion puppet. */
void Scene::heroScreen(int &sx, int &sy) const
{
    const float bx = floorf(m_hero.x);
    const float by = floorf(m_hero.y);

    IsoToScreen((int)bx, (int)by, sx, sy);

    const float fx = m_hero.x - bx;
    const float fy = m_hero.y - by;

    sx += (int)((fx - fy) * (float)TILE_HW);
    sy += (int)((fx + fy) * (float)TILE_HH);
}

int Scene::HeroSX() const
{
    int sx, sy;
    heroScreen(sx, sy);
    return sx;
}

int Scene::HeroSY() const
{
    int sx, sy;
    heroScreen(sx, sy);
    return sy;
}

/* --------------------------------------------------------------------- step */

/* Solid test for a fractional tile position.  The hero is treated as a point
   at his feet: a footprint test would need the sprite's real silhouette and
   would make the collision feel sticky in a 2:1 grid, where a "circle" is an
   ellipse on screen. */
static bool BlockedAt(const Scene &sc, float fx, float fy)
{
    const int tx = (int)floorf(fx);
    const int ty = (int)floorf(fy);
    return sc.Blocked(tx, ty);
}

void Scene::Step(const Input &in)
{
    ++m_tick;

    int mx = 0, my = 0;
    int face = m_hero.face;

    /* The four keys map to the four screen directions.  In a 2:1 isometric the
       tile axes project onto the screen DIAGONALS:
           +x -> ( +32, +16 )   right-down      +y -> ( -32, +16 )   left-down
           -x -> ( -32, -16 )   left-up         -y -> ( +32, -16 )   right-up
       so "towards the top of the picture" is -x -y, not -y.  Getting this
       wrong is the classic first bug: the character then walks along the
       screen diagonals while the art faces the cardinals. */
    if (in.up)    { mx -= 1; my -= 1; face = FACE_U; }
    if (in.down)  { mx += 1; my += 1; face = FACE_D; }
    if (in.left)  { mx -= 1; my += 1; face = FACE_L; }
    if (in.right) { mx += 1; my -= 1; face = FACE_R; }

    if (mx || my) {
        /* Normalise in TILE space, not screen space.  Screen space would
           make walking "up" (32 px per 2 tiles) half the speed of walking
           "right" (64 px per 2 tiles) -- that foreshortening is real and
           desirable, but normalising by it would make the hero cover twice
           as much ground per second going up. */
        const float len = sqrtf((float)(mx * mx + my * my));
        const float sp  = (in.run ? RUN_SPEED : WALK_SPEED) / 60.0f;

        const float nx = m_hero.x + (float)mx / len * sp;
        const float ny = m_hero.y + (float)my / len * sp;

        /* Axis-separated so the hero slides along a wall instead of sticking
           to it, which is the difference between "blocked" and "broken". */
        bool moved = false;
        if (!BlockedAt(*this, nx, m_hero.y)) { m_hero.x = nx; moved = true; }
        if (!BlockedAt(*this, m_hero.x, ny)) { m_hero.y = ny; moved = true; }

        m_hero.moving = moved ? 1 : 0;
        m_hero.face   = face;

        if (moved) {
            m_hero.animAcc += 1;
            if (m_hero.animAcc >= ANIM_PERIOD) {
                m_hero.animAcc = 0;
                m_hero.frame = (m_hero.frame + 1) % HERO_FRAMES;
            }
        }
    } else {
        m_hero.moving = 0;
        m_hero.frame  = 0;
        m_hero.animAcc = 0;
    }

    followCam();
}

} /* namespace gf */
