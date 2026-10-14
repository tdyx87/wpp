/* ===========================================================================
 *  wf_game.cpp -- Warfire : window, frame loop and input
 * ---------------------------------------------------------------------------
 *  The thin platform shell around wf_core (simulation), wf_art (sprites) and
 *  wf_render (drawing).  Frame loop:
 *
 *      WApplication::Excute()
 *        -> AfterCreate()          bake the atlas, create canvas + surface
 *        -> loop { PeekMessage / Idle() }
 *
 *  WSurface::Blit2Screen() performs the frame limiter, canvas.Update() and the
 *  present, and finishes with canvas.NextFrame().
 *
 *  Controls:
 *      WASD / arrows   move            (the hero accelerates, it is not a
 *                                       hard per-frame teleport)
 *      J / Space / LMB attack          (hold to chain the 3-hit combo)
 *      K / Shift / RMB dash
 *      L / Ctrl        whirlwind       (costs rage)
 *      M               enlarged full map (pauses while open, press M again
 *                       to return)
 *      R               restart after death
 *      Esc             quit
 * ===========================================================================
 */
#include <windows.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"
#include "wsurface.h"
#include "wsapp.h"

#include "wf_core.h"
#include "wf_art.h"
#include "wf_render.h"

using namespace wf;

static int keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
}

/* ------------------------------------------------------------------ class */

class Warfire : public WApplication {
public:
    Warfire(HINSTANCE hInst);
    ~Warfire();

    bool CreateMainWindow();

    void AfterCreate();
    void Idle();

    int OnPaint(DWORD w, DWORD l);
    int OnErase(DWORD w, DWORD l);

    MAP_MESSAGE_FUNC()

private:
    void PollKeys();

    WCanvas  *m_canvas;
    WSurface  m_surface;
    World     m_world;
    Atlas     m_atlas;
    Renderer  m_renderer;
    Input     m_input;
    int       m_prevDash;
    int       m_prevSpin;
    int       m_useGdi;
    int       m_mapOpen;             /* enlarged full-map view (M)     */
    int       m_prevMap;
};

BEGIN_MAP_MESSAGE(Warfire, WApplication)
    MAP_MESSAGE(WM_PAINT,      Warfire::OnPaint)
    MAP_MESSAGE(WM_ERASEBKGND, Warfire::OnErase)
END_MAP_MESSAGE()

/* -------------------------------------------------------------- lifecycle */

Warfire::Warfire(HINSTANCE hInst)
    : WApplication(hInst),
      m_canvas(0),
      m_prevDash(0),
      m_prevSpin(0),
      m_useGdi(0),
      m_mapOpen(0),
      m_prevMap(0)
{
    m_input.dx     = 0.0f;
    m_input.dy     = 0.0f;
    m_input.attack = 0;
    m_input.dash   = 0;
    m_input.spin   = 0;
}

Warfire::~Warfire()
{
    delete m_canvas;
}

bool Warfire::CreateMainWindow()
{
    /* fixed client area: the camera maths assumes exactly FIELD_W x FIELD_H */
    return Create("Warfire",
                  WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                  FIELD_W, FIELD_H);
}

void Warfire::AfterCreate()
{
    m_canvas = new WCanvas(FIELD_W, FIELD_H);
    m_canvas->Reset(60);

    if (m_surface.Create(hWnd, *m_canvas) != WS_OK) {
        m_useGdi = 1;                       /* no DirectDraw: fall back to GDI */
    }

    m_atlas.Build();                        /* bakes every sprite + the font  */
    m_world.Reset();
    m_renderer.Reset();
}

/* ---------------------------------------------------------------- messages */

int Warfire::OnPaint(DWORD, DWORD)
{
    PAINTSTRUCT ps;
    BeginPaint(hWnd, &ps);
    EndPaint(hWnd, &ps);
    return -1;
}

int Warfire::OnErase(DWORD, DWORD)
{
    return -1;                              /* never let Windows erase a frame */
}

/* ------------------------------------------------------------------- input */

void Warfire::PollKeys()
{
    float dx = 0.0f, dy = 0.0f;

    if (keyDown(VK_LEFT)  || keyDown('A')) dx -= 1.0f;
    if (keyDown(VK_RIGHT) || keyDown('D')) dx += 1.0f;
    if (keyDown(VK_UP)    || keyDown('W')) dy -= 1.0f;
    if (keyDown(VK_DOWN)  || keyDown('S')) dy += 1.0f;

    m_input.dx     = dx;
    m_input.dy     = dy;
    m_input.attack = (keyDown('J') || keyDown(VK_SPACE) ||
                      keyDown(VK_LBUTTON)) ? 1 : 0;

    /* dash and whirlwind are edge triggered so holding the key does not
       burn the cooldown over and over */
    {
        int dash = (keyDown('K') || keyDown(VK_SHIFT) || keyDown(VK_RBUTTON)) ? 1 : 0;
        m_input.dash = (dash && !m_prevDash) ? 1 : 0;
        m_prevDash   = dash;
    }
    {
        int spin = (keyDown('L') || keyDown(VK_CONTROL)) ? 1 : 0;
        m_input.spin = (spin && !m_prevSpin) ? 1 : 0;
        m_prevSpin   = spin;
    }
    {
        /* M toggles the enlarged full-map view; edge triggered so a held key
           does not flip it on and off every frame */
        int mp = keyDown('M') ? 1 : 0;
        if (mp && !m_prevMap) m_mapOpen ^= 1;
        m_prevMap = mp;
    }
}

/* -------------------------------------------------------------- frame loop */

void Warfire::Idle()
{
    PollKeys();

    if (keyDown(VK_ESCAPE)) { Quit(); return; }

    if (m_world.State().gameover && keyDown('R')) {
        m_world.Reset();
        m_renderer.Reset();
        m_prevDash = 0;
        m_prevSpin = 0;
        m_prevMap  = 0;
    }

    if (m_mapOpen) {
        /* enlarged map: freeze the world and study it */
        m_renderer.DrawMap(*m_canvas, m_world, m_atlas);
    } else {
        m_world.Step(m_input);
        m_renderer.Draw(*m_canvas, m_world, m_atlas);
    }

    if (m_useGdi) {
        m_canvas->Update();
        m_canvas->SetPos(0, 0);
        HDC hdc = GetDC();
        m_canvas->Update2DC(hdc, 0, 0);
        ReleaseDC(hdc);
        m_canvas->NextFrame();
    } else {
        m_surface.Blit2Screen();
    }
}

/* ------------------------------------------------------------------ entry */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   char *lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    Warfire app(hInstance);
    if (!app.CreateMainWindow()) return 1;
    app.Excute();
    return 0;
}
