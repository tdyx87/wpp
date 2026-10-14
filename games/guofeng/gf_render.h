/* ===========================================================================
 *  gf_render.h -- Guofeng : renderer
 * ---------------------------------------------------------------------------
 *  Turns a Scene into draw calls on a WCanvas.  It never touches the window,
 *  the message loop or DirectDraw, so it can be driven headless: build a
 *  WCanvas, call Renderer::Draw(), then canvas.Update() + canvas.BlitTo() to
 *  read the pixels back (see tests/test_guofeng.cpp).
 *
 *  IMPORTANT -- draw order.
 *  WCanvas::Draw() pushes each link at the HEAD of the per-block list and
 *  Update() walks that list head to tail, so the link submitted FIRST is
 *  applied LAST and ends up on TOP.  Submission order is therefore the exact
 *  reverse of paint order, and everything below is written from the top layer
 *  down:
 *
 *      the HUD / the map panel    the read-outs, topmost
 *      the dim wash               freezes the world without dimming them
 *      the vignette               darkens the corners of the picture
 *      entities                   decorations and the actor, NEAREST FIRST
 *      the ground sheet           one blit of the whole baked terrain
 *
 *  "Nearest first" is the same inversion: the nearest thing is submitted
 *  first, so it is painted last and covers what stands behind it.  Get it
 *  backwards and a bamboo clump vanishes behind the hero walking past it.
 *
 *  The same rule governs a panel and its contents: the contents are submitted
 *  BEFORE the panel, so the panel's body fill (submitted last inside
 *  drawPanel) lands underneath them.  Draw the panel first and it paints over
 *  its own text.
 * ===========================================================================
 */
#ifndef GF_RENDER_H
#define GF_RENDER_H

#include "gf_core.h"

class WCanvas;
class WBitmap;

namespace gf {

class Atlas;

class Renderer {
public:
    Renderer();
    ~Renderer();

    void Reset();

    /* One frame of the village plus its HUD.  The caller still owns the
       Update() / Blit2Screen() step. */
    void Draw(WCanvas &canvas, const Scene &scene, const Atlas &atlas);

    /* The title card: the village behind a slow drift, dimmed, with the
       plaque and the menu on top. */
    void DrawTitle(WCanvas &canvas, const Scene &scene, const Atlas &atlas,
                   unsigned tick, int sel);

    /* The controls page, including a strip of the real tile bitmaps so the
       legend shows the atlas rather than a description of it. */
    void DrawHowto(WCanvas &canvas, const Scene &scene, const Atlas &atlas);

    /* Whether the M screen is up.  Draw() reads it back. */
    void SetShowMap(int on) { m_showMap = on ? 1 : 0; }
    int  ShowMap() const { return m_showMap; }

    /* The lanterns' additive light.  Space toggles it, so the one effect this
       library can do that a plain blitter cannot is something the user can
       turn off and on and see for themselves. */
    void SetGlow(int on) { m_glow = on ? 1 : 0; }
    int  Glow() const { return m_glow; }

    /* Number of draw calls produced by the last frame.  The tests assert on
       it to prove the ground is one blit and not a thousand. */
    int ItemCount() const { return m_count; }

    /* The same total, but for the frame that finished last.  The HUD's
       read-out needs this one: the world is submitted after the HUD, so at
       the moment the panel is drawn the current frame's total is not known
       yet and the running count is off by an order of magnitude. */
    int LastCount() const { return m_last; }

private:
    void drawScene   (WCanvas &cv, const Scene &s, const Atlas &a,
                      int camX, int camY);
    void drawEntities(WCanvas &cv, const Scene &s, const Atlas &a,
                      int camX, int camY);
    void drawVignette(WCanvas &cv, const Atlas &a);
    void drawHud     (WCanvas &cv, const Scene &s, const Atlas &a);
    void drawMinimap (WCanvas &cv, const Scene &s, const Atlas &a, int x, int y);
    void drawBigMap  (WCanvas &cv, const Scene &s, const Atlas &a);
    void drawTitleCard(WCanvas &cv, const Atlas &a, unsigned tick, int sel);
    void drawLegend  (WCanvas &cv, const Atlas &a, int x, int y);

    /* ---- chrome ---------------------------------------------------------
       ALL of these are submitted AFTER the contents they decorate.  That is
       not an oversight: see the header comment. */
    void blit     (WCanvas &cv, WBitmap &b, int x, int y, int style);
    /* Latin text and numerals out of the baked atlas, so that the key names
       and the read-outs share the Chinese labels' typeface. */
    void drawStr  (WCanvas &cv, const Atlas &a, const char *s, int x, int y);
    int  drawNum  (WCanvas &cv, const Atlas &a, int value, int x, int y);
    void drawText (WCanvas &cv, const Atlas &a, int id, int x, int y);
    void drawTextC(WCanvas &cv, const Atlas &a, int id, int cx, int y);
    /* The panel: four 回纹 corner scrolls, a dim gold outer keyline, a jade
       inner one, a lit top inner edge, a shaded bottom one, and a flat ink
       fill submitted LAST so it is the bottom layer of the group.  `ornate`
       drops the scrolls, for panels whose contents run right up to the edge
       (the minimap's thumbnail would paint over them). */
    void drawPanel(WCanvas &cv, const Atlas &a, int x, int y, int w, int h,
                   int ornate);
    /* One offset rectangle of near-black, submitted AFTER the panel it
       belongs to: the overlap then disappears under the panel and only the
       offset band shows, which is what keeps a dark panel from melting into
       a dark background. */
    void drawShadow(WCanvas &cv, int x, int y, int w, int h, int off);
    void drawRule (WCanvas &cv, const Atlas &a, int x, int y, int w);
    void drawButton(WCanvas &cv, const Atlas &a, int x, int y, int w, int h,
                    int id, int live);

    /* The "freeze the world" wash.  WBox::DrawFunc() only carries COPY /
       TRANS / BACK -- the additive and subtractive slots in its table are
       NULL, so a WBox blitted with BLIT_SUBTRACT silently draws nothing at
       all.  A WBitmap supports every mode, so the wash has to be a bitmap. */
    void ensureDim();
    void drawDim(WCanvas &cv, int passes);

    int textW(const Atlas &a, int id) const;

    int m_count;
    int m_last;
    int m_showMap;
    int m_glow;

    /* full-screen subtract sheet, built on first use (see drawDim) */
    WBitmap *m_dim;
};

} /* namespace gf */

#endif /* GF_RENDER_H */
