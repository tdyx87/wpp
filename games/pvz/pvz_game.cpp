/* ===========================================================================
 *  pvz_game.cpp -- Lawn Defense : window, frame loop and input
 * ---------------------------------------------------------------------------
 *  The thin platform shell around pvz_core (the lawn), pvz_art (the cartoon
 *  atlas) and pvz_render (drawing).  Frame loop:
 *
 *      WApplication::Excute()
 *        -> AfterCreate()         bake the atlas, create canvas + present target
 *        -> loop { PeekMessage / Idle() }
 *
 *  Presentation
 *  ------------
 *  The renderer always draws into a fixed 800x600 canvas, but the window is
 *  free to be any size at all: it is resizable, maximisable, and F11 takes it
 *  borderless full-screen.  Every frame is therefore copied into an off-screen
 *  800x600 DIB and stretched, centred and aspect-correct, into the client
 *  area; the leftover is letterbox.  Z cycles the scale mode -- fill the window
 *  with the fast nearest-neighbour blit (the default), fill it with GDI's
 *  bilinear filter, or snap to a whole-number scale that is pixel-exact.
 *  Mouse positions are mapped back through the same rectangle, so a seed
 *  packet lands under the cursor at every window size.
 *
 *  Nothing here uses the engine's DirectDraw surface: an 800x600 frame is
 *  trivial for GDI, and unlike the surface blit a GDI blit can rescale, which
 *  is the whole point.
 *
 *  Screens:
 *      TITLE   the lawn behind a dim, the gold plaque and the two-item menu
 *      HOWTO   the controls page and the seed-packet legend
 *      GAME    the lawn; win/lose is drawn inside pvz_render when the lawn's
 *              phase flips
 *
 *  Controls:
 *      title      Up/Down pick, Enter confirm
 *      howto      Enter/Esc back
 *      play       left-click a seed packet to select it, then left-click an
 *                 empty lawn cell to plant it (if you can afford it)
 *                 R             restart the level
 *                 Esc           leave to the title, then quit
 *      anywhere   Z             scale mode: fill / fill-smooth / pixel-exact
 *                 F11           borderless full-screen
 * ===========================================================================
 */
#include <windows.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"
#include "wsapp.h"

#include "pvz_core.h"
#include "pvz_art.h"
#include "pvz_render.h"
#include "pvz_present.h"

using namespace pvz;

static int keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
}

/*  The client size to open at: the largest of 1x / 1.5x / 2x that still leaves
    the window frame inside the desktop work area.  The window can be resized
    from there, so this is only the starting point. */
static void PreferredClientSize(int &w, int &h)
{
    RECT wa;
    wa.left = wa.top = 0;
    wa.right  = ::GetSystemMetrics(SM_CXSCREEN);
    wa.bottom = ::GetSystemMetrics(SM_CYSCREEN);
    ::SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);

    const int room_w = (wa.right - wa.left) - 24;
    const int room_h = (wa.bottom - wa.top) - 64;        /* room for the frame */

    int num = 1, den = 1;                                /* 1x by default */
    if (room_w >= FIELD_W * 2 && room_h >= FIELD_H * 2)
        num = 2, den = 1;                                /* 2x */
    else if (room_w * 2 >= FIELD_W * 3 && room_h * 2 >= FIELD_H * 3)
        num = 3, den = 2;                                /* 1.5x */

    w = FIELD_W * num / den;
    h = FIELD_H * num / den;
}

/* ------------------------------------------------------------------- class */

class LawnDef : public WApplication {
public:
    LawnDef(HINSTANCE hInst);
    ~LawnDef();

    bool CreateMainWindow();
    void AfterCreate();
    void Idle();

    int OnPaint(DWORD w, DWORD l);
    int OnErase(DWORD w, DWORD l);
    int OnSize(DWORD w, DWORD l);
    int OnMinMax(DWORD w, DWORD l);

    MAP_MESSAGE_FUNC()

private:
    enum Screen { SCR_TITLE = 0, SCR_HOWTO, SCR_GAME };
    enum Action { ACT_UP = 0, ACT_DOWN, ACT_OK, ACT_BACK, ACT_PLANT, ACT_RESET,
                  ACT_ZOOM, ACT_FULL, ACT_COUNT };
    /*  Z cycles these; SCALE_FIT is the default because it fills the window
        and the nearest-neighbour path is both the sharpest and the cheapest. */
    enum ScaleMode { SCALE_FIT = 0, SCALE_SMOOTH, SCALE_PIXEL, SCALE_COUNT };

    void pollMouse();
    int  ActionDown(int act, int vk1, int vk2 = 0, int vk3 = 0);
    void stepTitle();
    void stepHowto();

    void CreatePresentTarget();
    void ReleasePresentTarget();
    void Layout();
    void PresentFrame(int paced);
    void ToggleScale();
    void ToggleFullScreen();

    WCanvas  *m_canvas;
    Lawn      m_lawn;
    Atlas     m_atlas;
    Renderer  m_renderer;
    int       m_screen;
    int       m_menuSel;
    unsigned  m_tick;
    int       m_prevAct[ACT_COUNT];
    int       m_mx, m_my, m_click;
    int       m_collectFlash;

    /* the window is not the canvas: this is how the two are reconciled */
    HDC           m_memDC;          /* off-screen 800x600 frame, GDI */
    HBITMAP       m_dib;
    HGDIOBJ       m_dibOld;
    PresentRect   m_rect;           /* where the frame goes in the client area */
    int           m_scaleMode;      /* ScaleMode */
    int           m_full;           /* borderless full-screen on */
    RECT          m_savedRect;
    DWORD         m_savedStyle;
};

BEGIN_MAP_MESSAGE(LawnDef, WApplication)
    MAP_MESSAGE(WM_PAINT,         LawnDef::OnPaint)
    MAP_MESSAGE(WM_ERASEBKGND,    LawnDef::OnErase)
    MAP_MESSAGE(WM_SIZE,          LawnDef::OnSize)
    MAP_MESSAGE(WM_GETMINMAXINFO, LawnDef::OnMinMax)
END_MAP_MESSAGE()

/* --------------------------------------------------------------- lifecycle */

LawnDef::LawnDef(HINSTANCE hInst)
    : WApplication(hInst),
      m_canvas(0),
      m_screen(SCR_TITLE),
      m_menuSel(0),
      m_tick(0),
      m_mx(0), m_my(0), m_click(0), m_collectFlash(0),
      m_memDC(0), m_dib(0), m_dibOld(0),
      m_scaleMode(SCALE_FIT), m_full(0), m_savedStyle(0)
{
    for (int i = 0; i < ACT_COUNT; ++i) m_prevAct[i] = 0;
    m_rect.x = m_rect.y = 0;
    m_rect.w = FIELD_W;
    m_rect.h = FIELD_H;
    ::SetRect(&m_savedRect, 0, 0, FIELD_W, FIELD_H);
}

LawnDef::~LawnDef()
{
    ReleasePresentTarget();
    delete m_canvas;
}

bool LawnDef::CreateMainWindow()
{
    /*  Resizable and maximisable, unlike the fixed 800x600 window this used to
        open: the client area is what the game is scaled into, so a bigger one
        is simply a bigger game.  Size the *outer* window so the client starts
        at exactly the size we asked for -- CreateWindow sizes the frame, not
        the client, and the old code was quietly 16x39 pixels short, clipping
        the right edge and the bottom row of the lawn. */
    int cw, ch;
    PreferredClientSize(cw, ch);

    RECT rc;
    ::SetRect(&rc, 0, 0, cw, ch);
    ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    return Create("Lawn Defense - cartoon lawn vs zombies  [Z] scale  [F11] full",
                  WS_OVERLAPPEDWINDOW,
                  rc.right - rc.left, rc.bottom - rc.top);
}

void LawnDef::AfterCreate()
{
    m_canvas = new WCanvas(FIELD_W, FIELD_H);
    m_canvas->Reset(60);

    CreatePresentTarget();
    Layout();

    m_lawn.Reset(7u);
    m_atlas.Build();                         /* bakes everything */
    m_renderer.Reset();
}

/* ------------------------------------------------------------- presentation */

void LawnDef::CreatePresentTarget()
{
    HDC screen = GetDC();
    m_memDC = ::CreateCompatibleDC(screen);

    /*  32bpp: the canvas is RGB565 and has to be converted somewhere, and a
        32bpp DIB is the format GDI's bilinear stretch is happiest with. */
    BITMAPINFO bmi;
    ::ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = FIELD_W;
    bmi.bmiHeader.biHeight      = -FIELD_H;              /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = 0;
    m_dib = ::CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, 0, 0);
    if (m_dib) m_dibOld = ::SelectObject(m_memDC, m_dib);
    ReleaseDC(screen);
}

void LawnDef::ReleasePresentTarget()
{
    if (m_dib) {
        if (m_dibOld) ::SelectObject(m_memDC, m_dibOld);
        ::DeleteObject(m_dib);
        m_dib = 0;
        m_dibOld = 0;
    }
    if (m_memDC) {
        ::DeleteDC(m_memDC);
        m_memDC = 0;
    }
}

void LawnDef::Layout()
{
    RECT rc;
    ::GetClientRect(hWnd, &rc);
    m_rect = PresentFit(rc.right - rc.left, rc.bottom - rc.top,
                        FIELD_W, FIELD_H, m_scaleMode != SCALE_PIXEL);
}

/*  One frame to the screen.  `paced` honours the 60 Hz limiter -- it is off
    for the redraws that arrive from WM_SIZE, where waiting would stall the
    modal resize loop the player is dragging. */
void LawnDef::PresentFrame(int paced)
{
    if (!m_canvas || !m_memDC) return;

    if (paced) m_canvas->SleepFrame();
    m_canvas->Update();
    m_canvas->SetPos(0, 0);
    m_canvas->Update2DC(m_memDC, 0, 0);

    HDC hdc = GetDC();
    RECT rc;
    ::GetClientRect(hWnd, &rc);
    ::FillRect(hdc, &rc, (HBRUSH)::GetStockObject(BLACK_BRUSH));

    if (m_rect.w == FIELD_W && m_rect.h == FIELD_H) {
        ::BitBlt(hdc, m_rect.x, m_rect.y, FIELD_W, FIELD_H,
                 m_memDC, 0, 0, SRCCOPY);
    } else {
        ::SetStretchBltMode(hdc, m_scaleMode == SCALE_SMOOTH ? HALFTONE
                                                             : COLORONCOLOR);
        ::SetBrushOrgEx(hdc, 0, 0, 0);
        ::StretchBlt(hdc, m_rect.x, m_rect.y, m_rect.w, m_rect.h,
                     m_memDC, 0, 0, FIELD_W, FIELD_H, SRCCOPY);
        ::SetStretchBltMode(hdc, COLORONCOLOR);
    }
    ReleaseDC(hdc);

    m_canvas->NextFrame();
}

void LawnDef::ToggleScale()
{
    m_scaleMode = (m_scaleMode + 1) % SCALE_COUNT;
    Layout();
}

void LawnDef::ToggleFullScreen()
{
    if (!m_full) {
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        HMONITOR mon = ::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        if (!::GetMonitorInfo(mon, &mi)) return;

        ::GetWindowRect(hWnd, &m_savedRect);
        m_savedStyle = (DWORD)::GetWindowLong(hWnd, GWL_STYLE);
        ::SetWindowLong(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        ::SetWindowPos(hWnd, HWND_TOP,
                       mi.rcMonitor.left, mi.rcMonitor.top,
                       mi.rcMonitor.right - mi.rcMonitor.left,
                       mi.rcMonitor.bottom - mi.rcMonitor.top,
                       SWP_FRAMECHANGED | SWP_NOZORDER);
        m_full = 1;
    } else {
        ::SetWindowLong(hWnd, GWL_STYLE, m_savedStyle);
        ::SetWindowPos(hWnd, HWND_TOP,
                       m_savedRect.left, m_savedRect.top,
                       m_savedRect.right - m_savedRect.left,
                       m_savedRect.bottom - m_savedRect.top,
                       SWP_FRAMECHANGED | SWP_NOZORDER);
        m_full = 0;
    }
    Layout();
    PresentFrame(0);
}

/* ---------------------------------------------------------------- messages */

int LawnDef::OnPaint(DWORD, DWORD)
{
    PAINTSTRUCT ps;
    BeginPaint(hWnd, &ps);
    EndPaint(hWnd, &ps);
    return -1;
}

int LawnDef::OnErase(DWORD, DWORD)
{
    return -1;                              /* never let Windows erase a frame */
}

int LawnDef::OnSize(DWORD, DWORD)
{
    if (!m_canvas) return -1;               /* before AfterCreate */
    Layout();
    PresentFrame(0);                        /* redraw while the border is dragged */
    return -1;
}

int LawnDef::OnMinMax(DWORD, DWORD l)
{
    /*  Never smaller than the canvas: the game can be scaled up freely, but
        shrinking below 1:1 would only throw pixels away. */
    MINMAXINFO *mmi = (MINMAXINFO *)l;
    RECT rc;
    ::SetRect(&rc, 0, 0, FIELD_W, FIELD_H);
    ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    mmi->ptMinTrackSize.x = rc.right - rc.left;
    mmi->ptMinTrackSize.y = rc.bottom - rc.top;
    return -1;
}

/* ------------------------------------------------------------------- input */

void LawnDef::pollMouse()
{
    POINT pt;
    ::GetCursorPos(&pt);
    ::ScreenToClient(hWnd, &pt);

    const int now = keyDown(VK_LBUTTON);
    const int edge = (now && !m_prevAct[ACT_PLANT]);
    m_prevAct[ACT_PLANT] = now;

    /*  A click in the letterbox is not a click on the lawn, and it must not
        become one when the window is scaled. */
    if (!PresentUnmap(pt.x, pt.y, m_rect, FIELD_W, FIELD_H, m_mx, m_my)) {
        m_mx = -1;
        m_my = -1;
        m_click = 0;
        return;
    }
    m_click = edge;                         /* edge only: one plant per click */
}

/* True only on the frame one of this action's keys goes down.  All of the
   action's keys are sampled in one call, so a held key never repeats. */
int LawnDef::ActionDown(int act, int vk1, int vk2, int vk3)
{
    int now = keyDown(vk1);
    if (!now && vk2) now = keyDown(vk2);
    if (!now && vk3) now = keyDown(vk3);
    const int hit = (now && !m_prevAct[act]);
    m_prevAct[act] = now;
    return hit;
}

void LawnDef::stepTitle()
{
    if (ActionDown(ACT_UP,   VK_UP,   'W')) m_menuSel = 0;
    if (ActionDown(ACT_DOWN, VK_DOWN, 'S')) m_menuSel = 1;
    if (ActionDown(ACT_OK, VK_RETURN, 'J', VK_SPACE)) {
        if (m_menuSel == 0) { m_lawn.Reset(7u); m_screen = SCR_GAME; }
        else                  m_screen = SCR_HOWTO;
    }
}

void LawnDef::stepHowto()
{
    if (ActionDown(ACT_OK, VK_RETURN, 'J', VK_SPACE)) m_screen = SCR_TITLE;
}

/* --------------------------------------------------------------- frame loop */

void LawnDef::Idle()
{
    ++m_tick;
    pollMouse();

    /* window furniture, available on every screen */
    if (ActionDown(ACT_ZOOM, 'Z'))    ToggleScale();
    if (ActionDown(ACT_FULL, VK_F11)) ToggleFullScreen();

    /* global Esc: leave the game, then quit from the title */
    {
        const int now = keyDown(VK_ESCAPE);
        const int hit = (now && !m_prevAct[ACT_BACK]);
        m_prevAct[ACT_BACK] = now;
        if (hit) {
            if (m_screen == SCR_GAME || m_screen == SCR_HOWTO) m_screen = SCR_TITLE;
            else Quit();
        }
    }

    switch (m_screen) {
    case SCR_TITLE:
        stepTitle();
        m_renderer.DrawTitle(*m_canvas, m_atlas, m_tick, m_menuSel);
        break;

    case SCR_HOWTO:
        stepHowto();
        m_renderer.DrawHowto(*m_canvas, m_atlas);
        break;

    default: {                              /* SCR_GAME */
        m_lawn.Step();
        if (m_click) {
            /* Collection has priority over planting: a player clicking a sun
               should never accidentally select a packet or plant underneath. */
            if (m_lawn.TryCollectSun(m_mx, m_my)) {
                m_collectFlash = 18;
            } else {
                const int seed = m_lawn.SeedIdx(m_mx, m_my);
                if (seed >= 0) {
                    m_lawn.SetSel(seed + 1);    /* seeds are PlantKind seed+1 */
                } else {
                    int c, r;
                    if (m_lawn.CellAt(m_mx, m_my, c, r)) m_lawn.TryPlant(c, r);
                }
            }
        }
        if (m_collectFlash > 0) --m_collectFlash;
        if (ActionDown(ACT_RESET, 'R')) m_lawn.Reset(7u);
        m_renderer.Draw(*m_canvas, m_lawn, m_atlas);
        break;
    }
    }

    PresentFrame(1);
}

/* ------------------------------------------------------------------- entry */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   char *lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    LawnDef app(hInstance);
    if (!app.CreateMainWindow()) return 1;
    app.Excute();
    return 0;
}
