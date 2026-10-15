/* ===========================================================================
 *  pvz_art.h -- Lawn Defense : procedural cartoon atlas
 * ---------------------------------------------------------------------------
 *  Every pixel is baked at start-up; there is not one image file in the
 *  project.  This snapshot of windsoul has no WZoomBitmap implementation
 *  (declared in include/wszoom.h, defined nowhere), no sprite cache and no
 *  colour matrix, so runtime scaling/rotation/alpha are all impossible: every
 *  sprite has to be baked at the size it is drawn.  That is fine here because
 *  the cartoon style WANTS hard-edged shapes, and hard edges are what a
 *  per-pixel baker draws best.
 *
 *  The look is a bright cel-shaded cartoon: flat saturated fills, a chunky
 *  1-2 px dark outline around every sprite, and a striped lawn.  The outline is
 *  produced by dilating the silhouette into the BLIT_MASK background a couple
 *  of passes and painting the dilation a near-black tone -- the green-screen
 *  trick, computed in software.  Transparency uses BLIT_MASK, whose key is
 *  magenta 0xF81F in RGB565 (see include/blit16.asm), NOT black, so every
 *  sprite is baked on a 0xFF00FF field and that field is drawn unseen.
 *
 *  ASCII only, so it compiles under the default (GBK) source code page; the
 *  Chinese labels come from pvz_text.h and the GDI face in bakeUi().
 * ===========================================================================
 */
#ifndef PVZ_ART_H
#define PVZ_ART_H

#include "pvz_core.h"
#include "pvz_text.h"      /* PVZ_LABEL_COUNT and the GBK label table */

class WBitmap;
class WFont;

namespace pvz {

enum {
    /* two walk frames and one chew frame per zombie */
    ZOMBIE_FRAMES = 2,
    ZOMBIE_EATS   = 2,          /* 0 walk, 1 chewing */

    /* the printable ASCII range 32..126, one bitmap per glyph */
    ASCII_FIRST = 32,
    ASCII_LAST  = 126,
    PVZ_ASCII_COUNT = ASCII_LAST - ASCII_FIRST + 1,

    PEA_W = 12,
    PEA_H = 12
};

class Atlas {
public:
    Atlas();
    ~Atlas();

    /* Bakes everything.  Returns false only if the GDI face for the labels
       could not be created; the rest is pure arithmetic. */
    bool Build();
    void Destroy();
    bool Ok() const { return m_ok; }

    /* ---- world -------------------------------------------------------- */
    WBitmap *Background() const { return m_bg; }    /* 800x600 lawn+bar   */
    WBitmap *House() const { return m_house; }      /* the left house      */

    /* ---- actors ------------------------------------------------------- */
    WBitmap *Plant(int kind) const;                 /* 1..PL_COUNT-1 */
    WBitmap *Zombie(int frame, int eating) const;   /* frame 0/1, eat 0/1 */
    WBitmap *Pea() const { return m_pea; }
    WBitmap *Shadow() const { return m_shadow; }    /* BLIT_SUBTRACT blob  */
    WBitmap *Sun() const { return m_sun; }          /* the HUD sun coin    */

    /* ---- ui ----------------------------------------------------------- */
    WBitmap *Seed(int i) const;                     /* seed packet, i 0..2 */
    WBitmap *Plaque(int which) const;               /* 0 title 1 win 2 lose */
    WBitmap *Label(int id) const;
    int      LabelW(int id) const;
    WBitmap *Ascii(int c) const;                    /* 32..126 */
    int      AsciiW(int c) const;

private:
    WBitmap *bakeBackground();
    WBitmap *bakeHouse();
    WBitmap *bakePeashooter();
    WBitmap *bakeSunflower();
    WBitmap *bakeWallnut();
    WBitmap *bakeZombie(int frame, int eating);
    WBitmap *bakePea();
    WBitmap *bakeShadow();
    WBitmap *bakeSun();
    WBitmap *bakeSeed(int i);
    WBitmap *bakePlaque(int which);
    bool     bakeUi();

    WBitmap *m_bg;
    WBitmap *m_house;
    WBitmap *m_plant[PL_COUNT];
    WBitmap *m_zombie[ZOMBIE_FRAMES][ZOMBIE_EATS];
    WBitmap *m_pea;
    WBitmap *m_shadow;
    WBitmap *m_sun;
    WBitmap *m_seed[PL_COUNT - 1];
    WBitmap *m_plaque[3];
    WBitmap *m_label[PVZ_LABEL_COUNT];
    int      m_labelW[PVZ_LABEL_COUNT];
    WBitmap *m_ascii[PVZ_ASCII_COUNT];
    int      m_asciiW[PVZ_ASCII_COUNT];

    bool m_ok;
};

/* ---------------------------------------------------------------- helpers
   Exposed because the tests assert on raw numbers: a mask field that stopped
   being 0xF81F, or an outline that silently vanished, is far easier to catch
   as a count than as a colour. */

/* A per-pixel grass speckle.  Deterministic and pure, the single source of
   truth for the baked lawn and its tests. */
unsigned long LawnSpeckle(int x, int y);

} /* namespace pvz */

#endif /* PVZ_ART_H */
