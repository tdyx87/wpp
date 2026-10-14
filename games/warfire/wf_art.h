/* ===========================================================================
 *  wf_art.h -- Warfire : sprite atlas
 * ---------------------------------------------------------------------------
 *  Every sprite in the game is generated at start-up, pixel by pixel, from
 *  geometric primitives.  No image files, no sprite framework (this snapshot
 *  of windsoul is missing wsprite/sptcache anyway) -- just shapes written
 *  straight into a WBitmap and an outline pass that gives them a crisp
 *  pixel-art silhouette.
 *
 *  Sprites are transparent around the edges: the atlas zeroes pixel (0,0) and
 *  the renderer blits with BLIT_MASK, which treats colour 0 as the mask.
 * ===========================================================================
 */
#ifndef WF_ART_H
#define WF_ART_H

#include "wf_core.h"
#include "wf_text.h"        /* WF_LABEL_COUNT and the GBK label table */

class WBitmap;
class WFont;

namespace wf {

enum {
    /* Largest sprite edge the baking grid has to hold.  It is NOT just the
       boss (36x36): the whirlwind shock ring is drawn at full spin range,
       which is the biggest thing in the game.  Sizing this too small
       silently overflows Grid and takes the process down. */
    SPR_MAX = 128
};

enum {
    /* The library has no sprite scaling, so the soft shadow comes in the three
       sizes the three actor sizes need: 0 hero/grunt/archer, 1 brute, 2 boss. */
    SHADOW_TIERS = 3
};

enum DigitSet {
    DIGIT_SMALL = 0,        /* normal damage, off-white                      */
    DIGIT_BIG   = 1,        /* critical hits, gold                           */
    DIGIT_HURT  = 2,        /* damage taken / healing, red                   */
    DIGIT_SET_COUNT
};

class Atlas {
public:
    Atlas();
    ~Atlas();

    /* Bakes everything.  Needs a GDI font for the digits and the Chinese
       labels, so it must run on a machine with a desktop.  Returns false if
       the font could not be created. */
    bool Build();
    void Destroy();

    bool Ok() const { return m_ok; }

    WBitmap *Tile(int id) const;
    WBitmap *Hero(int facing) const;
    WBitmap *Monster(int kind, int facing) const;
    WBitmap *HeroFlash(int facing) const;       /* additive hit flare       */
    WBitmap *MonsterFlash(int kind, int facing) const;
    WBitmap *Digit(int set, int d) const;
    WBitmap *Label(int id) const;
    WBitmap *Slash(int facing) const;
    WBitmap *Ring(int i) const;         /* 0 = small, 1 = large            */
    WBitmap *TitleArt() const { return m_title; }   /* 72 px menu headline */
    int      TitleW()   const { return m_titleW; }
    /* The run-summary headline at 48 px: 0 = WF_LBL_DEAD, 1 = WF_LBL_WIN.
       The 24 px label font is too small to carry the one line that ends a
       run, and scaling does not exist in this library, so it is baked. */
    WBitmap *BannerArt(int i) const;
    int      BannerW(int i) const;
    WBitmap *Spark() const;             /* a 3x3 additive dot              */
    /* A soft additive blob, used behind the menu headline.  The falloff is
       carried in the pixel values because BLIT_ADDITIVE has no alpha; the rim
       is pure black, so adding it leaves the sky untouched there. */
    WBitmap *Glow() const;
    WBitmap *Coin() const;
    WBitmap *Potion() const;
    WBitmap *Shadow(int tier) const;    /* soft blob under an actor       */
    int ShadowTierOf(int monsterKind) const;
    WBitmap *Vignette() const;          /* full-screen edge darkening     */
    int LabelW(int id) const;
    int DigitW(int set, int d) const;
    int DigitH(int set) const;

private:
    WBitmap *m_tile[TILE_COUNT];
    WBitmap *m_hero[FACE_COUNT];
    WBitmap *m_mon[MON_KIND_COUNT][FACE_COUNT];
    WBitmap *m_heroFlash[FACE_COUNT];
    WBitmap *m_monFlash[MON_KIND_COUNT][FACE_COUNT];
    WBitmap *m_digit[DIGIT_SET_COUNT][10];
    WBitmap *m_label[WF_LABEL_COUNT];
    WBitmap *m_slash[FACE_COUNT];
    WBitmap *m_ring[2];
    WBitmap *m_title;
    int      m_titleW;
    WBitmap *m_banner[2];           /* 0 = dead, 1 = win                    */
    int      m_bannerW[2];
    WBitmap *m_spark;
    WBitmap *m_glow;
    WBitmap *m_coin;
    WBitmap *m_potion;
    WBitmap *m_shadow[SHADOW_TIERS];
    WBitmap *m_vignette;
    int      m_labelW[WF_LABEL_COUNT];
    int      m_digitW[DIGIT_SET_COUNT][10];
    int      m_digitH[DIGIT_SET_COUNT];
    bool     m_ok;
};

} /* namespace wf */

#endif /* WF_ART_H */
