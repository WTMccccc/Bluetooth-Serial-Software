/*
 * plot.c - 接收区曲线绘图控件
 *
 * 支持：
 *   - 接收 [x,y] 文本数据包并绘制折线；
 *   - 设置 X/Y 步长、X/Y 最大值；
 *   - 底部滑块分别缩放 X、Y 轴；
 *   - 鼠标拖拽平移、滚轮缩放、双击复位；
 *   - 暂停/继续、清空。
 */
#define WIN32_LEAN_AND_MEAN
#include "plot.h"
#include "crashlog.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define PLOT_CLASS L"BLEUartPlot"

#define IDC_XSTEP   3001
#define IDC_YSTEP   3002
#define IDC_YMAX    3004
#define IDC_PAUSE   3005
#define IDC_CLEAR   3006
#define IDC_ZOOMX   3007
#define IDC_ZOOMY   3008
#define IDC_LXSTEP  3011
#define IDC_LYSTEP  3012
#define IDC_LXMAX   3013
#define IDC_LYMAX   3014
#define IDC_LZX     3015
#define IDC_LZY     3016

#define PLOT_POINT_MAX 200000
#define ZOOM_MIN 1.0
#define ZOOM_MAX 50.0

typedef struct PlotPoint {
    float x;
    float y;
} PlotPoint;

static HWND g_wnd;
static BOOL g_visible;
static BOOL g_ready;

static HWND g_ed_xstep, g_ed_ystep, g_ed_ymax;
static HWND g_chk_pause, g_btn_clear;
static HWND g_tb_zoomx, g_tb_zoomy;
static HWND g_lbl[6];
static HFONT g_font_small;

static PlotPoint *g_pts;
static int g_cap = PLOT_POINT_MAX;
static int g_start;
static int g_count;

/* 数据包解析状态 */
static char g_pbuf[64];
static int g_plen;
static int g_pkt;

/* 视图设置：X 轴不设最大值，按数据自动向右滚动 */
static double g_xwin = 100.0;          /* 1x 时 X 轴窗口宽度（数据单位） */
static double g_ymax = 100.0;
static double g_xstep = 10.0, g_ystep = 10.0;
static double g_zoomx = 1.0, g_zoomy = 1.0;
static double g_x0 = 0.0, g_y0 = 0.0;
static double g_latest_x;
static BOOL g_have_x;
static BOOL g_paused;
static DWORD g_last_inval;
static BOOL g_drag;
static POINT g_dragpt;
static double g_dragx0, g_dragy0;
static double g_demo_x;
static int g_demo_mode;

static RECT g_plotrect;
static wchar_t g_ini[MAX_PATH];
static BOOL g_ini_ready;

static int g_dpi = 96;

static void sync_zoom_sliders(void);
static void request_redraw(BOOL force);

static double clampd(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void view_params(double *x0, double *y0, double *sx, double *sy)
{
    *sx = g_xwin / g_zoomx;
    *sy = g_ymax / g_zoomy;
    if (*sx < 1e-6) *sx = 1e-6;
    if (*sy < 1e-6) *sy = 1e-6;
    *x0 = g_x0 < 0.0 ? 0.0 : g_x0;
    *y0 = clampd(g_y0, 0.0, g_ymax - *sy > 0.0 ? g_ymax - *sy : 0.0);
    g_x0 = *x0;
    g_y0 = *y0;
}

static void fmt_num(wchar_t *buf, size_t n, double v)
{
    if (fabs(v - floor(v + 0.5)) < 1e-9)
        _snwprintf_s(buf, n, _TRUNCATE, L"%.0f", v);
    else
        _snwprintf_s(buf, n, _TRUNCATE, L"%.2f", v);
}

static void request_redraw(BOOL force)
{
    DWORD now = GetTickCount();
    if (!g_wnd) return;
    if (!force && now - g_last_inval < 40) return;
    g_last_inval = now;
    InvalidateRect(g_wnd, NULL, FALSE);
}

void plot_clear(void)
{
    g_start = 0;
    g_count = 0;
    if (g_wnd) InvalidateRect(g_wnd, NULL, FALSE);
}

static void settings_path(void)
{
    const wchar_t *logp = log_file_path();
    wchar_t *slash;

    wcscpy_s(g_ini, MAX_PATH, logp);
    slash = wcsrchr(g_ini, L'\\');
    if (slash) {
        *slash = 0;
        wcscat_s(g_ini, MAX_PATH, L"\\settings.ini");
        g_ini_ready = TRUE;
    }
}

static void settings_save(void)
{
    wchar_t buf[32];

    if (!g_ini_ready) return;
    _snwprintf_s(buf, 32, _TRUNCATE, L"%g", g_xstep);
    WritePrivateProfileStringW(L"plot", L"xstep", buf, g_ini);
    _snwprintf_s(buf, 32, _TRUNCATE, L"%g", g_ystep);
    WritePrivateProfileStringW(L"plot", L"ystep", buf, g_ini);
    _snwprintf_s(buf, 32, _TRUNCATE, L"%g", g_ymax);
    WritePrivateProfileStringW(L"plot", L"ymax", buf, g_ini);
    _snwprintf_s(buf, 32, _TRUNCATE, L"%.2f", g_zoomx);
    WritePrivateProfileStringW(L"plot", L"zoomx", buf, g_ini);
    _snwprintf_s(buf, 32, _TRUNCATE, L"%.2f", g_zoomy);
    WritePrivateProfileStringW(L"plot", L"zoomy", buf, g_ini);
}

static void settings_touch(void)
{
    if (g_wnd) SetTimer(g_wnd, 2, 500, NULL);
}

static double settings_get_double(const wchar_t *key, double def)
{
    wchar_t buf[32];
    wchar_t *end;
    double v;

    buf[0] = 0;
    GetPrivateProfileStringW(L"plot", key, L"", buf, 32, g_ini);
    if (!buf[0]) return def;
    v = wcstod(buf, &end);
    if (end == buf || !isfinite(v)) return def;
    return v;
}

static void settings_load(void)
{
    wchar_t buf[32];
    double v;

    if (!g_ini_ready) return;
    g_ready = FALSE;

    v = settings_get_double(L"xstep", 10.0);
    if (v > 0) g_xstep = v;
    v = settings_get_double(L"ystep", 10.0);
    if (v > 0) g_ystep = v;
    v = settings_get_double(L"ymax", 100.0);
    if (v > 0) g_ymax = v;
    g_zoomx = clampd(settings_get_double(L"zoomx", 1.0), ZOOM_MIN, ZOOM_MAX);
    g_zoomy = clampd(settings_get_double(L"zoomy", 1.0), ZOOM_MIN, ZOOM_MAX);

    _snwprintf_s(buf, 32, _TRUNCATE, L"%g", g_xstep);
    SetWindowTextW(g_ed_xstep, buf);
    _snwprintf_s(buf, 32, _TRUNCATE, L"%g", g_ystep);
    SetWindowTextW(g_ed_ystep, buf);
    _snwprintf_s(buf, 32, _TRUNCATE, L"%g", g_ymax);
    SetWindowTextW(g_ed_ymax, buf);

    g_ready = TRUE;
    sync_zoom_sliders();
    request_redraw(TRUE);
}
static void add_point(double x, double y)
{
    if (g_paused) return;
    if (!isfinite(x) || !isfinite(y)) return;
    if (!g_pts) {
        g_pts = (PlotPoint *)malloc(sizeof(PlotPoint) * (size_t)g_cap);
        if (!g_pts) return;
        g_start = 0;
        g_count = 0;
    }
    if (g_count < g_cap) {
        g_pts[(g_start + g_count) % g_cap].x = (float)x;
        g_pts[(g_start + g_count) % g_cap].y = (float)y;
        g_count++;
    } else {
        g_pts[g_start].x = (float)x;
        g_pts[g_start].y = (float)y;
        g_start = (g_start + 1) % g_cap;
    }
    {
        double span = g_xwin / g_zoomx;
        if (!g_have_x) {
            g_x0 = x > span * 0.02 ? x - span * 0.02 : 0.0;
            g_have_x = TRUE;
        } else if (x < g_x0 - span * 0.5) {
            /* 新一轮数据从较小的 x 重新开始 */
            g_x0 = x - span * 0.02;
            if (g_x0 < 0.0) g_x0 = 0.0;
        } else if (x > g_x0 + span * 0.98) {
            /* 数据超出右边界：窗口跟着往右移 */
            g_x0 = x - span * 0.98;
            if (g_x0 < 0.0) g_x0 = 0.0;
        }
        g_latest_x = x;
    }
    request_redraw(FALSE);
}

static void parse_packet(void)
{
    char *comma;
    char *e1, *e2;
    double x, y;

    g_pbuf[g_plen] = 0;
    comma = strchr(g_pbuf, ',');
    if (!comma) return;
    *comma = 0;
    x = strtod(g_pbuf, &e1);
    y = strtod(comma + 1, &e2);
    if (e1 == g_pbuf || e2 == comma + 1) return;
    if (!isfinite(x) || !isfinite(y)) return;
    add_point(x, y);
}

void plot_feed(const BYTE *data, int len)
{
    int i;
    if (!data || len <= 0) return;
    for (i = 0; i < len; i++) {
        unsigned char c = data[i];
        if (c == '[') {
            g_pkt = 1;
            g_plen = 0;
        } else if (g_pkt) {
            if (c == ']') {
                g_pkt = 0;
                if (g_plen > 0) parse_packet();
            } else if (g_plen < (int)sizeof(g_pbuf) - 1 &&
                       (isdigit(c) || c == '.' || c == '-' || c == '+' ||
                        c == ',' || c == ' ' || c == 'e' || c == 'E')) {
                g_pbuf[g_plen++] = (char)c;
            } else {
                g_pkt = 0;   /* 非法字符，丢弃这个包 */
            }
        }
    }
    if (g_pkt && g_plen >= (int)sizeof(g_pbuf) - 1) g_pkt = 0;
}

static BOOL read_double(HWND edit, double *out)
{
    wchar_t buf[40];
    wchar_t *end;
    double v;
    GetWindowTextW(edit, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    v = wcstod(buf, &end);
    if (end == buf || !isfinite(v)) return FALSE;
    *out = v;
    return TRUE;
}

static void apply_settings(void)
{
    double v;
    if (!g_ready) return;
    if (read_double(g_ed_xstep, &v) && v > 0) g_xstep = v;
    if (read_double(g_ed_ystep, &v) && v > 0) g_ystep = v;
    if (read_double(g_ed_ymax, &v) && v > 0 && v > g_ystep) g_ymax = v;
    {
        double x0, y0, sx, sy;
        view_params(&x0, &y0, &sx, &sy);
    }
    settings_touch();
    request_redraw(TRUE);
}

static void set_zoom_x(double nz)
{
    nz = clampd(nz, ZOOM_MIN, ZOOM_MAX);
    g_zoomx = nz;
    if (g_have_x) {
        double span = g_xwin / g_zoomx;
        g_x0 = g_latest_x - span * 0.98;
        if (g_x0 < 0.0) g_x0 = 0.0;
    }
}

static void set_zoom_y(double nz)
{
    double old_span = g_ymax / g_zoomy;
    double center = g_y0 + old_span / 2.0;
    double new_span;
    nz = clampd(nz, ZOOM_MIN, ZOOM_MAX);
    g_zoomy = nz;
    new_span = g_ymax / g_zoomy;
    g_y0 = clampd(center - new_span / 2.0, 0.0, g_ymax - new_span > 0 ? g_ymax - new_span : 0.0);
}

static void sync_zoom_sliders(void)
{
    if (!g_tb_zoomx || !g_tb_zoomy) return;
    SendMessageW(g_tb_zoomx, TBM_SETPOS, TRUE, (LPARAM)(LONG)(g_zoomx * 100.0 + 0.5));
    SendMessageW(g_tb_zoomy, TBM_SETPOS, TRUE, (LPARAM)(LONG)(g_zoomy * 100.0 + 0.5));
}

static void zoom_at(int px, int py, double factor)
{
    double x0, y0, sx, sy;
    double fx, fy;
    int w = g_plotrect.right - g_plotrect.left;
    int h = g_plotrect.bottom - g_plotrect.top;

    if (w <= 0 || h <= 0) return;
    view_params(&x0, &y0, &sx, &sy);
    fx = (double)(px - g_plotrect.left) / (double)w;
    fy = (double)(g_plotrect.bottom - py) / (double)h;
    fx = clampd(fx, 0.0, 1.0);
    fy = clampd(fy, 0.0, 1.0);

    {
        double datax = x0 + fx * sx;
        double datay = y0 + fy * sy;
        double nzx = clampd(g_zoomx * factor, ZOOM_MIN, ZOOM_MAX);
        double nzy = clampd(g_zoomy * factor, ZOOM_MIN, ZOOM_MAX);
        double nsx = g_xwin / nzx;
        double nsy = g_ymax / nzy;

        g_zoomx = nzx;
        g_zoomy = nzy;
        g_x0 = datax - fx * nsx;
        if (g_x0 < 0.0) g_x0 = 0.0;
        g_y0 = clampd(datay - fy * nsy, 0.0, g_ymax - nsy > 0 ? g_ymax - nsy : 0.0);
    }
    sync_zoom_sliders();
    request_redraw(TRUE);
}

/* ------------------------------------------------------------------ */
/* 绘制                                                                */
/* ------------------------------------------------------------------ */

static void draw_labels(HDC dc)
{
    double x0, y0, sx, sy;
    double v;
    int w = g_plotrect.right - g_plotrect.left;
    int h = g_plotrect.bottom - g_plotrect.top;
    wchar_t buf[32];
    int guard = 0;

    view_params(&x0, &y0, &sx, &sy);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(90, 90, 90));
    SelectObject(dc, g_font_small);

    /* X 轴刻度 */
    v = ceil(x0 / g_xstep) * g_xstep;
    if (v < 0) v = 0;
    for (; v <= x0 + sx + 1e-9 && guard < 200; v += g_xstep, guard++) {
        int px = g_plotrect.left + (int)((v - x0) / sx * w);
        RECT r;
        r.left = px - 40;
        r.right = px + 40;
        r.top = g_plotrect.bottom + 2;
        r.bottom = g_plotrect.bottom + 18;
        fmt_num(buf, 32, v);
        DrawTextW(dc, buf, -1, &r, DT_CENTER | DT_SINGLELINE | DT_TOP);
    }

    /* Y 轴刻度 */
    guard = 0;
    v = ceil(y0 / g_ystep) * g_ystep;
    if (v < 0) v = 0;
    for (; v <= y0 + sy + 1e-9 && guard < 200; v += g_ystep, guard++) {
        int py = g_plotrect.bottom - (int)((v - y0) / sy * h);
        RECT r;
        r.left = 0;
        r.right = g_plotrect.left - 6;
        r.top = py - 8;
        r.bottom = py + 8;
        fmt_num(buf, 32, v);
        DrawTextW(dc, buf, -1, &r, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
}

static void draw_grid(HDC dc)
{
    double x0, y0, sx, sy;
    double v;
    int w = g_plotrect.right - g_plotrect.left;
    int h = g_plotrect.bottom - g_plotrect.top;
    int guard;
    HPEN pen;

    view_params(&x0, &y0, &sx, &sy);
    pen = CreatePen(PS_SOLID, 1, RGB(232, 236, 240));
    SelectObject(dc, pen);

    guard = 0;
    v = ceil(x0 / g_xstep) * g_xstep;
    if (v < 0) v = 0;
    for (; v <= x0 + sx + 1e-9 && guard < 200; v += g_xstep, guard++) {
        int px = g_plotrect.left + (int)((v - x0) / sx * w);
        MoveToEx(dc, px, g_plotrect.top, NULL);
        LineTo(dc, px, g_plotrect.bottom);
    }
    guard = 0;
    v = ceil(y0 / g_ystep) * g_ystep;
    if (v < 0) v = 0;
    for (; v <= y0 + sy + 1e-9 && guard < 200; v += g_ystep, guard++) {
        int py = g_plotrect.bottom - (int)((v - y0) / sy * h);
        MoveToEx(dc, g_plotrect.left, py, NULL);
        LineTo(dc, g_plotrect.right, py);
    }
    DeleteObject(pen);
}

static void draw_axes(HDC dc)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(120, 126, 132));
    SelectObject(dc, pen);
    MoveToEx(dc, g_plotrect.left, g_plotrect.top, NULL);
    LineTo(dc, g_plotrect.left, g_plotrect.bottom + 1);
    LineTo(dc, g_plotrect.right, g_plotrect.bottom + 1);
    DeleteObject(pen);
}

static void draw_curve(HDC dc)
{
    double x0, y0, sx, sy;
    int w = g_plotrect.right - g_plotrect.left;
    int h = g_plotrect.bottom - g_plotrect.top;
    int i;
    HPEN pen;
    POINT pts[1024];
    int np = 0;
    POINT prev = {0, 0};
    BOOL have_prev = FALSE;
    double prev_x = 0.0;
    BOOL have_x = FALSE;
    int step = 1;

    view_params(&x0, &y0, &sx, &sy);
    if (g_count <= 0) return;
    if (g_count > 60000) step = g_count / 60000 + 1;

    pen = CreatePen(PS_SOLID, 2, RGB(0, 102, 204));
    SelectObject(dc, pen);
    IntersectClipRect(dc, g_plotrect.left, g_plotrect.top, g_plotrect.right, g_plotrect.bottom);

    for (i = 0; i < g_count; i += step) {
        const PlotPoint *p = &g_pts[(g_start + i) % g_cap];
        POINT q;

        q.x = g_plotrect.left + (int)(((double)p->x - x0) / sx * w);
        q.y = g_plotrect.bottom - (int)(((double)p->y - y0) / sy * h);
        if (q.x < -30000) q.x = -30000;
        if (q.x > 30000) q.x = 30000;
        if (q.y < -30000) q.y = -30000;
        if (q.y > 30000) q.y = 30000;

        /* x 回退说明是新的一轮数据，断开折线，避免横穿整张图 */
        if (have_x && (double)p->x < prev_x - 1e-6) {
            if (np > 1) Polyline(dc, pts, np);
            np = 0;
            have_prev = FALSE;
        }
        prev_x = (double)p->x;
        have_x = TRUE;

        if (np == 0 && have_prev) pts[np++] = prev;
        pts[np++] = q;
        prev = q;
        have_prev = TRUE;
        if (np >= (int)(sizeof(pts) / sizeof(pts[0]))) {
            if (np > 1) Polyline(dc, pts, np);
            np = 0;
            have_prev = FALSE;
        }
    }
    if (np > 1) Polyline(dc, pts, np);
    SelectClipRgn(dc, NULL);
    DeleteObject(pen);
}

static void draw_overlay(HDC dc)
{
    wchar_t buf[64];
    RECT r;

    r.left = g_plotrect.left;
    r.top = g_plotrect.top + 6;
    r.right = g_plotrect.right - 10;
    r.bottom = r.top + 20;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(120, 126, 132));
    SelectObject(dc, g_font_small);
    _snwprintf_s(buf, 64, _TRUNCATE, L"%d 点", g_count);
    DrawTextW(dc, buf, -1, &r, DT_RIGHT | DT_SINGLELINE | DT_TOP);

    if (g_paused) {
        r.left = g_plotrect.left;
        r.right = g_plotrect.right;
        r.top = g_plotrect.top + 8;
        r.bottom = r.top + 24;
        SetTextColor(dc, RGB(200, 40, 40));
        DrawTextW(dc, L"已暂停", -1, &r, DT_CENTER | DT_SINGLELINE | DT_TOP);
    }
}

static void paint(HDC target, int w, int h)
{
    HDC mem;
    HBITMAP bmp;
    HGDIOBJ old;
    HBRUSH bg;
    RECT all = {0, 0, w, h};
    HPEN bp;
    HGDIOBJ op, ob;

    mem = CreateCompatibleDC(target);
    bmp = CreateCompatibleBitmap(target, w, h);
    old = SelectObject(mem, bmp);

    bg = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    FillRect(mem, &all, bg);
    DeleteObject(bg);

    bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mem, &g_plotrect, bg);
    DeleteObject(bg);

    draw_grid(mem);
    draw_axes(mem);

    bp = CreatePen(PS_SOLID, 1, RGB(170, 176, 182));
    op = SelectObject(mem, bp);
    ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
    Rectangle(mem, g_plotrect.left, g_plotrect.top, g_plotrect.right + 1, g_plotrect.bottom + 1);
    SelectObject(mem, ob);
    SelectObject(mem, op);
    DeleteObject(bp);

    draw_curve(mem);
    draw_labels(mem);
    draw_overlay(mem);

    BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

/* ------------------------------------------------------------------ */
/* 控件                                                                */
/* ------------------------------------------------------------------ */

static HWND make_ctl(const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id)
{
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
                           0, 0, 10, 10, g_wnd, (HMENU)(INT_PTR)id,
                           (HINSTANCE)(ULONG_PTR)GetWindowLongPtrW(g_wnd, GWLP_HINSTANCE), NULL);
}

static void create_children(HWND wnd)
{
    HDC dc;
    LOGFONTW lf;

    g_lbl[0] = make_ctl(L"STATIC", L"X 步长", SS_RIGHT, 0, IDC_LXSTEP);
    g_lbl[1] = make_ctl(L"STATIC", L"Y 步长", SS_RIGHT, 0, IDC_LYSTEP);
    g_lbl[3] = make_ctl(L"STATIC", L"Y 最大", SS_RIGHT, 0, IDC_LYMAX);
    g_lbl[4] = make_ctl(L"STATIC", L"X 缩放", SS_RIGHT, 0, IDC_LZX);
    g_lbl[5] = make_ctl(L"STATIC", L"Y 缩放", SS_RIGHT, 0, IDC_LZY);

    g_ed_xstep = make_ctl(L"EDIT", L"10", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_XSTEP);
    g_ed_ystep = make_ctl(L"EDIT", L"10", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_YSTEP);
    g_ed_ymax = make_ctl(L"EDIT", L"100", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_YMAX);

    g_chk_pause = make_ctl(L"BUTTON", L"暂停", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_PAUSE);
    g_btn_clear = make_ctl(L"BUTTON", L"清空", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_CLEAR);

    g_tb_zoomx = make_ctl(L"msctls_trackbar32", L"",
                          TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, IDC_ZOOMX);
    g_tb_zoomy = make_ctl(L"msctls_trackbar32", L"",
                          TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, IDC_ZOOMY);
    SendMessageW(g_tb_zoomx, TBM_SETRANGE, TRUE, MAKELPARAM((int)(ZOOM_MIN * 100), (int)(ZOOM_MAX * 100)));
    SendMessageW(g_tb_zoomy, TBM_SETRANGE, TRUE, MAKELPARAM((int)(ZOOM_MIN * 100), (int)(ZOOM_MAX * 100)));
    SendMessageW(g_tb_zoomx, TBM_SETPOS, TRUE, 100);
    SendMessageW(g_tb_zoomy, TBM_SETPOS, TRUE, 100);

    dc = GetDC(wnd);
    if (dc) {
        int dpi = GetDeviceCaps(dc, LOGPIXELSY);
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -MulDiv(9, dpi, 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Microsoft YaHei UI");
        g_font_small = CreateFontIndirectW(&lf);
        ReleaseDC(wnd, dc);
    }
    if (!g_font_small) g_font_small = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    {
        HWND all[] = { g_lbl[0], g_lbl[1], g_lbl[2], g_lbl[3], g_lbl[4], g_lbl[5],
                       g_ed_xstep, g_ed_ystep, g_ed_ymax,
                       g_chk_pause, g_btn_clear };
        int i;
        for (i = 0; i < (int)(sizeof(all) / sizeof(all[0])); i++)
            if (all[i]) SendMessageW(all[i], WM_SETFONT, (WPARAM)g_font_small, TRUE);
    }
}

static void do_layout(HWND wnd)
{
    RECT rc;
    int w, h;
    int x, y, eh = 24, lw = 56, ew = 46, gap = 6;
    int by, half;

    GetClientRect(wnd, &rc);
    w = rc.right;
    h = rc.bottom;

    x = 8;
    y = 8;
    MoveWindow(g_lbl[0], x, y + 4, lw - 6, 20, TRUE); x += lw;
    MoveWindow(g_ed_xstep, x, y, ew, eh, TRUE); x += ew + gap;
    MoveWindow(g_lbl[1], x, y + 4, lw - 6, 20, TRUE); x += lw;
    MoveWindow(g_ed_ystep, x, y, ew, eh, TRUE); x += ew + gap;
    MoveWindow(g_lbl[3], x, y + 4, lw - 6, 20, TRUE); x += lw;
    MoveWindow(g_ed_ymax, x, y, ew, eh, TRUE); x += ew + gap;
    MoveWindow(g_chk_pause, x, y + 3, 58, 22, TRUE); x += 62;
    MoveWindow(g_btn_clear, x, y, 56, eh, TRUE);

    by = h - 34;
    half = (w - 24) / 2;
    MoveWindow(g_lbl[4], 8, by + 6, 40, 20, TRUE);
    MoveWindow(g_tb_zoomx, 48, by, half - 44, 30, TRUE);
    MoveWindow(g_lbl[5], 8 + half, by + 6, 40, 20, TRUE);
    MoveWindow(g_tb_zoomy, 48 + half, by, half - 44, 30, TRUE);

    g_plotrect.left = 56;
    g_plotrect.top = 38;
    g_plotrect.right = w - 14;
    g_plotrect.bottom = by - 26;
    if (g_plotrect.right < g_plotrect.left + 20) g_plotrect.right = g_plotrect.left + 20;
    if (g_plotrect.bottom < g_plotrect.top + 20) g_plotrect.bottom = g_plotrect.top + 20;
}

static LRESULT CALLBACK PlotWndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_wnd = wnd;
        g_dpi = 96;
        create_children(wnd);
        do_layout(wnd);
        g_ready = TRUE;
        sync_zoom_sliders();
        return 0;

    case WM_SIZE:
        do_layout(wnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(wnd, &ps);
            RECT rc;
            GetClientRect(wnd, &rc);
            paint(dc, rc.right, rc.bottom);
            EndPaint(wnd, &ps);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_XSTEP: case IDC_YSTEP: case IDC_YMAX:
            if (HIWORD(wp) == EN_CHANGE) apply_settings();
            return 0;
        case IDC_PAUSE:
            g_paused = IsDlgButtonChecked(wnd, IDC_PAUSE) == BST_CHECKED;
            request_redraw(TRUE);
            return 0;
        case IDC_CLEAR:
            plot_clear();
            return 0;
        }
        break;

    case WM_TIMER:
        if (wp == 2) {
            KillTimer(wnd, 2);
            settings_save();
            return 0;
        }
        if (wp == 1) {
            g_demo_x += 1.0;
            if (g_demo_x > 1000.0) g_demo_x = 0.0;
            if (g_demo_mode == 2) {
                char pkt[48];
                int n = sprintf(pkt, "[%.1f,%.1f]", g_demo_x,
                                g_ymax * 0.5 + g_ymax * 0.35 * sin(g_demo_x / 8.0));
                plot_feed((const BYTE *)pkt, n);
            } else {
                add_point(g_demo_x, g_ymax * 0.5 + g_ymax * 0.35 * sin(g_demo_x / 8.0));
            }
            return 0;
        }
        break;

    case WM_HSCROLL:
        if ((HWND)lp == g_tb_zoomx) {
            int pos = (int)SendMessageW(g_tb_zoomx, TBM_GETPOS, 0, 0);
            set_zoom_x(pos / 100.0);
            settings_touch();
            request_redraw(TRUE);
        } else if ((HWND)lp == g_tb_zoomy) {
            int pos = (int)SendMessageW(g_tb_zoomy, TBM_GETPOS, 0, 0);
            set_zoom_y(pos / 100.0);
            settings_touch();
            request_redraw(TRUE);
        }
        return 0;

    case WM_MOUSEWHEEL:
        {
            POINT pt;
            pt.x = GET_X_LPARAM(lp);
            pt.y = GET_Y_LPARAM(lp);
            ScreenToClient(wnd, &pt);
            if (PtInRect(&g_plotrect, pt)) {
                int delta = GET_WHEEL_DELTA_WPARAM(wp);
                zoom_at(pt.x, pt.y, delta > 0 ? 1.25 : 1.0 / 1.25);
                settings_touch();
                return 0;
            }
        }
        break;

    case WM_LBUTTONDOWN:
        {
            POINT pt;
            pt.x = GET_X_LPARAM(lp);
            pt.y = GET_Y_LPARAM(lp);
            if (PtInRect(&g_plotrect, pt)) {
                double x0, y0, sx, sy;
                view_params(&x0, &y0, &sx, &sy);
                SetCapture(wnd);
                SetFocus(wnd);
                g_drag = TRUE;
                g_dragpt = pt;
                g_dragx0 = g_x0;
                g_dragy0 = g_y0;
                (void)sx; (void)sy;
                return 0;
            }
        }
        break;

    case WM_MOUSEMOVE:
        if (g_drag) {
            POINT pt;
            double x0, y0, sx, sy;
            int w = g_plotrect.right - g_plotrect.left;
            int h = g_plotrect.bottom - g_plotrect.top;
            pt.x = GET_X_LPARAM(lp);
            pt.y = GET_Y_LPARAM(lp);
            view_params(&x0, &y0, &sx, &sy);
            g_x0 = g_dragx0 - (double)(pt.x - g_dragpt.x) / (double)w * sx;
            if (g_x0 < 0.0) g_x0 = 0.0;
            g_y0 = clampd(g_dragy0 + (double)(pt.y - g_dragpt.y) / (double)h * sy,
                          0.0, g_ymax - sy > 0 ? g_ymax - sy : 0.0);
            request_redraw(TRUE);
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (g_drag) {
            g_drag = FALSE;
            ReleaseCapture();
            return 0;
        }
        break;

    case WM_LBUTTONDBLCLK:
        g_zoomx = 1.0;
        g_zoomy = 1.0;
        g_x0 = 0.0;
        g_y0 = 0.0;
        sync_zoom_sliders();
        request_redraw(TRUE);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(wnd, &pt);
            if (PtInRect(&g_plotrect, pt)) {
                SetCursor(LoadCursorW(NULL, IDC_CROSS));
                return TRUE;
            }
        }
        break;

    case WM_DESTROY:
        KillTimer(wnd, 2);
        settings_save();
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void plot_create(HWND parent)
{
    static BOOL registered = FALSE;
    INITCOMMONCONTROLSEX icc;

    if (g_wnd) return;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    if (!registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = PlotWndProc;
        wc.hInstance = (HINSTANCE)(ULONG_PTR)GetWindowLongPtrW(parent, GWLP_HINSTANCE);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = PLOT_CLASS;
        if (!RegisterClassExW(&wc)) return;
        registered = TRUE;
    }
    g_wnd = CreateWindowExW(0, PLOT_CLASS, L"",
                            WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                            0, 0, 100, 100, parent, NULL,
                            (HINSTANCE)(ULONG_PTR)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
    settings_path();
    if (g_wnd) settings_load();
    {
        wchar_t demo[8];
        if (GetEnvironmentVariableW(L"BLE_PLOT_DEMO", demo, 8) &&
            (demo[0] == L'1' || demo[0] == L'2')) {
            g_demo_mode = (demo[0] == L'2') ? 2 : 1;
            SetTimer(g_wnd, 1, 50, NULL);
        }
    }
}

void plot_layout(const RECT *rc)
{
    if (g_wnd && rc) MoveWindow(g_wnd, rc->left, rc->top, rc->right - rc->left,
                                rc->bottom - rc->top, TRUE);
}

void plot_show(BOOL show)
{
    if (!g_wnd) return;
    g_visible = show;
    ShowWindow(g_wnd, show ? SW_SHOW : SW_HIDE);
    if (show) request_redraw(TRUE);
}

BOOL plot_is_visible(void)
{
    return g_visible;
}
