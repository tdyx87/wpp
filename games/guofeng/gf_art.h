/* ===========================================================================
 *  gf_art.h -- Guofeng : procedural atlas
 * ---------------------------------------------------------------------------
 *  Every pixel in this prototype is generated at start-up.  There is no image
 *  file anywhere in the project, partly because the original assets are not
 *  ours to ship and partly because this snapshot of windsoul has no sprite
 *  framework at all -- no wsprite, no sptcache, and WZoomBitmap is declared
 *  in include/wszoom.h with no implementation behind it.  So: no runtime
 *  scaling, no rotation, no colour matrix.  Everything has to be baked at the
 *  size it is drawn.
 *
 *  Three consequences shape the art:
 *
 *  1. TILE TRANSITIONS cannot use a per-pixel alpha ramp.  WAlphaBitmap looks
 *     like an 8-bit alpha channel but blit16.asm compares each channel byte
 *     against 32 and uses it as a 1-bit stencil; the actual blend factor is
 *     the object's constant userdata.  A smooth edge is therefore done the
 *     period way: ordered dithering with a 4x4 Bayer matrix, which is what
 *     the era's pixel artists used anyway and which reads better at this
 *     scale than a smooth ramp would.
 *
 *  2. THE WHOLE GROUND IS ONE BAKED SHEET.  A 32x32 map at 64x32 diamonds
 *     covers 2048x1024 screen pixels and about a thousand diamonds intersect
 *     an 800x600 viewport.  Drawing five links per diamond (base plus four
 *     transition bands) would be five thousand draw calls a frame; baking the
 *     sheet once and blitting it is one.  The individual tile and edge
 *     bitmaps are still baked -- they are the tileset, the tests assert on
 *     them, and the sheet is composed from the same functions -- but the
 *     frame loop only ever touches the sheet.
 *
 *  3. ADDITIVE LIGHT carries its falloff in the pixel VALUES, because
 *     BLIT_ADDITIVE has no alpha at all: adding black leaves the destination
 *     untouched, so a radial blob whose rim is pure black is the only way to
 *     get a soft glow.
 * ===========================================================================
 */
#ifndef GF_ART_H
#define GF_ART_H

#include "gf_core.h"
#include "gf_text.h"        /* GF_LABEL_COUNT and the GBK label table */

class WBitmap;
class WFont;

namespace gf {

enum {
    /* Three texture variants per material, picked per tile from a hash of
       the tile coordinate.  Two is not enough to hide the repeat on a 32x32
       map and four costs bake time for no visible gain. */
    TILE_VARIANTS = 3,

    /* The transition band, as a fraction of the distance from the edge to the
       opposite vertex.  0.30 gives a feather about ten pixels deep, which is
       roughly a third of the diamond's half height. */
    EDGE_BAND_PCT = 30,

    GLOW_FRAMES = 4,            /* lantern flicker                          */
    SHADOW_TIERS = 3,
    CLOUD_VARIANTS = 3,
    CORNER_COUNT = 4,           /* one 回纹 motif per screen corner         */

    /* The printable ASCII range 32..126, baked one bitmap per character.
       Ninety-five small bitmaps is a rounding error next to the 4 MB ground
       sheet, and it buys a single typeface for the whole interface. */
    ASCII_FIRST = 32,
    ASCII_LAST  = 126,
    GF_ASCII_COUNT = ASCII_LAST - ASCII_FIRST + 1
};

class Atlas {
public:
    Atlas();
    ~Atlas();

    /* Bakes everything.  Needs the map (for the ground sheet) and a GDI font
       (for the labels), so it must run on a machine with a desktop.  Returns
       false if the font could not be created; everything else is pure
       arithmetic and cannot fail. */
    bool Build(const class Scene &scene);
    void Destroy();

    bool Ok() const { return m_ok; }

    /* ---- ground ---- */
    WBitmap *Tile(int t, int v) const;              /* one diamond          */
    /* The transition band for material `t` along tile-space direction `dir`
       (0 = +x, 1 = +y, 2 = -x, 3 = -y), dithered against the next material in
       the TileId enum.  This is the tileset's representative transition
       sprite: the composed sheet picks the actual neighbour per tile, but the
       dither pattern and the band geometry are the same, so a test can assert
       on this one bitmap and mean something. */
    WBitmap *Edge(int t, int v, int dir) const;
    WBitmap *Ground() const { return m_ground; }    /* the whole sheet      */
    int      GroundX() const { return m_groundX; }  /* its world screen origin */
    int      GroundY() const { return m_groundY; }
    WBitmap *Mini() const { return m_mini; }        /* 1/16 -- the HUD map  */
    int      MiniW() const { return m_miniW; }
    int      MiniH() const { return m_miniH; }
    WBitmap *Big() const { return m_big; }          /* 1/4 -- the M screen  */
    int      BigW() const { return m_bigW; }
    int      BigH() const { return m_bigH; }

    /* ---- scenery ---- */
    WBitmap *Deco(int kind) const;
    int DecoW(int kind) const;
    int DecoH(int kind) const;
    /* The sprite pixel that lands on the tile CENTRE.  A tall sprite extends
       upwards from there, which is what makes the depth sort visible. */
    int DecoAnchorX(int kind) const;
    int DecoAnchorY(int kind) const;
    /* How far the sprite reaches above the tile centre -- used by the renderer
       to decide which decorations can possibly overlap the hero. */
    int DecoUp(int kind) const;

    /* ---- actor ---- */
    WBitmap *Hero(int face, int frame) const;

    /* ---- effects ---- */
    WBitmap *Shadow(int tier) const;    /* BLIT_SUBTRACT, soft blob         */
    WBitmap *Glow(int frame) const;     /* BLIT_ADDITIVE, radial            */
    WBitmap *Spark(int frame) const;    /* BLIT_ADDITIVE, water glint       */
    WBitmap *Vignette() const;          /* BLIT_SUBTRACT, full screen       */

    /* ---- ui ---- */
    WBitmap *Label(int id) const;
    int      LabelW(int id) const;
    /* The printable ASCII range, baked in the same face as the Chinese
       labels.  The built-in WFont rasteriser is a 7x12 fixed pitch face and
       reads as a different program when it is set beside 微软雅黑, so every
       piece of UI text -- including the key names on the controls page --
       goes through here.  `c` is the character code, 32..126. */
    WBitmap *Ascii(int c) const;
    int      AsciiW(int c) const;
    WBitmap *Corner(int i) const;       /* 回纹 corner motif, gold          */
    WBitmap *Cloud(int i) const;        /* 云纹 flourish                    */
    WBitmap *Seal() const;              /* cinnabar 印章                    */
    WBitmap *Plaque() const;            /* the 匾额 board for the title     */
    int      PlaqueW() const { return m_plaqueW; }
    int      PlaqueH() const { return m_plaqueH; }

private:
    bool bakeGround(const Scene &scene);
    bool bakeUi();

    WBitmap *m_tile[TILE_COUNT][TILE_VARIANTS];
    WBitmap *m_edge[TILE_COUNT][TILE_VARIANTS][4];
    WBitmap *m_ground;
    int      m_groundX, m_groundY;
    WBitmap *m_mini;
    int      m_miniW, m_miniH;
    WBitmap *m_big;
    int      m_bigW, m_bigH;

    WBitmap *m_deco[DECO_COUNT];
    int      m_decoW[DECO_COUNT], m_decoH[DECO_COUNT];
    int      m_decoAX[DECO_COUNT], m_decoAY[DECO_COUNT], m_decoUp[DECO_COUNT];

    WBitmap *m_hero[FACE_COUNT][HERO_FRAMES];

    WBitmap *m_shadow[SHADOW_TIERS];
    WBitmap *m_glow[GLOW_FRAMES];
    WBitmap *m_spark[GLOW_FRAMES];
    WBitmap *m_vignette;

    WBitmap *m_label[GF_LABEL_COUNT];
    int      m_labelW[GF_LABEL_COUNT];
    WBitmap *m_ascii[GF_ASCII_COUNT];
    int      m_asciiW[GF_ASCII_COUNT];
    WBitmap *m_corner[CORNER_COUNT];
    WBitmap *m_cloud[CLOUD_VARIANTS];
    WBitmap *m_seal;
    WBitmap *m_plaque;
    int      m_plaqueW, m_plaqueH;

    bool m_ok;
};

/* ---------------------------------------------------------------- helpers */
/*  Exposed because the tests assert on the raw numbers rather than on
 *  pictures: a dither that silently became a hard edge, or a diamond mask that
 *  silently stopped tiling, is much easier to catch as a count than as a
 *  colour. */

/* Which of the TILE_VARIANTS a tile uses.  A hash, not the coordinate modulo:
 *  modulo would put the same variant on every tile of a diagonal row and the
 *  repeat would be obvious. */
int TileVariant(int tx, int ty);

/* True when the pixel at (lx, ly) of a 64x32 diamond box belongs to the tile.
 * Measured at the pixel CENTRE (lx - 31.5, ly - 15.5) so that neighbouring
 * diamonds partition the plane exactly: the test value is always a
 * half-integer and can never tie against the threshold. */
bool InDiamond(int lx, int ly);

/* The procedural ground texture.  Deterministic, pure, and the single source
 * of truth for both the tileset bitmaps and the composed sheet. */
unsigned long TileTexel(int tile, int lx, int ly, int variant);

/* The dithered transition weight: 0 at the band's inner limit, 255 at the
 * edge.  `dir` is the tile-space neighbour: 0 = +x, 1 = +y, 2 = -x, 3 = -y. */
int EdgeWeight(int lx, int ly, int dir);

} /* namespace gf */

#endif /* GF_ART_H */
