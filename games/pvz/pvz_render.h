/* ===========================================================================
 *  pvz_render.h -- Lawn Defense : renderer
 * ---------------------------------------------------------------------------
 *  Turns a Lawn into draw calls on a WCanvas.  It never touches the window, the
 *  message loop or DirectDraw, so it can be driven headless: build a WCanvas,
 *  call Draw(), then canvas.Update() + canvas.BlitTo() to read the pixels back
 *  (see tests/test_pvz.cpp).
 *
 *  IMPORTANT -- draw order (the same contract as the other prototypes):
 *  WCanvas::Draw() pushes each link at the HEAD of the per-block list and
 *  Update() walks that list head to tail, so the link submitted FIRST is
 *  applied LAST and ends up on TOP.  Submission order is therefore the exact
 *  reverse of paint order.  Draw() submits from the top layer down:
 *
 *      the end overlay (win/lose)   -- dim + plaque, freezes the picture
 *      the HUD                      -- seed packets, sun, wave count
 *      splats, peas                 -- in front of the zombies
 *      zombie sprites
 *      zombie shadows               -- a subtract blob under each zombie
 *      plant sprites
 *      plant shadows
 *      the house                    -- far-left facade, behind everyone
 *      the lawn background          -- one blit, the bottom layer
 * ===========================================================================
 */
#ifndef PVZ_RENDER_H
#define PVZ_RENDER_H

#include "pvz_core.h"

class WCanvas;
class WBitmap;

namespace pvz {

class Atlas;

class Renderer {
public:
    Renderer();
    ~Renderer();

    void Reset();

    /* One frame of the lawn.  The caller still owns Update()/Blit2Screen(). */
    void Draw(WCanvas &canvas, const Lawn &lawn, const Atlas &atlas);

    /* The title card: the lawn dimmed behind the gold plaque and the menu. */
    void DrawTitle(WCanvas &canvas, const Atlas &atlas, unsigned tick, int sel);

    /* The controls / legend page. */
    void DrawHowto(WCanvas &canvas, const Atlas &atlas);

    /* Number of draw calls produced by the last frame. */
    int ItemCount() const { return m_count; }

private:
    void drawScene(WCanvas &cv, const Lawn &l, const Atlas &a);
    void drawHud  (WCanvas &cv, const Lawn &l, const Atlas &a);
    void drawEnd  (WCanvas &cv, const Lawn &l, const Atlas &a);

    /* ---- chrome ---- */
    void blit    (WCanvas &cv, WBitmap &b, int x, int y, int style);
    void box     (WCanvas &cv, int x, int y, int w, int h, unsigned long col, int style);
    void drawText (WCanvas &cv, const Atlas &a, int id, int x, int y);
    void drawTextC(WCanvas &cv, const Atlas &a, int id, int cx, int y);
    void drawStr  (WCanvas &cv, const Atlas &a, const char *s, int x, int y);
    int  drawNum  (WCanvas &cv, const Atlas &a, int value, int x, int y);

    /* The "dim the world" subtract sheet.  WBox cannot subtract (the slot is
       NULL), so this has to be a bitmap. */
    void ensureDim();
    void drawDim(WCanvas &cv, int passes);

    int      m_count;
    WBitmap *m_dim;
};

} /* namespace pvz */

#endif /* PVZ_RENDER_H */
