/* ===========================================================================
 *  gf_core.h -- Guofeng : isometric scene logic
 * ---------------------------------------------------------------------------
 *  A 45-degree isometric (dimetric 2:1) Chinese-style village, built to find
 *  out how far the Windsoul++ renderer can be pushed towards the look of a
 *  late-90s/2000s Chinese isometric RPG.
 *
 *  This header is deliberately free of windows.h / DirectDraw / windsoul so
 *  the projection, the depth ordering and the camera can be exercised
 *  headless -- see tests/test_guofeng.cpp.  gf_render.cpp only reads the
 *  state exposed here; gf_game.cpp owns the window and the input.
 *
 *  Everything is integer screen pixels and tile units.  Time is a fixed
 *  60 Hz step: one Scene::Step() call is exactly 1/60 s.
 * ===========================================================================
 */
#ifndef GF_CORE_H
#define GF_CORE_H

namespace gf {

/* ------------------------------------------------------------------ layout */
enum {
    FIELD_W = 800,                      /* client area                      */
    FIELD_H = 600,

    MAP_W   = 32,                       /* tiles                            */
    MAP_H   = 32,

    /* The isometric diamond.  2:1 is the classic ratio: a 64x32 diamond
       tiles the plane exactly and keeps every edge on a 2:1 slope, which is
       what makes hand-drawn pixel art line up with the grid. */
    TILE_W  = 64,
    TILE_H  = 32,
    TILE_HW = TILE_W / 2,               /* 32 -- half width,  also the x step */
    TILE_HH = TILE_H / 2,               /* 16 -- half height, also the y step */

    MAX_DECOS = 512,

    /* Above the tile centre.  A tall sprite is what makes the depth sort
       visible: a bamboo clump two rows nearer the camera has to paint over
       the hero standing behind it. */
    DECO_MAX_UP = 128,

    HERO_W  = 34,                       /* baked sprite box                 */
    HERO_H  = 56,
    HERO_FRAMES = 4                     /* walk cycle                       */
};

/* ------------------------------------------------------------------- tiles */
enum TileId {
    T_GRASS = 0,
    T_DIRT,                             /* the trodden path                 */
    T_PAVE,                             /* cut stone slabs, in front of houses */
    T_STONE,                            /* rough ground / river bank        */
    T_WATER,
    TILE_COUNT
};

/* ------------------------------------------------------------- decorations */
/*  Order matters only in that D_NONE must be 0 so a cleared map reads as
 *  empty.  `solid` decorations block the hero. */
enum DecoId {
    D_NONE = 0,
    D_BAMBOO,                           /* tall, sways -- the occlusion demo */
    D_PINE,
    D_ROCK,
    D_LANTERN,                          /* the additive-light demo          */
    D_HOUSE,
    D_WELL,
    DECO_COUNT
};

/* ----------------------------------------------------------------- facings */
/*  Four facings, named after where they point on SCREEN rather than in tile
 *  space, because that is what the player sees.  In a 2:1 isometric the four
 *  tile axes project to the four screen diagonals, so:
 *
 *      +x -> ( +32, +16 )  right-down        -y -> ( +32, -16 )  right-up
 *      +y -> ( -32, +16 )  left-down         -x -> ( -32, -16 )  left-up
 *
 *  Holding one key therefore walks along one screen axis, and the four keys
 *  map to the four cardinal screen directions.  The tile-space delta that
 *  produces each of them is in the table below. */
enum Face {
    FACE_R = 0,                         /* screen right  : +x -y            */
    FACE_U,                             /* screen up     : -x -y            */
    FACE_L,                             /* screen left   : -x +y            */
    FACE_D,                             /* screen down   : +x +y            */
    FACE_COUNT
};

/* -------------------------------------------------------------- projection */
/*  IsoToScreen returns the CENTRE of the diamond, not its top vertex.  That
 *  keeps the depth sort and the sprite anchoring on one number, and the
 *  inverse stays a clean 2x2 solve.  A tile sprite is blitted at
 *  (sx - TILE_HW, sy - TILE_HH). */
inline void IsoToScreen(int tx, int ty, int &sx, int &sy)
{
    sx = (tx - ty) * TILE_HW;
    sy = (tx + ty) * TILE_HH;
}

/* Float inverse, used for mouse picking and for the culling box.  The
 * integer version would have to round, and rounding is what makes a pick
 * land on the wrong tile near an edge. */
inline void ScreenToIsoF(int sx, int sy, float &tx, float &ty)
{
    float a = (float)sx / (float)TILE_HW;       /* tx - ty */
    float b = (float)sy / (float)TILE_HH;       /* tx + ty */
    tx = (a + b) * 0.5f;
    ty = (b - a) * 0.5f;
}

/* Row-major depth: a tile is "nearer the camera" the larger tx + ty is.  Two
 * tiles never share a depth value unless they are on the same anti-diagonal,
 * and those never overlap on screen, so the order between them is free. */
inline int TileDepth(int tx, int ty) { return tx + ty; }

/* The screen rectangle the whole map can ever occupy.  Used to clamp the
 * camera and to size the baked ground sheet. */
struct ScreenBox { int x0, y0, x1, y1; };
ScreenBox WorldScreenBox();

/* The tile window a viewport covers.  Computed from the four screen corners
 * rather than by inverting the rectangle, because a rotated (isometric)
 * rectangle is not a rectangle: the axis-aligned bound of the four inverted
 * corners is the tight answer, and inverting only the two opposite corners
 * silently loses a triangle in each of the four quadrants. */
struct TileRange { int x0, y0, x1, y1; };
TileRange VisibleRange(int camX, int camY, int vw, int vh);

/* ------------------------------------------------------------------- input */
struct Input {
    int up, down, left, right;          /* held                             */
    int run;                            /* held: hold shift to run          */
};

/* -------------------------------------------------------------------- hero */
struct Hero {
    float x, y;                         /* tile units, fractional           */
    int   face;                         /* Face                             */
    int   frame;                        /* 0..HERO_FRAMES-1                 */
    int   moving;
    int   animAcc;                      /* frame timer accumulator          */
};

/* ------------------------------------------------------------------- scene */
class Scene {
public:
    Scene();

    void Reset(unsigned seed);
    void Step(const Input &in);

    /* ---- map ---- */
    TileId TileAt(int tx, int ty) const;
    DecoId DecoAt(int tx, int ty) const;
    bool   InMap (int tx, int ty) const
    {
        return tx >= 0 && ty >= 0 && tx < MAP_W && ty < MAP_H;
    }
    /* A solid tile or a solid decoration.  Water is solid: the village is
       built around the pond, not through it. */
    bool   Blocked(int tx, int ty) const;

    /* Only for tests and for the generator -- the map is static otherwise. */
    void SetTile(int tx, int ty, TileId t);
    void SetDeco(int tx, int ty, DecoId d);

    /* Test seam.  A test that wants to look at a particular corner of the
       village would otherwise have to walk the hero there frame by frame,
       which makes the test depend on the whole movement and collision path
       instead of on the thing it is actually checking.  The shell never
       calls it. */
    void SetHero(float x, float y);

    /* ---- camera ---- */
    int CamX() const { return m_camX; }
    int CamY() const { return m_camY; }

    /* ---- hero ---- */
    const Hero &H() const { return m_hero; }
    int HeroSX() const;                 /* screen position of the feet      */
    int HeroSY() const;

    unsigned Tick() const { return m_tick; }

private:
    void genMap(unsigned seed);
    void placeDecorations(unsigned seed);
    void followCam();
    void heroScreen(int &sx, int &sy) const;

    unsigned char m_tile[MAP_W * MAP_H];
    unsigned char m_deco[MAP_W * MAP_H];

    Hero m_hero;
    int  m_camX, m_camY;
    unsigned m_tick;
};

/* ----------------------------------------------------------------- palette */
/*  A warm, saturated, slightly dusty palette -- the look of printed silk and
 *  lacquer rather than of a modern 3D renderer.  Colours are 0xRRGGBB; the
 *  library converts to RGB565 on the way in, so the low bits are approximate
 *  on purpose and nothing here should be compared for exact equality after a
 *  round trip through a bitmap. */
enum {
    /* ground */
    COL_GRASS_A   = 0x5E9450,
    COL_GRASS_B   = 0x4E8044,
    COL_GRASS_C   = 0x3E6C38,
    COL_DIRT_A    = 0xA08050,
    COL_DIRT_B    = 0x8A6C42,
    COL_DIRT_C    = 0x745836,
    COL_PAVE_A    = 0xA2A2A8,
    COL_PAVE_B    = 0x8C8C94,
    COL_PAVE_C    = 0x74747E,
    COL_STONE_A   = 0x8E8E96,
    COL_STONE_B   = 0x7A7A84,
    COL_STONE_C   = 0x646470,
    COL_WATER_A   = 0x3E8AB4,
    COL_WATER_B   = 0x2E7096,
    COL_WATER_C   = 0x205878,
    COL_WAVE      = 0x8AD2E8,

    /* scenery */
    COL_BAMBOO_A  = 0x7CB44E,
    COL_BAMBOO_B  = 0x5C9438,
    COL_BAMBOO_C  = 0x3E6C28,
    COL_PINE_A    = 0x3A7A46,
    COL_PINE_B    = 0x2A5C34,
    COL_PINE_C    = 0x1C4024,
    COL_TRUNK     = 0x5A4228,
    COL_TRUNK_LIT = 0x765A3A,
    COL_ROCK_A    = 0x9A9AA2,
    COL_ROCK_B    = 0x7E7E88,
    COL_ROCK_C    = 0x60606C,
    COL_LANTERN   = 0xE0483A,
    COL_LANTERN_L = 0xFF7A5A,
    COL_WELL      = 0x8A8478,

    /* architecture */
    COL_WALL_A    = 0xE0D6BE,
    COL_WALL_B    = 0xC4B89C,
    COL_ROOF_A    = 0x8E3A2E,
    COL_ROOF_B    = 0x6E2A22,
    COL_ROOF_L    = 0xB0503C,
    COL_EAVE      = 0xC8A24A,

    /* ui -- ink and lacquer, not the blue-grey of the other prototype */
    COL_INK       = 0x14181E,
    COL_INK_KEY   = 0x060A0E,
    COL_PANEL     = 0x1E2C28,
    COL_PANEL_LIT = 0x486C5C,
    COL_PANEL_SHD = 0x0C1412,
    COL_JADE      = 0x3EA88E,
    COL_JADE_DIM  = 0x1E6454,
    COL_GOLD      = 0xE2C070,
    COL_GOLD_LIT  = 0xFFEAA8,
    COL_GOLD_DIM  = 0x8A7038,
    COL_CINNABAR  = 0xC03A2E,
    COL_SILK      = 0xE8E0C8,
    COL_SILK_DIM  = 0xA8A088,

    /* additive-only: the colour that means "add nothing" */
    COL_NOTHING   = 0x000000,
    /* the BLIT_MASK key.  blit16.asm loads the mask from 0xF81F in RGB565,
       which is magenta -- NOT black.  A sprite baked on black gets a black
       box around it. */
    COL_MASK      = 0xFF00FF
};

} /* namespace gf */

#endif /* GF_CORE_H */
