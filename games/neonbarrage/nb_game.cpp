/* ===========================================================================
 *  nb_game.cpp -- Neon Barrage : window, frame loop and input
 * ---------------------------------------------------------------------------
 *  Everything here is the thin platform shell around nb_core (simulation) and
 *  nb_render (drawing).  Frame loop:
 *
 *      WApplication::Excute()
 *        -> AfterCreate()          create canvas + DirectDraw surface
 *        -> loop { PeekMessage / Idle() }
 *
 *  WSurface::Blit2Screen() performs the frame limiter, canvas.Update() and the
 *  present, and finishes with canvas.NextFrame().
 * ===========================================================================
 */
#include <windows.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"
#include "wsurface.h"
#include "wsapp.h"

#include "nb_core.h"
#include "nb_render.h"

/* ------------------------------------------------------------------ class */

class NeonBarrage : public WApplication {
public:
    NeonBarrage(HINSTANCE hInst);
    ~NeonBarrage();

    bool CreateMainWindow();

    void AfterCreate();
    void Idle();

    int OnPaint(DWORD w, DWORD l);
    int OnErase(DWORD w, DWORD l);

    MAP_MESSAGE_FUNC()

private:
    void PollKeys();

    WCanvas     *m_canvas;
    WSurface     m_surface;
    nb::World    m_world;
    nb::Renderer m_renderer;
    nb::Input    m_input;
    int          m_prevBomb;
    int          m_useGdi;
};

BEGIN_MAP_MESSAGE(NeonBarrage, WApplication)
    MAP_MESSAGE(WM_PAINT,       NeonBarrage::OnPaint)
    MAP_MESSAGE(WM_ERASEBKGND,  NeonBarrage::OnErase)
END_MAP_MESSAGE()

/* -------------------------------------------------------------- lifecycle */

NeonBarrage::NeonBarrage(HINSTANCE hInst)
    : WApplication(hInst),
      m_canvas(0),
      m_prevBomb(0),
      m_useGdi(0)
{
    m_input.dx    = 0.0f;
    m_input.dy    = 0.0f;
    m_input.fire  = 0;
    m_input.bomb  = 0;
    m_input.focus = 0;
}

NeonBarrage::~NeonBarrage()
{
    delete m_canvas;
}

bool NeonBarrage::CreateMainWindow()
{
    /* Fixed-size client area: the playfield is exactly 640x480. */
    return Create("Neon Barrage",
                  WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                  nb::FIELD_W, nb::FIELD_H);
}

void NeonBarrage::AfterCreate()
{
    m_canvas = new WCanvas(nb::FIELD_W, nb::FIELD_H);
    m_canvas->Reset(60);                 /* target 60 frames per second */

    if (m_surface.Create(hWnd, *m_canvas) != WS_OK) {
        /* No DirectDraw (or no exclusive surface): fall back to GDI, which is
           slower but keeps the game playable everywhere. */
        m_useGdi = 1;
    }

    m_world.Reset();
    m_renderer.Reset();
}

/* ---------------------------------------------------------------- messages */

int NeonBarrage::OnPaint(DWORD, DWORD)
{
    PAINTSTRUCT ps;
    BeginPaint(hWnd, &ps);
    EndPaint(hWnd, &ps);
    return -1;                           /* -1 == handled, skip DefWindowProc */
}

int NeonBarrage::OnErase(DWORD, DWORD)
{
    return -1;                           /* never let Windows erase the frame */
}

/* ------------------------------------------------------------------- input */

void NeonBarrage::PollKeys()
{
    m_input.dx = 0.0f;
    m_input.dy = 0.0f;

    if (GetAsyncKeyState(VK_LEFT)  & 0x8000) m_input.dx -= 1.0f;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) m_input.dx += 1.0f;
    if (GetAsyncKeyState(VK_UP)    & 0x8000) m_input.dy -= 1.0f;
    if (GetAsyncKeyState(VK_DOWN)  & 0x8000) m_input.dy += 1.0f;

    m_input.focus = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
    m_input.fire  = (GetAsyncKeyState('Z')      & 0x8000) ? 1 : 0;

    /* Bomb is edge triggered: holding X must not spend every bomb at once. */
    {
        int bomb = (GetAsyncKeyState('X') & 0x8000) ? 1 : 0;
        m_input.bomb = (bomb && !m_prevBomb) ? 1 : 0;
        m_prevBomb   = bomb;
    }
}

/* -------------------------------------------------------------- frame loop */

void NeonBarrage::Idle()
{
    PollKeys();

    if (m_world.State().gameover) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
            m_world.Reset();
            m_renderer.Reset();
            m_prevBomb = 0;
        }
    }

    m_world.Step(m_input);
    m_renderer.Draw(*m_canvas, m_world);

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

    NeonBarrage app(hInstance);
    if (!app.CreateMainWindow()) return 1;
    app.Excute();
    return 0;
}
