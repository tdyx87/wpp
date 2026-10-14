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
};

} /* namespace wf */

#endif /* WF_RENDER_H */
