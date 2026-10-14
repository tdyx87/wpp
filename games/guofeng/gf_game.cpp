/* ===========================================================================
 *  gf_game.cpp -- Guofeng : window, frame loop and input
 * ---------------------------------------------------------------------------
 *  The thin platform shell around gf_core (projection + scene), gf_art (the
 *  procedural atlas) and gf_render (drawing).  Frame loop:
 *
 *      WApplication::Excute()
 *        -> AfterCreate()          bake the atlas, create canvas + surface
 *        -> loop { PeekMessage / Idle() }
 *
 *  WSurface::Blit2Screen() performs the frame limiter, canvas.Update() and the
 *  present, and finishes with canvas.NextFrame().
 *
 *  Screens (the shell owns which one is up, the renderer owns how it looks):
 *      TITLE   the village behind a slow drift, dimmed, with the 匾额 and menu
 *      HOWTO   the controls page and the tile legend
 *      GAME    the village itself; M freezes it into the enlarged map
 *
 *  Controls:
 *      title      Up/Down pick, Enter confirm, Esc quits
 *      howto      Enter/Esc back
 *      play       W A S D / arrows   move (W walks towards the top of the
 *                                    picture, which in a 2:1 isometric is the
 *                                    tile diagonal, not the tile axis)
 *                 Shift              run
 *                 M                  the enlarged map (freezes the scene)
 *                 Space              toggle the lanterns' additive light
 *                 Esc                leave the map, then the game
 * ===========================================================================
 */
#include <windows.h>

#include "windsoul.h"
#include "wsbitmap.h"
#include "wscanvas.h"
#include "wsurface.h"
#include "wsapp.h"

#include "gf_core.h"
#include "gf_art.h"
#include "gf_render.h"

using namespace gf;

static int keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
}

/* ------------------------------------------------------------------ class */

class Guofeng : public WApplication {
public:
    Guofeng(HINSTANCE hInst);
    ~Guofeng();

    bool CreateMainWindow();

    void AfterCreate();
    void Idle();

    int OnPaint(DWORD w, DWORD l);
    int OnErase(DWORD w, DWORD l);

    MAP_MESSAGE_FUNC()

private:
    enum Screen { SCR_TITLE = 0, SCR_HOWTO, SCR_GAME };

    /* Menu / confirm actions, one slot each.  A slot is "down" if ANY of its
       physical keys is down, so releasing one key can never release another
       action, and a held key never repeats. */
    enum Action {
        ACT_UP = 0, ACT_DOWN, ACT_OK, ACT_MAP, ACT_GLOW, ACT_BACK, ACT_COUNT
    };

    void PollKeys();
    int  ActionDown(int act, int vk1, int vk2 = 0, int vk3 = 0);
    void stepTitle();
    void stepHowto();

    WCanvas  *m_canvas;
    WSurface  m_surface;
    Scene     m_scene;
    Atlas     m_atlas;
    Renderer  m_renderer;
    Input     m_input;
    int       m_useGdi;
    int       m_mapOpen;
    int       m_glow;
    int       m_screen;
    int       m_menuSel;
    unsigned  m_tick;
    int       m_prevAct[ACT_COUNT];
};

BEGIN_MAP_MESSAGE(Guofeng, WApplication)
    MAP_MESSAGE(WM_PAINT,      Guofeng::OnPaint)
    MAP_MESSAGE(WM_ERASEBKGND, Guofeng::OnErase)
END_MAP_MESSAGE()

/* -------------------------------------------------------------- lifecycle */

Guofeng::Guofeng(HINSTANCE hInst)
    : WApplication(hInst),
      m_canvas(0),
      m_useGdi(0),
      m_mapOpen(0),
      m_glow(1),
      m_screen(SCR_TITLE),
      m_menuSel(0),
      m_tick(0)
{
    for (int i = 0; i < ACT_COUNT; ++i) m_prevAct[i] = 0;

    m_input.up = m_input.down = m_input.left = m_input.right = 0;
    m_input.run = 0;
}

Guofeng::~Guofeng()
{
    delete m_canvas;
}

bool Guofeng::CreateMainWindow()
{
    /* fixed client area: the camera maths assumes exactly FIELD_W x FIELD_H */
    return Create("Guofeng - isometric village",
                  WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                  FIELD_W, FIELD_H);
}

void Guofeng::AfterCreate()
{
    m_canvas = new WCanvas(FIELD_W, FIELD_H);
    m_canvas->Reset(60);

    if (m_surface.Create(hWnd, *m_canvas) != WS_OK) {
        m_useGdi = 1;                       /* no DirectDraw: fall back to GDI */
    }

    m_scene.Reset(7u);
    m_atlas.Build(m_scene);                 /* bakes the ground and every sprite */
    m_renderer.Reset();
}

/* ---------------------------------------------------------------- messages */

int Guofeng::OnPaint(DWORD, DWORD)
{
    PAINTSTRUCT ps;
    BeginPaint(hWnd, &ps);
    EndPaint(hWnd, &ps);
    return -1;
}

int Guofeng::OnErase(DWORD, DWORD)
{
    return -1;                              /* never let Windows erase a frame */
}

/* ------------------------------------------------------------------- input */

void Guofeng::PollKeys()
{
    m_input.up    = (keyDown('W') || keyDown(VK_UP))    ? 1 : 0;
    m_input.down  = (keyDown('S') || keyDown(VK_DOWN))  ? 1 : 0;
    m_input.left  = (keyDown('A') || keyDown(VK_LEFT))  ? 1 : 0;
    m_input.right = (keyDown('D') || keyDown(VK_RIGHT)) ? 1 : 0;
    m_input.run   = keyDown(VK_SHIFT) ? 1 : 0;

    /* M and Space are edge triggered: holding either would otherwise flip the
       map open and shut, or the glow on and off, sixty times a second */
    if (ActionDown(ACT_MAP, 'M')) m_mapOpen ^= 1;
    if (ActionDown(ACT_GLOW, VK_SPACE)) m_glow ^= 1;
}

/* True only on the frame one of this action's keys goes down.  All of the
   action's keys must be sampled in one call: splitting them would let the
   second call release the slot the first one just set, and a held key would
   then fire again on the next frame. */
int Guofeng::ActionDown(int act, int vk1, int vk2, int vk3)
{
    int now = keyDown(vk1);
    if (!now && vk2) now = keyDown(vk2);
    if (!now && vk3) now = keyDown(vk3);
    const int hit = (now && !m_prevAct[act]);
    m_prevAct[act] = now;
    return hit;
}

void Guofeng::stepTitle()
{
    if (ActionDown(ACT_UP,   VK_UP,   '1')) m_menuSel = 0;
    if (ActionDown(ACT_DOWN, VK_DOWN, '2')) m_menuSel = 1;

    if (ActionDown(ACT_OK, VK_RETURN, 'J')) {
        if (m_menuSel == 0) m_screen = SCR_GAME;
        else                m_screen = SCR_HOWTO;
    }
}

void Guofeng::stepHowto()
{
    if (ActionDown(ACT_BACK, VK_RETURN, 'J', VK_ESCAPE)) m_screen = SCR_TITLE;
}

/* -------------------------------------------------------------- frame loop */

void Guofeng::Idle()
{
    ++m_tick;
    PollKeys();

    if (keyDown(VK_ESCAPE) && !m_prevAct[ACT_BACK]) {
        if (m_mapOpen)                   m_mapOpen = 0;   /* leave the map first */
        else if (m_screen == SCR_GAME)   m_screen = SCR_TITLE;
        else                             Quit();
    }
    /* Esc is edge triggered through the same table the menus use, so the frame
       it is pressed is the only one that acts on it */
    m_prevAct[ACT_BACK] = keyDown(VK_ESCAPE);

    switch (m_screen) {
    case SCR_TITLE:
        stepTitle();
        m_renderer.DrawTitle(*m_canvas, m_scene, m_atlas, m_tick, m_menuSel);
        break;

    case SCR_HOWTO:
        stepHowto();
        m_renderer.DrawHowto(*m_canvas, m_scene, m_atlas);
        break;

    default:
        if (!m_mapOpen) m_scene.Step(m_input);   /* the map freezes the scene */
        m_renderer.SetShowMap(m_mapOpen);
        m_renderer.SetGlow(m_glow);
        m_renderer.Draw(*m_canvas, m_scene, m_atlas);
        break;
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

    Guofeng app(hInstance);
    if (!app.CreateMainWindow()) return 1;
    app.Excute();
    return 0;
}
