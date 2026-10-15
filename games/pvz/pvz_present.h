/* ===========================================================================
 *  pvz_present.h -- fitting the 800x600 frame into a window of any size
 * ---------------------------------------------------------------------------
 *  pvz_render always draws into a fixed 800x600 canvas.  The window, on the
 *  other hand, is whatever the player drags it to (and it can be maximised or
 *  taken borderless full-screen), so every frame has to be rescaled on the way
 *  out to the screen and every click mapped back the other way.
 *
 *  The frame keeps its 4:3 shape and is centred; whatever is left over is
 *  letterbox.  Three scale modes, cycled with Z:
 *
 *      fit      an arbitrary scale that fills the window as far as 4:3 allows,
 *               blitted with the fast nearest-neighbour path.  The default:
 *               on a 1080p desktop the frame comes out 1440x1080 and the
 *               outlines and labels stay crisp.
 *      smooth   the same rectangle, blitted with GDI's bilinear filter.  A
 *               little softer, and about 6x the cost of the fast path, so it
 *               is a taste rather than the default.
 *      pixel    a whole-number scale, so every canvas pixel lands on an exact
 *               square of screen pixels.  Sharpest of all, but on a 1080p
 *               desktop the largest whole multiple that fits is 1x, which is
 *               the small window again.
 *
 *  Deliberately free of windows.h: pvz_game.cpp uses it for the blit and for
 *  the mouse, and the unit test checks the arithmetic on its own.
 * ===========================================================================
 */
#ifndef _PVZ_PRESENT_H
#define _PVZ_PRESENT_H

namespace pvz {

struct PresentRect {
    int x, y, w, h;
};

/*  The destination rectangle that shows the whole canvas inside a cw x ch
    client area.  anyScale allows a fractional scale (fill the window);
    otherwise the scale is rounded down to a whole number, never below 1. */
inline PresentRect PresentFit(int cw, int ch, int canvasW, int canvasH, int anyScale)
{
    PresentRect r;
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    if (canvasW < 1) canvasW = 1;
    if (canvasH < 1) canvasH = 1;

    if (anyScale) {
        int w = cw, h = cw * canvasH / canvasW;
        if (h > ch) { h = ch; w = ch * canvasW / canvasH; }
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        r.w = w;
        r.h = h;
    } else {
        int s = cw / canvasW;
        if (ch / canvasH < s) s = ch / canvasH;
        if (s < 1) s = 1;
        r.w = canvasW * s;
        r.h = canvasH * s;
    }
    r.x = (cw - r.w) / 2;
    r.y = (ch - r.h) / 2;
    return r;
}

/*  Client point -> canvas point.  Returns 0 for the letterbox, which is not
    part of the picture and must not be treated as a click on the lawn. */
inline int PresentUnmap(int px, int py, const PresentRect &r,
                        int canvasW, int canvasH, int &cx, int &cy)
{
    if (r.w <= 0 || r.h <= 0) return 0;
    if (px < r.x || py < r.y || px >= r.x + r.w || py >= r.y + r.h) return 0;
    cx = (px - r.x) * canvasW / r.w;
    cy = (py - r.y) * canvasH / r.h;
    if (cx < 0) cx = 0; else if (cx >= canvasW) cx = canvasW - 1;
    if (cy < 0) cy = 0; else if (cy >= canvasH) cy = canvasH - 1;
    return 1;
}

}
#endif
