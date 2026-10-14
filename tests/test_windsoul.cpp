/*
 * test_windsoul.cpp -- runtime smoke test for windsoul.dll
 *
 * Links against the generated windsoul.lib (implicit DLL load) and exercises
 * the subsystems that were touched during the optimisation rounds:
 *   WSTRING.CPP  -> [1][2]   string + Format (length estimation fix)
 *   timer.cpp    -> [3]      RDTSC inline asm
 *   datafile.cpp -> [4]      data pack (.wdf) loading (VC6 for-scope fix)
 *   blit.cpp     -> [5]      blit / clear / ASCII font / style bounds check
 *   readtga.cpp  -> [6]      TGA reader incl. both RLE branches (runalpha fix)
 *   geometry.cpp -> [7]      WBox / WLine (style bounds check)
 *   CANVAS.CPP   -> [8]      block canvas (duplicated-loop removal)
 *
 * ASCII only, so it compiles under the default (GBK) source codepage.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windsoul.h"
#include "wsstring.h"
#include "wstimer.h"
#include "wsfile.h"
#include "wsbitmap.h"
#include "wsalpha.h"
#include "wsgeo.h"
#include "wscanvas.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *msg)
{
    if (cond) { ++g_pass; printf("  [PASS] %s\n", msg); }
    else      { ++g_fail; printf("  [FAIL] %s\n", msg); }
}

/* ------------------------------------------------------------------------- *
 * The data pack (data.wdf) lives in <repo>/dll.  Make the test independent of
 * the caller's working directory: if data.wdf is not in the CWD, walk a few
 * well-known relative locations (and the exe's own folder) until we find it.
 * ------------------------------------------------------------------------- */
#include <direct.h>

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static void locate_pack_dir(void)
{
    static const char *cands[] = {
        "",                 /* already correct? */
        "dll",
        "..\\dll",
        "..\\..\\dll",
        "..\\..\\..\\dll",
        "..\\..\\..\\..\\dll",
        0
    };
    char exe_dir[MAX_PATH];
    char buf[MAX_PATH];
    int i;

    for (i = 0; cands[i]; ++i) {
        if (cands[i][0] == 0) {
            if (file_exists("data.wdf")) return;
        } else {
            sprintf(buf, "%s\\data.wdf", cands[i]);
            if (file_exists(buf) && _chdir(cands[i]) == 0) return;
        }
    }

    /* fall back to the directory that contains this executable */
    if (GetModuleFileNameA(NULL, exe_dir, MAX_PATH)) {
        char *slash = strrchr(exe_dir, '\\');
        if (slash) {
            *slash = 0;
            sprintf(buf, "%s\\data.wdf", exe_dir);
            if (file_exists(buf)) { _chdir(exe_dir); return; }
            sprintf(buf, "%s\\..\\..\\dll\\data.wdf", exe_dir);
            if (file_exists(buf)) {
                sprintf(buf, "%s\\..\\..\\dll", exe_dir);
                _chdir(buf);
                return;
            }
        }
    }
}

/* compare WString::Format() against the CRT sprintf() for the same format+args */
#define FMT_TEST(fmt, ...)                                                      \
    do {                                                                        \
        char _ref[512];                                                         \
        sprintf(_ref, fmt, __VA_ARGS__);                                        \
        WString _w;                                                             \
        _w.Format(fmt, __VA_ARGS__);                                            \
        if (strcmp((const char *)_w, _ref) == 0) {                              \
            ++g_pass;                                                           \
            printf("  [PASS] %-10s -> \"%s\"\n", fmt, _ref);                    \
        } else {                                                                \
            ++g_fail;                                                           \
            printf("  [FAIL] %-10s -> got \"%s\"  want \"%s\"\n",                \
                   fmt, (const char *)_w, _ref);                                \
        }                                                                       \
    } while (0)

/* ------------------------------------------------------------------ [1] */
static void test_wstring(void)
{
    printf("[1] WString\n");

    WString s;
    check(s.IsEmpty(), "default ctor -> IsEmpty()");
    check(s.GetLength() == 0, "default ctor -> length 0");

    s = "Hello";
    check(s.GetLength() == 5, "assign \"Hello\" -> length 5");
    check(strcmp((const char *)s, "Hello") == 0, "assign -> value round-trip");

    s += ", World";
    check(strcmp((const char *)s, "Hello, World") == 0, "operator+= const char*");
    check(s.Compare("Hello, World") == 0, "Compare() equal");
    check(s.CompareNoCase("hello, world") == 0, "CompareNoCase() equal");
    check(s == "Hello, World", "operator== const char*");

    check(strcmp((const char *)s.Mid(7, 5), "World") == 0, "Mid(7,5) == \"World\"");
    check(strcmp((const char *)s.Left(5), "Hello") == 0, "Left(5) == \"Hello\"");
    check(strcmp((const char *)s.Right(5), "World") == 0, "Right(5) == \"World\"");

    WString cat = WString("ab") + "cd";
    check(strcmp((const char *)cat, "abcd") == 0, "operator+ concat");

    s.MakeUpper();
    check(strcmp((const char *)s, "HELLO, WORLD") == 0, "MakeUpper()");
    s.MakeLower();
    check(strcmp((const char *)s, "hello, world") == 0, "MakeLower()");

    check(WString("1234").AsInt() == 1234, "AsInt() == 1234");

    /* grow past every ref-count tier (64 / 128 / 256 / unlimited) */
    WString big;
    for (int i = 0; i < 600; ++i) big += "x";
    check(big.GetLength() == 600, "append 600 chars -> length 600");
}

/* ------------------------------------------------------------------ [2] */
static void test_format(void)
{
    printf("[2] WString::Format  (oracle: CRT sprintf)\n");

    FMT_TEST("%d", 42);
    FMT_TEST("%s", "abc");
    FMT_TEST("%c", 'Z');
    FMT_TEST("%u", 4000000000u);
    FMT_TEST("%x", 0xabcdef);
    FMT_TEST("%08X", 0xdeadbeef);
    FMT_TEST("%-6d|", 7);
    FMT_TEST("%+d", 5);
    FMT_TEST("%10s|", "hi");
    FMT_TEST("%.3s", "abcdef");
    FMT_TEST("%hd", 30000);
    FMT_TEST("%I64d", (__int64)1234567890123);
    FMT_TEST("%llu", (unsigned __int64)9876543210);
    FMT_TEST("%zu", (size_t)1234567);
    FMT_TEST("%hhu", (unsigned)200);
    FMT_TEST("%.2f", 2.5);
    FMT_TEST("%e", 1234.5);
    FMT_TEST("%s/%d/%x", "mix", 99, 0x1f);

    WString w;
    w.Format("100%%done");
    check(strcmp((const char *)w, "100%done") == 0, "100%%done");
    w.Format("trailing%");
    check(1, "trailing '%' -> no crash (was an out-of-bounds read)");
}

/* ------------------------------------------------------------------ [3] */
static void test_timer(void)
{
    printf("[3] WTimer\n");

    int mhz = WTimer::CPU();
    printf("      CPU() = %d MHz\n", mhz);
    check(mhz > 0, "WTimer::CPU() > 0  (inline-asm RDTSC path)");

    WTimer t;
    t.Reset(0);
    DWORD a = t.GetTime();
    Sleep(120);
    DWORD b = t.GetTime();
    printf("      GetTime(): %lu -> %lu  (delta = %lu, 1/10000 s)\n",
           (unsigned long)a, (unsigned long)b, (unsigned long)(b - a));
    check(b >= a, "GetTime() monotonic");
    check((b - a) > 0, "GetTime() advances after Sleep(120ms)");
    check((b - a) < 100000, "GetTime() delta is sane (< 10 s)");
}

/* ------------------------------------------------------------------ [4] */
static void test_file(void)
{
    printf("[4] WFile / data.wdf\n");

    WSERR e = WFile::OpenDataFile("data.wdf");
    printf("      OpenDataFile(\"data.wdf\") = 0x%08lX\n", (unsigned long)e);
    check(e == WS_OK, "OpenDataFile");

    static const char *names[] = {
        "data/tile1.bmp", "data/tile2.bmp", "data/stand.bmp",
        "data/walk.bmp",  "data/sm.bmp",    "data/10001.tga"
    };
    for (int i = 0; i < 6; ++i) {
        WFile f;
        WSERR r = f.Load(names[i]);
        printf("      Load %-16s -> 0x%08lX  size = %lu\n",
               names[i], (unsigned long)r, (unsigned long)f.GetSize());
        check(r == WS_OK && f.GetSize() > 0, names[i]);
    }

    WFile f;
    WSERR r = f.Load("data/__no_such_file__.bmp");
    printf("      Load missing        -> 0x%08lX\n", (unsigned long)r);
    check(r != WS_OK, "Load missing file -> error (no crash)");
}

/* ------------------------------------------------------------------ [5] */
static void test_bitmap(void)
{
    printf("[5] WBitmap  (bitmap.cpp / blit.cpp / text.cpp)\n");

    WBitmap bmp(64, 48);
    check(bmp.GetW() == 64 && bmp.GetH() == 48, "WBitmap(64,48) size");
    check(bmp.GetObjStruct() != 0, "bitmap buffer allocated");

    WPixel green(0, 255, 0);
    check(bmp.Clear(green) == WS_OK, "Clear(green)");
    WPixel *row = bmp[10];
    check(row != 0, "operator[](10) row pointer");
    check((DWORD)row[10] == (DWORD)green, "pixel(10,10) == green");

    /* blit one bitmap onto another */
    WBitmap src(16, 16);
    src.Clear(WPixel(255, 0, 0));
    WBitmap dst(64, 64);
    dst.Clear(WPixel(0, 0, 0));
    src.SetPos(0, 0);
    dst.SetPos(8, 8);                 /* destination kx/ky is the draw position */
    check(dst.Draw(src, BLIT_COPY) == WS_OK, "dst.Draw(src, BLIT_COPY)");
    check((DWORD)dst[12][12] == (DWORD)WPixel(255, 0, 0), "blit -> dst(12,12) red");
    check((DWORD)dst[2][2] == 0, "blit -> dst(2,2) untouched black");

    /* built-in ASCII font (text16 asm) */
    WBitmap txt(80, 16);
    txt.Clear(WPixel(0, 0, 0));
    txt.Draw("Ab9", WPixel(255, 255, 255));
    int nz = 0;
    for (int y = 0; y < txt.GetH(); ++y) {
        WPixel *r = txt[y];
        for (int x = 0; x < txt.GetW(); ++x) if ((DWORD)r[x] != 0) ++nz;
    }
    printf("      ASCII draw -> %d non-black pixels\n", nz);
    check(nz > 0, "WBitmap::Draw(ASCII) painted pixels");

    /* hardened style bounds check */
    check(dst.DrawFunc(16) == 0, "WBitmap::DrawFunc(style>=16) rejected");
    check(dst.DrawFunc(0) != 0, "WBitmap::DrawFunc(0) valid");
}

/* ------------------------------------------------------------------ [6] */
static void tga_header(unsigned char *h, int imgtype, int w, int ht, int bits)
{
    memset(h, 0, 18);
    h[2]  = (unsigned char)imgtype;
    h[12] = (unsigned char)(w & 0xFF);
    h[13] = (unsigned char)((w >> 8) & 0xFF);
    h[14] = (unsigned char)(ht & 0xFF);
    h[15] = (unsigned char)((ht >> 8) & 0xFF);
    h[16] = (unsigned char)bits;
    h[17] = (unsigned char)(bits == 32 ? 8 : 0);   /* alpha bits in low nibble */
}

static void test_tga(void)
{
    printf("[6] WAlphaBitmap / TGA  (readtga.cpp, both RLE branches)\n");

    /* --- 24-bit RLE: exercises readtga.cpp:105 (int runalpha=0) --- */
    {
        unsigned char buf[64];
        int n = 18;
        tga_header(buf, 10, 4, 2, 24);
        /* data row 1: raw packet of 4 px -> magenta(key), red, green, blue (BGR) */
        buf[n++] = 0x03;
        buf[n++] = 255; buf[n++] = 0;   buf[n++] = 255;
        buf[n++] = 0;   buf[n++] = 0;   buf[n++] = 255;
        buf[n++] = 0;   buf[n++] = 255; buf[n++] = 0;
        buf[n++] = 255; buf[n++] = 0;   buf[n++] = 0;
        /* data row 2: RLE packet, 4 px red */
        buf[n++] = 0x83;
        buf[n++] = 0;   buf[n++] = 0;   buf[n++] = 255;

        WAlphaBitmap a;
        WSERR e = a.LoadTGA(buf);
        printf("      LoadTGA(24-bit RLE) = 0x%08lX  %dx%d\n",
               (unsigned long)e, a.GetW(), a.GetH());
        check(e == WS_OK && a.GetW() == 4 && a.GetH() == 2, "24-bit RLE header");
        check((DWORD)a[1][1] == (DWORD)WPixel(255, 0, 0), "raw packet -> red pixel");
        check((DWORD)a[1][0] == 0, "colour-key pixel -> transparent");
        check((DWORD)a[0][0] == (DWORD)WPixel(255, 0, 0), "RLE run -> red pixel (runalpha path)");
        BYTE *ch = a.ChannelLine(0);
        printf("      alpha channel[0][0] = %d\n", ch ? (int)ch[0] : -1);
        check(ch != 0 && ch[0] == 32, "RLE run -> alpha channel 32");
    }

    /* --- 32-bit RLE: exercises readtga.cpp:209 (int runalpha=0) --- */
    {
        unsigned char buf[64];
        int n = 18, i;
        tga_header(buf, 10, 4, 2, 32);
        buf[n++] = 0x03;                                   /* raw, 4 px blue a=255 */
        for (i = 0; i < 4; ++i) {
            buf[n++] = 255; buf[n++] = 0; buf[n++] = 0; buf[n++] = 255;
        }
        buf[n++] = 0x83;                                   /* RLE, 4 px red a=255 */
        buf[n++] = 0;   buf[n++] = 0; buf[n++] = 255; buf[n++] = 255;

        WAlphaBitmap a;
        WSERR e = a.LoadTGA(buf);
        printf("      LoadTGA(32-bit RLE) = 0x%08lX  %dx%d\n",
               (unsigned long)e, a.GetW(), a.GetH());
        check(e == WS_OK && a.GetW() == 4 && a.GetH() == 2, "32-bit RLE header");
        check((DWORD)a[1][0] == (DWORD)WPixel(0, 0, 255), "raw packet -> blue pixel");
        check((DWORD)a[0][0] == (DWORD)WPixel(255, 0, 0), "RLE run -> red pixel (runalpha path)");
    }

    /* --- the real TGA shipped inside data.wdf --- */
    {
        WFile f;
        if (f.Load("data/10001.tga") == WS_OK) {
            WAlphaBitmap a;
            WSERR e = a.LoadTGA(f);
            printf("      LoadTGA(data/10001.tga) = 0x%08lX  %dx%d\n",
                   (unsigned long)e, a.GetW(), a.GetH());
            check(e == WS_OK && a.GetW() > 0 && a.GetH() > 0, "real 10001.tga loaded");
        } else {
            check(0, "data/10001.tga not found in pack");
        }
    }
}

/* ------------------------------------------------------------------ [7] */
static void test_geometry(void)
{
    printf("[7] WBox / WLine  (geometry.cpp)\n");

    WBitmap bmp(64, 64);
    bmp.Clear(WPixel(0, 0, 0));
    WBox box(32, 32, WPixel(255, 0, 0));
    bmp.SetPos(16, 16);               /* destination kx/ky is the draw position */
    WSERR e = bmp.Draw(box, 0);
    printf("      WBox draw = 0x%08lX\n", (unsigned long)e);
    check(e == WS_OK, "WBitmap::Draw(WBox, style 0)");
    check((DWORD)bmp[20][20] == (DWORD)WPixel(255, 0, 0), "box interior pixel red");
    check(box.DrawFunc(6) == 0, "WBox::DrawFunc(style>=6) rejected");
    check(box.DrawFunc(0) != 0, "WBox::DrawFunc(0) valid");

    WBitmap b2(64, 64);
    b2.Clear(WPixel(0, 0, 0));
    WLine line(40, 0, WPixel(0, 255, 0));
    b2.SetPos(5, 30);
    e = b2.Draw(line, 0);
    printf("      WLine draw = 0x%08lX\n", (unsigned long)e);
    check(e == WS_OK, "WBitmap::Draw(WLine, style 0)");
    int gn = 0;
    for (int y = 0; y < 64; ++y) {
        WPixel *r = b2[y];
        for (int x = 0; x < 64; ++x) if ((DWORD)r[x] == (DWORD)WPixel(0, 255, 0)) ++gn;
    }
    printf("      WLine green pixels = %d\n", gn);
    check(gn >= 30, "WLine painted a horizontal run");
}

/* ------------------------------------------------------------------ [8] */
static void test_canvas(void)
{
    printf("[8] WCanvas  (CANVAS.CPP)\n");

    WCanvas cv(200, 100);
    check(cv.GetW() == 200 && cv.GetH() == 100, "WCanvas(200,100) size");

    cv.Clear(WPixel(0, 0, 255));
    cv.SetPos(0, 0);
    cv.Update();

    WBitmap out(200, 100);
    out.Clear(WPixel(0, 0, 0));
    cv.BlitTo(out);
    printf("      canvas pixel(50,50) = 0x%06lX\n",
           (unsigned long)((DWORD)out[50][50] & 0xFFFFFF));
    check((DWORD)out[50][50] == (DWORD)WPixel(0, 0, 255), "Clear -> blue after Update+BlitTo");

    /* draw a bitmap onto a fresh canvas, then read it back */
    WCanvas cv2(200, 100);
    WBitmap red(20, 20);
    red.Clear(WPixel(255, 0, 0));
    cv2.SetPos(30, 30);               /* canvas kx/ky is the draw position */
    check(cv2.Draw(red, BLIT_COPY) == WS_OK, "WCanvas::Draw(WBitmap)");
    cv2.Update();
    cv2.SetPos(0, 0);

    WBitmap out2(200, 100);
    out2.Clear(WPixel(0, 0, 0));
    cv2.BlitTo(out2);
    printf("      canvas2 pixel(35,35) = 0x%06lX\n",
           (unsigned long)((DWORD)out2[35][35] & 0xFFFFFF));
    check((DWORD)out2[35][35] == (DWORD)WPixel(255, 0, 0), "canvas blit -> red at (35,35)");

    /* SetOvlRect is the function whose duplicated loop was removed */
    cv2.SetOvlRect(64, 32);
    check(1, "SetOvlRect(64,32) -> no crash");

    /* informational: Clear + Draw queued in the same frame -- who wins? */
    {
        WCanvas cv3(64, 64);
        cv3.Clear(WPixel(0, 0, 255));       /* queued first */
        cv3.SetPos(8, 8);
        WBitmap g(16, 16);
        g.Clear(WPixel(0, 255, 0));         /* queued second */
        cv3.Draw(g, BLIT_COPY);
        cv3.Update();
        cv3.SetPos(0, 0);
        WBitmap o3(64, 64);
        o3.Clear(WPixel(0, 0, 0));
        cv3.BlitTo(o3);
        DWORD px = (DWORD)o3[12][12] & 0xFFFFFF;
        printf("      [info] Clear(blue)+Draw(green) same frame -> pixel(12,12)=0x%06lX "
               "(%s wins)\n", (unsigned long)px,
               px == 0x00FF00 ? "Draw" : (px == 0x0000FF ? "Clear" : "?"));
    }
}

/* ------------------------------------------------------------------ [9] */
static void test_format_64(void)
{
    printf("[9] WString::Format -- 64-bit arg followed by other specifiers\n");
    FMT_TEST("%I64d|%s", (__int64)1234567890123, "tail");
    FMT_TEST("%llu|%d", (unsigned __int64)9876543210, 7);
    FMT_TEST("%d|%I64d|%s", 1, (__int64)2, "x");
    FMT_TEST("%I64d/%I64d", (__int64)111111111111, (__int64)222222222222);
}

/* --------------------------------------------------------------------- */
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* keep output if something crashes */
    locate_pack_dir();                  /* find the folder holding data.wdf */

    printf("=== Windsoul++ (wpp16) runtime smoke test ===\n");
    printf("    windsoul.dll implicitly linked; CWD = data pack folder\n\n");

    test_wstring();
    test_format();
    test_timer();
    test_file();
    test_bitmap();
    test_tga();
    test_geometry();
    test_canvas();
    test_format_64();

    printf("\n=== RESULT: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
