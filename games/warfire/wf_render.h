/* ===========================================================================
 *  wf_render.h -- Warfire : renderer
 * ---------------------------------------------------------------------------
 *  Turns a World into draw calls on a WCanvas.  It never touches the window,
 *  the message loop or DirectDraw, so it can be driven headless: build a
 *  WCanvas, call Renderer::Draw(), then canvas.Update() + canvas.BlitTo() to
 *  read the pixels back (see tests/test_warfire.cpp).
 *
 *  IMPORTANT -- draw order.
 *  WCanvas pushes each CANVAS_LINK at the head of a per-block list and Update()
 *  walks it head to tail, so the link submitted FIRST is applied LAST and ends
 *  up on top.  Every helper below therefore submits from the top layer down:
 *  HUD, then the vignette, then effects, then actors (nearest first), then
 *  drops, then terrain.  The vignette sits directly under the HUD on purpose
 *  so it can darken the world without dimming the read-outs.
 * ===========================================================================
 */
#ifndef WF_RENDER_H
#define WF_RENDER_H

#include "wf_core.h"

class WCanvas;
class WBitmap;

namespace wf {

class Atlas;

class Renderer {
public:
    Renderer();
    ~Renderer();

    void Reset();

    /* Build one frame into the canvas.  The caller still owns the
       Update() / Blit2Screen() step. */
    void Draw(WCanvas &canvas, const World &w, const Atlas &atlas);

    /* The enlarged "zoom out" map (M): the whole world at 10 px per tile, the
       pockets the hero can never walk into shaded out, plus every monster,
       drop, the hero and the camera box in detail.  Driven headless too. */
    void DrawMap(WCanvas &canvas, const World &w, const Atlas &atlas);

    /* ---- product shell screens -------------------------------------------
       Title menu, controls page, the level-up overlay and the run summary.
       They only touch WCanvas, so the tests render them headless exactly like
       the in-game frame.  Draw() itself dispatches the overlay and the run
       summary from World::App(), so the shell never has to know about them. */
    void DrawTitle(WCanvas &canvas, const World &w, const Atlas &atlas,
                   unsigned tick, int sel);
    void DrawHowto(WCanvas &canvas, const Atlas &atlas);

    /* The level-up overlay has two pages sharing one frame: the perk cards
       (tab 0) and the forge -- the gold sink -- on tab 1.  `sel` is the card
       the cursor is on.  Keeping them in one overlay is deliberate: the
       level-up pause is the only pause the player gets, so that is where the
       gold has to be spendable. */
    void DrawPerks(WCanvas &canvas, const World &w, const Atlas &atlas,
                   int sel, int tab);
    void DrawOver (WCanvas &canvas, const World &w, const Atlas &atlas, int won);

    /* Which perk card the shell's cursor is on, and which page of the
       overlay is up (Draw reads both back). */
    void SetPerkSel(int sel) { m_perkSel = sel; }
    void SetOverlayTab(int tab) { m_tab = tab; }

    /* Number of draw calls produced by the last Draw(); used by the tests to
       prove the frame stays inside its budget. */
    int ItemCount() const { return m_count; }

private:
    struct Ent {
        float y;
        int   kind;         /* 0 = monster, 1 = hero */
        int   idx;
    };

    void drawTerrain (WCanvas &cv, const World &w, const Atlas &atlas);
    void drawVignette(WCanvas &cv, const Atlas &atlas);
    void drawDrops   (WCanvas &cv, const World &w, const Atlas &atlas);
    void drawEntities(WCanvas &cv, const World &w, const Atlas &atlas);
    void drawEffects (WCanvas &cv, const World &w, const Atlas &atlas);
    void drawHud     (WCanvas &cv, const World &w, const Atlas &atlas);
    void drawMinimap (WCanvas &cv, const World &w, const Atlas &atlas);
    void buildMinimap(const World &w);
    void buildBig     (const World &w);
    void drawBig      (WCanvas &cv, const World &w, const Atlas &atlas);

    /* shell screens (see the public block above) */
    void drawBackdrop(WCanvas &cv, const Atlas &atlas, unsigned tick);
    void drawDiorama (WCanvas &cv, const Atlas &atlas);
    void drawClock   (WCanvas &cv, const Atlas &atlas, int seconds, int cx, int y);
    int  clockWidth  (const Atlas &atlas, int seconds) const;
    int  numWidth    (const Atlas &atlas, int set, int value) const;

    /* The "freeze the world" wash under an overlay.
       WBox::DrawFunc() only carries COPY / TRANS / BACK in its table -- the
       additive and subtractive slots are NULL, so a WBox blitted with
       BLIT_SUBTRACT silently draws nothing at all.  A WBitmap does support
       every mode, so the dim has to be a bitmap: one full-screen sheet,
       baked once, blitted with BLIT_SUBTRACT.  `passes` deepens it. */
    void ensureDim();
    void drawDim(WCanvas &cv, int passes);

    /* ---- shared UI chrome ------------------------------------------------
       One visual language for all five screens, kept in one place so they
       cannot drift apart:

         drawBevel()  a near-black keyline, a lit top inner edge and a shaded
                      bottom inner edge over a flat fill.  The lit/shade pair
                      is what makes a flat rectangle read as a solid surface
                      instead of a coloured block.
         drawBarEx()  a gradient tube: the fill is laid down one row at a
                      time from a bright top to a saturated bottom, with a
                      highlight row on top and faint ticks so a fraction can
                      be read at a glance.
         drawCool()   a cooldown that fills from the bottom, gold the moment
                      it is ready -- no text needed. */
    void drawBevel(WCanvas &cv, int x, int y, int w, int h,
                   unsigned long fill, unsigned long key,
                   unsigned long lit, unsigned long shade);
    void drawBarEx(WCanvas &cv, int x, int y, int w, int h, int fillW,
                   unsigned long top, unsigned long bot, int ticks);
    void drawRule (WCanvas &cv, int x, int y, int w, unsigned long col);
    void drawArrow(WCanvas &cv, const Atlas &atlas, int x, int y);
    void drawCoin (WCanvas &cv, const Atlas &atlas, int value,
                   int x, int y, int big);
    void drawPips (WCanvas &cv, int cx, int y, int filled, int total,
                   unsigned long col);
    void drawTab  (WCanvas &cv, const Atlas &atlas, int x, int y, int w, int h,
                   int labelId, int live);
    void drawCool (WCanvas &cv, const Atlas &atlas, int x, int y, int w, int h,
                   int labelId, int cd, int cdMax);
    void drawCardBevel(WCanvas &cv, int x, int y, int w, int h,
                       int live, int affordable, unsigned long accent);
    /* A drop shadow: one offset rectangle of near-black, submitted AFTER the
       panel it belongs to.  The overlap then disappears under the panel and
       only the offset band shows -- which is what stops a dark panel from
       melting into a dark, dimmed background. */
    void drawShadow(WCanvas &cv, int x, int y, int w, int h, int off);
    /* "攻击 34 -> 39" centred on cx; returns the width it used. */
    int  drawStatRow(WCanvas &cv, const Atlas &atlas, const World &w,
                     int perk, int cx, int y);

    void drawNumber(WCanvas &cv, const Atlas &atlas, int set, int value,
                    int sx, int sy, int centered);
    void blit(WCanvas &cv, WBitmap &bmp, int sx, int sy, int style);
    void blitCentered(WCanvas &cv, WBitmap &bmp, int cx, int y, int style);
    void blitShadow(WCanvas &cv, const Atlas &atlas, int tier, int cx, int footY);

    /* qsort comparator for m_ents; a plain function so it can be handed
       straight to qsort.  Kept private so Ent stays an implementation
       detail of this class. */
    static int cmpEntDesc(const void *a, const void *b);

    Ent m_ents[MAX_MONSTERS + 1];
    int m_count;
    int m_perkSel;                  /* perk card the cursor is on           */
    int m_tab;                      /* 0 = perk cards, 1 = the forge        */
    int m_ox, m_oy;             /* camera origin, shake already folded in */
    unsigned m_rng;

    /* Baked once from the (constant) tile map: the full-world terrain
       thumbnail.  The per-frame overlay (monsters, hero, camera box) is drawn
       straight onto the canvas, so only the terrain pays the bake cost. */
    WBitmap *m_mini;
    int      m_miniW, m_miniH;
    int      m_miniX, m_miniY;

    /* Same idea, bigger: the 10 px-per-tile world map for DrawMap(). */
    WBitmap *m_big;
    int      m_bigW, m_bigH;
    int      m_bigX, m_bigY;
    int      m_bigTile;              /* pixels per tile                 */
    int      m_bigHero;             /* hero tile the shading was baked for */

    /* full-screen subtract sheet, built on first use (see drawDim) */
    WBitmap *m_dim;
};

} /* namespace wf */

#endif /* WF_RENDER_H */
