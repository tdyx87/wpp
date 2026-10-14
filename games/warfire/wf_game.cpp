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
 *  Screens (the shell owns which one is up, the renderer owns how it looks):
 *      TITLE      animated menu: 开始游戏 / 操作说明 (+ best run so far)
 *      HOWTO      the controls page
 *      GAME       the run itself; wf_core's AppState decides what is on top:
 *                  APP_PLAY    the simulation runs
 *                  APP_LEVELUP frozen, pick one of three perks
 *                  APP_DEAD / APP_WIN   frozen, run summary panel
 *
 *  Controls:
 *      title      Up/Down or 1/2 pick, Enter/J/Space confirm, Esc quits
 *      howto      Enter/J/Space/Esc back
 *      play       WASD / arrows   move
 *                 J / Space / LMB attack
 *                 K / Shift / RMB dash
 *                 L / Ctrl        whirlwind
 *                 M               enlarged full map (pauses while open)
 *      level up   1/2/3 pick, or Left/Right + Enter/J
 *      summary    R restart, Enter/J back to the title, Esc quits
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
    enum Screen { SCR_TITLE = 0, SCR_HOWTO, SCR_GAME };

    /* Menu / confirm actions, one slot each.  A slot is "down" if ANY of its
       physical keys is down, so releasing one key can never release another
       action, and a held key never repeats. */
    enum Action {
        ACT_UP = 0, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
        ACT_OK,           /* Enter / J / Space */
        ACT_ONE, ACT_TWO, ACT_THREE,
        ACT_RESTART,      /* R */
        ACT_BACK,         /* Esc, in the menus */
        ACT_COUNT
    };

    void PollKeys();
    int  ActionDown(int act, int vk1, int vk2 = 0, int vk3 = 0,
                    int vk4 = 0, int vk5 = 0);
    void stepTitle();
    void stepHowto();
    void stepGame();
    void startRun();

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

    int       m_screen;              /* SCR_TITLE / SCR_HOWTO / SCR_GAME */
    int       m_menuSel;             /* title cursor                    */
    int       m_perkSel;             /* perk card cursor                */
    unsigned  m_tick;                /* frame counter, drives the menu  */
    int       m_prevAct[ACT_COUNT];  /* per-action edge memory          */
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
      m_prevMap(0),
      m_screen(SCR_TITLE),
      m_menuSel(0),
      m_perkSel(0),
      m_tick(0)
{
    for (int i = 0; i < ACT_COUNT; ++i) m_prevAct[i] = 0;
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

    /* the save file carries the best run across sessions; Reset() below keeps
       it, so load first and the title menu shows the real record at once */
    m_world.EnableSave(1);
    m_world.LoadSave();
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

/* ------------------------------------------------------------ title menu */

/* True only on the frame one of this action's keys goes down.  All of the
   action's keys must be sampled in one call: splitting them would let the
   second call release the slot the first one just set, and a held key would
   then fire again on the next frame. */
int Warfire::ActionDown(int act, int vk1, int vk2, int vk3, int vk4, int vk5)
{
    int now = keyDown(vk1);
    if (!now && vk2) now = keyDown(vk2);
    if (!now && vk3) now = keyDown(vk3);
    if (!now && vk4) now = keyDown(vk4);
    if (!now && vk5) now = keyDown(vk5);
    int hit = (now && !m_prevAct[act]);
    m_prevAct[act] = now;
    return hit;
}

void Warfire::stepTitle()
{
    if (ActionDown(ACT_UP,   VK_UP,   '1')) m_menuSel = 0;
    if (ActionDown(ACT_DOWN, VK_DOWN, '2')) m_menuSel = 1;

    if (ActionDown(ACT_OK, VK_RETURN, 'J', VK_SPACE)) {
        if (m_menuSel == 0) startRun();
        else                m_screen = SCR_HOWTO;
    }
}

void Warfire::startRun()
{
    m_world.Reset();                        /* keeps the best score          */
    m_renderer.Reset();
    m_prevDash  = 0;
    m_prevSpin  = 0;
    m_prevMap   = 0;
    m_mapOpen   = 0;
    m_perkSel   = 0;
    m_screen    = SCR_GAME;
}

void Warfire::stepHowto()
{
    if (ActionDown(ACT_BACK, VK_RETURN, 'J', VK_SPACE, VK_ESCAPE)) {
        m_screen = SCR_TITLE;
    }
}

/* ------------------------------------------------------------- the run */

void Warfire::stepGame()
{
    int app = m_world.App();

    /* ---- level up: frozen, three cards, one pick ------------------------ */
    if (app == APP_LEVELUP) {
        if (ActionDown(ACT_LEFT,  VK_LEFT))  { if (--m_perkSel < 0) m_perkSel = 2; }
        if (ActionDown(ACT_RIGHT, VK_RIGHT)) { if (++m_perkSel > 2) m_perkSel = 0; }
        if (ActionDown(ACT_UP,    VK_UP))    m_perkSel = 0;
        if (ActionDown(ACT_DOWN,  VK_DOWN))  m_perkSel = 2;
        if (ActionDown(ACT_ONE,   '1')) { m_world.ChoosePerk(0); m_perkSel = 0; }
        if (ActionDown(ACT_TWO,   '2')) { m_world.ChoosePerk(1); m_perkSel = 1; }
        if (ActionDown(ACT_THREE, '3')) { m_world.ChoosePerk(2); m_perkSel = 2; }
        if (ActionDown(ACT_OK, VK_RETURN, 'J', VK_SPACE)) {
            m_world.ChoosePerk(m_perkSel);
            m_perkSel = 0;
        }
        m_renderer.SetPerkSel(m_perkSel);
        return;                             /* the world stays frozen        */
    }

    /* ---- run over: summary panel --------------------------------------- */
    if (app == APP_DEAD || app == APP_WIN) {
        if (ActionDown(ACT_RESTART, 'R')) startRun();
        else if (ActionDown(ACT_BACK, VK_RETURN, 'J', VK_SPACE))
            m_screen = SCR_TITLE;
        return;
    }

    /* ---- normal play ---------------------------------------------------- */
    m_world.Step(m_input);
}

/* -------------------------------------------------------------- frame loop */

void Warfire::Idle()
{
    ++m_tick;
    PollKeys();

    if (keyDown(VK_ESCAPE)) {
        if (m_mapOpen)              m_mapOpen = 0;   /* leave the map first */
        else if (m_screen == SCR_GAME ||
                 m_screen == SCR_TITLE) { Quit(); return; }
        else                        m_screen = SCR_TITLE;
    }

    switch (m_screen) {
    case SCR_TITLE:
        stepTitle();
        m_renderer.DrawTitle(*m_canvas, m_world, m_atlas, m_tick, m_menuSel);
        break;

    case SCR_HOWTO:
        stepHowto();
        m_renderer.DrawHowto(*m_canvas, m_atlas);
        break;

    default:
        if (!m_mapOpen) stepGame();
        if (m_mapOpen) {
            /* enlarged map: freeze the world and study it */
            m_renderer.DrawMap(*m_canvas, m_world, m_atlas);
        } else {
            m_renderer.Draw(*m_canvas, m_world, m_atlas);
        }
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

    Warfire app(hInstance);
    if (!app.CreateMainWindow()) return 1;
    app.Excute();
    return 0;
}
