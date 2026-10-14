/* ===========================================================================
 *  nb_render.h -- Neon Barrage : renderer
 * ---------------------------------------------------------------------------
 *  Turns a World into draw calls on a WCanvas.  It never touches the window,
 *  the message loop or DirectDraw, so it can be driven headless: build a
 *  WCanvas, call Renderer::Draw(), then canvas.Update() + canvas.BlitTo() to
 *  read the resulting pixels back (see tests/test_neonbarrage.cpp).
 *
 *  IMPORTANT -- draw order.
 *  WCanvas keeps a per-block singly linked list that is pushed at the head,
 *  and Update() walks it head-to-tail.  The link submitted *first* is
 *  therefore applied *last*, i.e. it ends up on top.  This renderer builds a
 *  display list in ordinary painter order (bottom -> top) and then submits it
 *  in reverse so the visual result matches the intuitive ordering.
 *
 *  All primitives are solid WBitmap blocks.  WBox would be cheaper but only
 *  implements BLIT_COPY / BLIT_TRANS / BLIT_BACK; the additive blend that
 *  gives the neon look is only available on WBitmap.
 * ===========================================================================
 */
#ifndef NB_RENDER_H
#define NB_RENDER_H

#include "nb_core.h"

class WCanvas;
class WBitmap;

namespace nb {

class Renderer {
public:
    Renderer();
    ~Renderer();

    void Reset();

    /* Build one frame into the canvas.  The caller still owns the
       Update()/Blit2Screen() step. */
    void Draw(WCanvas &canvas, const World &w);

    /* Number of draw calls produced by the last Draw(); used by the tests to
       prove the display list stays inside its budget. */
    int ItemCount() const { return m_count; }

    /* Number of distinct solid blocks currently memoised. */
    int BlockCount() const { return m_blockCount; }

private:
    enum { MAX_ITEMS = 4096, MAX_BLOCKS = 96, STAR_COUNT = 96 };

    struct Item {
        int x, y;
        const WBitmap *bmp;
        unsigned char style;
    };

    struct Block {
        int w, h;
        unsigned long color;
        WBitmap *bmp;
    };

    struct Star {
        float x, y, speed;
        unsigned long color;
    };

    const WBitmap *block(int w, int h, unsigned long color);
    void push(int cx, int cy, int w, int h, unsigned long color, int style);

    void buildScene(const World &w);
    void buildPlayer(const World &w);
    void buildEnemies(const World &w);
    void buildBullets(const World &w);
    void buildShots(const World &w);
    void buildStars();
    void flush(WCanvas &canvas);
    void drawHud(WCanvas &canvas, const World &w);

    Item  m_items[MAX_ITEMS];
    int   m_count;

    Block m_blocks[MAX_BLOCKS];
    int   m_blockCount;

    Star  m_stars[STAR_COUNT];
    unsigned m_rng;

    /* Whole-scene shake offset, applied by push().  The HUD is drawn
       separately so it stays steady. */
    int m_offX, m_offY;
};

} /* namespace nb */

#endif /* NB_RENDER_H */
