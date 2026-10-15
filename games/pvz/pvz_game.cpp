/* ===========================================================================
 *  pvz_game.cpp -- Lawn Defense : window, frame loop and input
 * ---------------------------------------------------------------------------
 *  The thin platform shell around pvz_core (the lawn), pvz_art (the cartoon
 *  atlas) and pvz_render (drawing).  Frame loop:
 *
 *      WApplication::Excute()
 *        -> AfterCreate()         bake the atlas, create canvas + surface
 *        -> loop { PeekMessage / Idle() }
 *
 *  WSurface::Blit2Screen() performs the frame limiter, canvas.Update() and the
 *  present, and finishes with canvas.NextFrame().
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
 * ===========================================================================
 */
#include <windows.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"
#include "wsurface.h"
#include "wsapp.h"

#include "pvz_core.h"
#include "pvz_art.h"
#include "pvz_render.h"

using namespace pvz;

static int keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
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

    MAP_MESSAGE_FUNC()

private:
    enum Screen { SCR_TITLE = 0, SCR_HOWTO, SCR_GAME };
    enum Action { ACT_UP = 0, ACT_DOWN, ACT_OK, ACT_BACK, ACT_PLANT, ACT_RESET,
                  ACT_COUNT };

    void pollMouse();
    int  ActionDown(int act, int vk1, int vk2 = 0, int vk3 = 0);
    void stepTitle();
    void stepHowto();

    WCanvas  *m_canvas;
    WSurface  m_surface;
    Lawn      m_lawn;
    Atlas     m_atlas;
    Renderer  m_renderer;
    int       m_useGdi;
    int       m_screen;
    int       m_menuSel;
    unsigned  m_tick;
    int       m_prevAct[ACT_COUNT];
    int       m_mx, m_my, m_click;
    int       m_collectFlash;
};

BEGIN_MAP_MESSAGE(LawnDef, WApplication)
    MAP_MESSAGE(WM_PAINT,      LawnDef::OnPaint)
    MAP_MESSAGE(WM_ERASEBKGND, LawnDef::OnErase)
END_MAP_MESSAGE()

/* --------------------------------------------------------------- lifecycle */

LawnDef::LawnDef(HINSTANCE hInst)
    : WApplication(hInst),
      m_canvas(0),
      m_useGdi(0),
      m_screen(SCR_TITLE),
      m_menuSel(0),
      m_tick(0),
      m_mx(0), m_my(0), m_click(0), m_collectFlash(0)
{
    for (int i = 0; i < ACT_COUNT; ++i) m_prevAct[i] = 0;
}

LawnDef::~LawnDef()
{
    delete m_canvas;
}

bool LawnDef::CreateMainWindow()
{
    return Create("Lawn Defense - cartoon lawn vs zombies",
                  WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                  FIELD_W, FIELD_H);
}

void LawnDef::AfterCreate()
{
    m_canvas = new WCanvas(FIELD_W, FIELD_H);
    m_canvas->Reset(60);

    if (m_surface.Create(hWnd, *m_canvas) != WS_OK) {
        m_useGdi = 1;                       /* no DirectDraw: fall back to GDI */
    }

    m_lawn.Reset(7u);
    m_atlas.Build();                         /* bakes everything */
    m_renderer.Reset();
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

/* ------------------------------------------------------------------- input */

void LawnDef::pollMouse()
{
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hWnd, &pt);
    m_mx = pt.x;
    m_my = pt.y;
    const int now = keyDown(VK_LBUTTON);
    m_click = (now && !m_prevAct[ACT_PLANT]);   /* edge only: one plant per click */
    m_prevAct[ACT_PLANT] = now;
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
