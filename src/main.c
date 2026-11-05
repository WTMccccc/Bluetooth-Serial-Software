/*
 * main.c - BLE 串口助手（Win32 界面）
 *
 * 界面分区：
 *   左侧：蓝牙设备列表 + 连接信息（名称/状态/地址/服务/特征/MTU）
 *   右侧：接收区（含清空） + 发送区（含清空）
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <bluetoothapis.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "ble.h"
#include "util.h"
#include "winrt_abi.h"
#include "crashlog.h"
#include "logview.h"
#include "plot.h"

#define APP_NAME        L"BLE 串口助手"
#define APP_SUBTITLE    L"ESP32 / STM32 · BLE 4.0+ · UTF-8 文本透传"
#define APP_CLASS       L"BLEUartHostWnd"

#define WM_APP_BLE_STATUS (WM_APP + 1)
#define WM_APP_BLE_LOG    (WM_APP + 2)

/* 控件 ID */
enum {
    IDC_LIST_DEV = 100,
    IDC_BTN_SCAN,
    IDC_BTN_STOP,
    IDC_BTN_CONNECT,
    IDC_BTN_DISCONNECT,
    IDC_LBL_STATE,
    IDC_LBL_NAME,
    IDC_LBL_ADDR,
    IDC_LBL_SVC,
    IDC_LBL_NOTIFY,
    IDC_LBL_WRITE,
    IDC_LBL_MTU,
    IDC_BTN_CLEAR_RECV,
    IDC_BTN_SAVE,
    IDC_CHK_AUTOSCROLL,
    IDC_CHK_HEXVIEW,
    IDC_CHK_SHOWSTATUS,
    IDC_CHK_PLOT,
    IDC_EDIT_RECV,
    IDC_BTN_CLEAR_SEND,
    IDC_CHK_HEXSEND,
    IDC_CMB_EOL,
    IDC_EDIT_CHUNK,
    IDC_EDIT_INTERVAL,
    IDC_EDIT_SEND,
    IDC_BTN_SEND,
    IDC_LBL_SENDHINT,
    IDC_LBL_CHUNK,
    IDC_LBL_INTERVAL,
    IDC_LBL_EOL
};

/* 颜色 */
#define CLR_BG        RGB(243, 245, 248)
#define CLR_CARD      RGB(255, 255, 255)
#define CLR_BORDER    RGB(224, 228, 234)
#define CLR_TEXT      RGB(28, 32, 38)
#define CLR_SUBTEXT   RGB(108, 116, 128)
#define CLR_ACCENT    RGB(24, 108, 216)
#define CLR_ACCENT_D  RGB(18, 88, 180)
#define CLR_GREEN     RGB(14, 122, 88)
#define CLR_GREEN_BG  RGB(232, 247, 240)
#define CLR_RED       RGB(192, 57, 43)
#define CLR_RED_BG    RGB(253, 237, 235)
#define CLR_AMBER     RGB(160, 110, 10)
#define CLR_AMBER_BG  RGB(255, 248, 230)

typedef enum BtnKind {
    BTN_PRIMARY = 0,
    BTN_SECONDARY,
    BTN_DANGER
} BtnKind;

typedef struct App {
    HINSTANCE inst;
    HWND wnd;
    UINT dpi;

    /* 字体 */
    HFONT f_title;
    HFONT f_sub;
    HFONT f_ui;
    HFONT f_ui_bold;
    HFONT f_mono;
    HFONT f_small;

    /* 控件 */
    HWND list;
    HWND btn_scan;
    HWND btn_stop;
    HWND btn_connect;
    HWND btn_disconnect;
    HWND lbl_name;
    HWND lbl_addr;
    HWND lbl_state;
    HWND lbl_svc;
    HWND lbl_notify;
    HWND lbl_write;
    HWND lbl_mtu;
    HWND btn_clear_recv;
    HWND btn_save;
    HWND chk_autoscroll;
    HWND chk_hexview;
    HWND chk_showstatus;
    HWND chk_plot;
    HWND edit_recv;
    HWND btn_clear_send;
    HWND chk_hexsend;
    HWND cmb_eol;
    HWND edit_chunk;
    HWND edit_interval;
    HWND edit_send;
    HWND btn_send;
    HWND lbl_sendhint;
    HWND lbl_chunk;
    HWND lbl_interval;
    HWND lbl_eol;

    /* 卡片区域 */
    RECT card_dev;
    RECT card_info;
    RECT card_recv;
    RECT card_send;
    RECT link_log_rect;

    /* 状态文本 */
    wchar_t adapter_text[64];
    wchar_t rx_text[64];
    wchar_t tx_text[64];

    /* 接收解码 */
    BYTE u8_pending[8];
    int  u8_pending_len;
    BOOL cr_pending;

    BOOL scanning;
    BOOL radio_ok;
    HBRUSH br_bg;
    HBRUSH br_card;
} App;

static App g_app;


typedef struct BtnDef {
    int id;
    BtnKind kind;
} BtnDef;

static const BtnDef kButtons[] = {
    {IDC_BTN_SCAN, BTN_PRIMARY},
    {IDC_BTN_STOP, BTN_SECONDARY},
    {IDC_BTN_CONNECT, BTN_PRIMARY},
    {IDC_BTN_DISCONNECT, BTN_DANGER},
    {IDC_BTN_CLEAR_RECV, BTN_SECONDARY},
    {IDC_BTN_SAVE, BTN_SECONDARY},
    {IDC_BTN_CLEAR_SEND, BTN_SECONDARY},
    {IDC_BTN_SEND, BTN_PRIMARY},
};

static BtnKind button_kind(int id)
{
    size_t i;
    for (i = 0; i < _countof(kButtons); i++) {
        if (kButtons[i].id == id) return kButtons[i].kind;
    }
    return BTN_SECONDARY;
}

static int S(int v)
{
    return MulDiv(v, (int)g_app.dpi, 96);
}

/* ------------------------------------------------------------------ */
/* 字体                                                                */
/* ------------------------------------------------------------------ */
static HFONT make_font(int pt, BOOL bold, const wchar_t *face)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -MulDiv(pt, (int)g_app.dpi, 72);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, LF_FACESIZE, face);
    return CreateFontIndirectW(&lf);
}

static void create_fonts(void)
{
    g_app.f_title   = make_font(16, TRUE, L"Microsoft YaHei UI");
    g_app.f_sub     = make_font(9, FALSE, L"Microsoft YaHei UI");
    g_app.f_ui      = make_font(9, FALSE, L"Microsoft YaHei UI");
    g_app.f_ui_bold = make_font(9, TRUE, L"Microsoft YaHei UI");
    g_app.f_mono    = make_font(10, FALSE, L"Consolas");
    g_app.f_small   = make_font(8, FALSE, L"Microsoft YaHei UI");
}

static void destroy_fonts(void)
{
    HFONT *fonts[] = {&g_app.f_title, &g_app.f_sub, &g_app.f_ui, &g_app.f_ui_bold,
                      &g_app.f_mono, &g_app.f_small};
    size_t i;
    for (i = 0; i < _countof(fonts); i++) {
        if (*fonts[i]) { DeleteObject(*fonts[i]); *fonts[i] = NULL; }
    }
}

/* ------------------------------------------------------------------ */
/* 文本追加                                                            */
/* ------------------------------------------------------------------ */
static void edit_scroll_preserve(HWND edit, int old_first, BOOL autoscroll)
{
    if (autoscroll) {
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
    } else {
        int now = (int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
        if (now != old_first) SendMessageW(edit, EM_LINESCROLL, 0, old_first - now);
    }
}

static void append_edit_text(HWND edit, const wchar_t *text)
{
    int len, old_first;
    BOOL autoscroll = IsDlgButtonChecked(g_app.wnd, IDC_CHK_AUTOSCROLL) == BST_CHECKED;
    if (!text || !text[0]) return;

    /* 追加 + 滚动时关闭重绘，结束后整块重画：
       避免多行 EDIT 在快速追加时出现文字残影/重叠。 */
    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);

    len = GetWindowTextLengthW(edit);
    if (len > 400000) {
        SendMessageW(edit, EM_SETSEL, 0, (LPARAM)120000);
        SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM)L"");
    }
    old_first = (int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM)text);

    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);

    /* 注意：滚动必须在恢复重绘之后做，否则 WM_SETREDRAW(FALSE) 期间
       EM_SCROLLCARET 不生效，表现就是“自动滚动失效”。 */
    if (autoscroll) {
        SendMessageW(edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
    } else {
        edit_scroll_preserve(edit, old_first, FALSE);
    }
    InvalidateRect(edit, NULL, TRUE);
}

static void append_log_line(const wchar_t *text)
{
    wchar_t line[640];
    wchar_t ts[16];
    if (!IsDlgButtonChecked(g_app.wnd, IDC_CHK_SHOWSTATUS)) return;
    time_now_text(ts, 16);
    _snwprintf_s(line, _countof(line), _TRUNCATE, L"[%s] %s\r\n", ts, text);
    append_edit_text(g_app.edit_recv, line);
}

/* ------------------------------------------------------------------ */
/* 接收数据解码                                                        */
/* ------------------------------------------------------------------ */
/* 串口数据里单独的 \r（回车不换行）在 EDIT 里会让后续文字覆盖本行，
   这里统一按换行处理；跨包的 CR/LF 用 cr_pending 记住。 */
static void append_receive_text(const wchar_t *s, int n)
{
    wchar_t *buf;
    int pos = 0;
    int i = 0;

    if (n <= 0) return;
    buf = (wchar_t *)malloc(((size_t)n * 2 + 4) * sizeof(wchar_t));
    if (!buf) return;

    if (g_app.cr_pending) {
        buf[pos++] = L'\r';
        buf[pos++] = L'\n';
        if (s[0] == L'\n') i = 1;
        g_app.cr_pending = FALSE;
    }
    for (; i < n; i++) {
        if (s[i] == L'\r') {
            if (i + 1 < n) {
                buf[pos++] = L'\r';
                buf[pos++] = L'\n';
                if (s[i + 1] == L'\n') i++;
            } else {
                g_app.cr_pending = TRUE;
            }
        } else {
            buf[pos++] = s[i];
        }
    }
    buf[pos] = 0;
    append_edit_text(g_app.edit_recv, buf);
    free(buf);
}
static void rx_append_bytes(const BYTE *data, int len)
{
    wchar_t *out;
    int cap = len * 4 + 64;

    if (len <= 0) return;
    out = (wchar_t *)malloc((size_t)cap * sizeof(wchar_t));
    if (!out) return;

    if (IsDlgButtonChecked(g_app.wnd, IDC_CHK_HEXVIEW)) {
        int pos = 0;
        int i;
        for (i = 0; i < len && pos < cap - 6; i++) {
            static const wchar_t digits[] = L"0123456789ABCDEF";
            out[pos++] = digits[data[i] >> 4];
            out[pos++] = digits[data[i] & 0x0F];
            if ((i + 1) % 32 == 0) {
                out[pos++] = L'\r';
                out[pos++] = L'\n';
            } else {
                out[pos++] = L' ';
            }
        }
        out[pos++] = L'\r';
        out[pos++] = L'\n';
        out[pos] = 0;
        append_edit_text(g_app.edit_recv, out);
        free(out);
        return;
    }

    /* 文本模式：把上次残留的半个 UTF-8 字符接上 */
    {
        BYTE *tmp = (BYTE *)malloc((size_t)(g_app.u8_pending_len + len));
        int total, conv, wlen, tail;
        if (!tmp) { free(out); return; }
        if (g_app.u8_pending_len) memcpy(tmp, g_app.u8_pending, (size_t)g_app.u8_pending_len);
        memcpy(tmp + g_app.u8_pending_len, data, (size_t)len);
        total = g_app.u8_pending_len + len;
        tail = u8_tail_incomplete(tmp, total);
        conv = total - tail;
        wlen = u8_to_w(tmp, conv, out, cap);
        if (wlen > 0) {
            out[wlen] = 0;
            append_edit_text(g_app.edit_recv, out);
        }
        if (tail > 0) memcpy(g_app.u8_pending, tmp + conv, (size_t)tail);
        g_app.u8_pending_len = tail;
        free(tmp);
    }
    free(out);
}

/* ------------------------------------------------------------------ */
/* 状态刷新                                                            */
/* ------------------------------------------------------------------ */
static int current_state_color(void)
{
    BleStatus st;
    ble_get_status(&st);
    switch (st.state) {
    case BLE_STATE_CONNECTED: return 0;
    case BLE_STATE_ERROR: return 2;
    case BLE_STATE_CONNECTING:
    case BLE_STATE_DISCOVERING: return 1;
    default: return 3;
    }
}

static void refresh_status(void)
{
    BleStatus st;
    wchar_t buf[256];

    ble_get_status(&st);

    SetWindowTextW(g_app.lbl_name, st.device_name[0] ? st.device_name : L"—");
    SetWindowTextW(g_app.lbl_addr, st.address[0] ? st.address : L"—");
    SetWindowTextW(g_app.lbl_state, st.message[0] ? st.message : L"—");
    SetWindowTextW(g_app.lbl_svc, st.service_uuid[0] ? st.service_uuid : L"—");
    SetWindowTextW(g_app.lbl_notify, st.notify_uuid[0] ? st.notify_uuid : L"—");
    SetWindowTextW(g_app.lbl_write, st.write_uuid[0] ? st.write_uuid : L"—");
    if (st.state == BLE_STATE_CONNECTED) {
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d 字节%s", st.mtu,
                     st.write_with_response ? L" · 带响应" : L" · 无响应");
    } else {
        wcscpy_s(buf, _countof(buf), L"—");
    }
    SetWindowTextW(g_app.lbl_mtu, buf);

    _snwprintf_s(g_app.rx_text, 64, _TRUNCATE, L"RX  %llu 字节", (unsigned long long)st.rx_bytes);
    _snwprintf_s(g_app.tx_text, 64, _TRUNCATE, L"TX  %llu 字节", (unsigned long long)st.tx_bytes);

    EnableWindow(g_app.btn_connect, (st.state == BLE_STATE_IDLE || st.state == BLE_STATE_ERROR) &&
                                    SendMessageW(g_app.list, LB_GETCURSEL, 0, 0) >= 0);
    EnableWindow(g_app.btn_disconnect, st.state == BLE_STATE_CONNECTED ||
                                       st.state == BLE_STATE_CONNECTING ||
                                       st.state == BLE_STATE_DISCOVERING);
    EnableWindow(g_app.btn_send, st.state == BLE_STATE_CONNECTED);
    EnableWindow(g_app.btn_stop, g_app.scanning ? TRUE : FALSE);

    InvalidateRect(g_app.wnd, NULL, FALSE);
}

/* ------------------------------------------------------------------ */
/* 设备列表                                                            */
/* ------------------------------------------------------------------ */
static void refresh_device_list(void)
{
    int n = ble_device_count();
    int count = (int)SendMessageW(g_app.list, LB_GETCOUNT, 0, 0);
    int sel = (int)SendMessageW(g_app.list, LB_GETCURSEL, 0, 0);
    int want = n > 0 ? n : 1;

    if (want != count) {
        int i;
        SendMessageW(g_app.list, WM_SETREDRAW, FALSE, 0);
        SendMessageW(g_app.list, LB_RESETCONTENT, 0, 0);
        if (n == 0) {
            SendMessageW(g_app.list, LB_ADDSTRING, 0, (LPARAM)L"");
            SendMessageW(g_app.list, LB_SETITEMDATA, 0, (LPARAM)-1);
        } else {
            for (i = 0; i < n; i++) {
                LRESULT idx = SendMessageW(g_app.list, LB_ADDSTRING, 0, (LPARAM)L"");
                SendMessageW(g_app.list, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)i);
            }
        }
        SendMessageW(g_app.list, WM_SETREDRAW, TRUE, 0);
        if (sel >= 0 && sel < want) SendMessageW(g_app.list, LB_SETCURSEL, (WPARAM)sel, 0);
        InvalidateRect(g_app.list, NULL, FALSE);
    } else if (n > 0) {
        InvalidateRect(g_app.list, NULL, FALSE);
    }
    EnableWindow(g_app.btn_connect, n > 0 && !ble_is_connected() &&
                 SendMessageW(g_app.list, LB_GETCURSEL, 0, 0) >= 0);
}

/* ------------------------------------------------------------------ */
/* 绘制                                                                */
/* ------------------------------------------------------------------ */
static void fill_round_rect(HDC dc, const RECT *r, int radius, COLORREF fill, COLORREF border)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, p);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

static void draw_card(HDC dc, const RECT *r, const wchar_t *title)
{
    RECT tr = *r;
    fill_round_rect(dc, r, S(10), CLR_CARD, CLR_BORDER);
    tr.left += S(16);
    tr.top += S(12);
    tr.right -= S(12);
    tr.bottom = tr.top + S(24);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_app.f_ui_bold);
    SetTextColor(dc, CLR_TEXT);
    DrawTextW(dc, title, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

static void draw_info_labels(HDC dc)
{
    static const wchar_t *labels[] = {L"状态", L"名称", L"地址", L"服务", L"通知", L"写入", L"MTU"};
    int x = g_app.card_info.left + S(14);
    int y = g_app.card_info.top + S(38);
    int row_h = S(25);
    int i;

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_app.f_small);
    SetTextColor(dc, CLR_SUBTEXT);
    for (i = 0; i < 7; i++) {
        RECT r;
        r.left = x;
        r.top = y + i * row_h;
        r.right = x + S(44);
        r.bottom = r.top + row_h;
        DrawTextW(dc, labels[i], -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void draw_header(HDC dc, const RECT *client)
{
    RECT r;
    wchar_t state[64];
    COLORREF fg, bg;
    BleStatus st;

    ble_get_status(&st);
    r.left = S(20);
    r.top = S(14);
    r.right = client->right - S(160);
    r.bottom = r.top + S(32);
    SelectObject(dc, g_app.f_title);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, CLR_TEXT);
    DrawTextW(dc, APP_NAME, -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    r.top = r.bottom;
    r.bottom = r.top + S(20);
    SelectObject(dc, g_app.f_sub);
    SetTextColor(dc, CLR_SUBTEXT);
    DrawTextW(dc, APP_SUBTITLE, -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    switch (st.state) {
    case BLE_STATE_CONNECTED:
        wcscpy_s(state, 64, L"已连接");
        fg = CLR_GREEN;
        bg = CLR_GREEN_BG;
        break;
    case BLE_STATE_CONNECTING:
    case BLE_STATE_DISCOVERING:
        wcscpy_s(state, 64, L"连接中…");
        fg = CLR_AMBER;
        bg = CLR_AMBER_BG;
        break;
    case BLE_STATE_ERROR:
        wcscpy_s(state, 64, L"连接失败");
        fg = CLR_RED;
        bg = CLR_RED_BG;
        break;
    default:
        wcscpy_s(state, 64, L"未连接");
        fg = CLR_SUBTEXT;
        bg = RGB(238, 240, 244);
        break;
    }
    {
        RECT pill;
        int w = S(112);
        pill.left = client->right - S(20) - w;
        pill.right = client->right - S(20);
        pill.top = S(18);
        pill.bottom = pill.top + S(30);
        fill_round_rect(dc, &pill, S(15), bg, bg);
        {
            RECT dot;
            HBRUSH b = CreateSolidBrush(fg);
            int d = S(8);
            dot.left = pill.left + S(14);
            dot.top = pill.top + (S(30) - d) / 2;
            dot.right = dot.left + d;
            dot.bottom = dot.top + d;
            SelectObject(dc, b);
            SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
            DeleteObject(b);
        }
        {
            RECT tr = pill;
            tr.left += S(28);
            SelectObject(dc, g_app.f_ui_bold);
            SetTextColor(dc, fg);
            DrawTextW(dc, state, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
    }
}

static void draw_status_bar(HDC dc, const RECT *client)
{
    RECT bar, line, tr;

    bar = *client;
    bar.top = bar.bottom - S(30);
    line = bar;
    line.bottom = line.top + 1;

    {
        HBRUSH b = CreateSolidBrush(CLR_CARD);
        FillRect(dc, &bar, b);
        DeleteObject(b);
        b = CreateSolidBrush(CLR_BORDER);
        FillRect(dc, &line, b);
        DeleteObject(b);
    }

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_app.f_small);

    tr = bar;
    tr.left += S(20);
    tr.right -= S(180);
    SetTextColor(dc, g_app.radio_ok ? CLR_GREEN : CLR_RED);
    DrawTextW(dc, g_app.adapter_text, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    tr = bar;
    tr.left += S(260);
    SetTextColor(dc, CLR_SUBTEXT);
    DrawTextW(dc, g_app.rx_text, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    tr.left += S(120);
    DrawTextW(dc, g_app.tx_text, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    {
        const wchar_t *loglink = L"打开日志";
        SIZE sz;
        GetTextExtentPoint32W(dc, loglink, (int)wcslen(loglink), &sz);
        g_app.link_log_rect = bar;
        g_app.link_log_rect.right = bar.right - S(20);
        g_app.link_log_rect.left = g_app.link_log_rect.right - sz.cx;
        g_app.link_log_rect.top = bar.top + S(4);
        g_app.link_log_rect.bottom = bar.bottom - S(4);
        SetTextColor(dc, CLR_ACCENT);
        DrawTextW(dc, loglink, -1, &g_app.link_log_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

static void paint_window(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    HDC mem;
    HBITMAP bmp;
    HGDIOBJ old;

    GetClientRect(hwnd, &client);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, client.right, client.bottom);
    old = SelectObject(mem, bmp);

    {
        HBRUSH b = CreateSolidBrush(CLR_BG);
        FillRect(mem, &client, b);
        DeleteObject(b);
    }

    draw_header(mem, &client);
    draw_card(mem, &g_app.card_dev, L"蓝牙设备");
    draw_card(mem, &g_app.card_info, L"连接信息");
    draw_card(mem, &g_app.card_recv, L"接收区");
    draw_card(mem, &g_app.card_send, L"发送区");
    draw_info_labels(mem);
    draw_status_bar(mem, &client);

    BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* ------------------------------------------------------------------ */
/* 控件创建与布局                                                      */
/* ------------------------------------------------------------------ */
static HWND create_ctl(const wchar_t *cls, const wchar_t *text, DWORD style, DWORD exstyle, int id)
{
    return CreateWindowExW(exstyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                           0, 0, 10, 10, g_app.wnd, (HMENU)(INT_PTR)id, g_app.inst, NULL);
}

static void set_font(HWND h, HFONT f)
{
    SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
}

/* 发送框：Ctrl+Enter 直接发送 */
static LRESULT CALLBACK SendEditSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR id, DWORD_PTR ref)
{
    (void)id;
    (void)ref;
    if (msg == WM_KEYDOWN && wp == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        /* 长按会连续产生 WM_KEYDOWN：只处理第一次，忽略自动重复 */
        if ((lp & 0x40000000) == 0) {
            PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_BTN_SEND, BN_CLICKED), 0);
        }
        return 0;
    }
    /* TranslateMessage 仍会给编辑框发 WM_CHAR，Ctrl 按住时不要插入换行 */
    if (msg == WM_CHAR && (wp == L'\r' || wp == L'\n') && (GetKeyState(VK_CONTROL) & 0x8000)) {
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void create_controls(HWND hwnd)
{
    (void)hwnd;
    g_app.list = create_ctl(L"LISTBOX", L"",
                            LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL |
                            WS_TABSTOP | WS_CLIPSIBLINGS, 0, IDC_LIST_DEV);
    g_app.btn_scan = create_ctl(L"BUTTON", L"扫描", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_BTN_SCAN);
    g_app.btn_stop = create_ctl(L"BUTTON", L"停止", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_BTN_STOP);
    g_app.btn_connect = create_ctl(L"BUTTON", L"连接", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_BTN_CONNECT);
    g_app.btn_disconnect = create_ctl(L"BUTTON", L"断开", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_BTN_DISCONNECT);

    g_app.lbl_name = create_ctl(L"STATIC", L"—", SS_LEFT | SS_ENDELLIPSIS, 0, IDC_LBL_NAME);
    g_app.lbl_addr = create_ctl(L"STATIC", L"—", SS_LEFT | SS_ENDELLIPSIS, 0, IDC_LBL_ADDR);
    g_app.lbl_state = create_ctl(L"STATIC", L"—", SS_LEFT | SS_ENDELLIPSIS, 0, IDC_LBL_STATE);
    g_app.lbl_svc = create_ctl(L"STATIC", L"—", SS_LEFT | SS_ENDELLIPSIS, 0, IDC_LBL_SVC);
    g_app.lbl_notify = create_ctl(L"STATIC", L"—", SS_LEFT | SS_ENDELLIPSIS, 0, IDC_LBL_NOTIFY);
    g_app.lbl_write = create_ctl(L"STATIC", L"—", SS_LEFT | SS_ENDELLIPSIS, 0, IDC_LBL_WRITE);
    g_app.lbl_mtu = create_ctl(L"STATIC", L"23 字节", SS_LEFT | SS_ENDELLIPSIS, 0, IDC_LBL_MTU);

    g_app.btn_clear_recv = create_ctl(L"BUTTON", L"清空接收", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_BTN_CLEAR_RECV);
    g_app.btn_save = create_ctl(L"BUTTON", L"保存…", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_BTN_SAVE);
    g_app.chk_autoscroll = create_ctl(L"BUTTON", L"自动滚动", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_CHK_AUTOSCROLL);
    g_app.chk_hexview = create_ctl(L"BUTTON", L"HEX 显示", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_CHK_HEXVIEW);
    g_app.chk_showstatus = create_ctl(L"BUTTON", L"状态提示", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_CHK_SHOWSTATUS);
    g_app.chk_plot = create_ctl(L"BUTTON", L"曲线", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_CHK_PLOT);
    g_app.edit_recv = create_ctl(L"EDIT", L"",
                                 ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL |
                                 WS_TABSTOP | WS_CLIPSIBLINGS, WS_EX_CLIENTEDGE, IDC_EDIT_RECV);

    g_app.btn_clear_send = create_ctl(L"BUTTON", L"清空发送", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_BTN_CLEAR_SEND);
    g_app.chk_hexsend = create_ctl(L"BUTTON", L"HEX 发送", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_CHK_HEXSEND);
    g_app.lbl_eol = create_ctl(L"STATIC", L"行尾", SS_LEFT, 0, IDC_LBL_EOL);
    g_app.cmb_eol = create_ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDC_CMB_EOL);
    g_app.lbl_chunk = create_ctl(L"STATIC", L"分包", SS_LEFT, 0, IDC_LBL_CHUNK);
    g_app.edit_chunk = create_ctl(L"EDIT", L"20", ES_NUMBER | ES_CENTER | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_EDIT_CHUNK);
    g_app.lbl_interval = create_ctl(L"STATIC", L"间隔(ms)", SS_LEFT, 0, IDC_LBL_INTERVAL);
    g_app.edit_interval = create_ctl(L"EDIT", L"20", ES_NUMBER | ES_CENTER | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_EDIT_INTERVAL);
    g_app.edit_send = create_ctl(L"EDIT", L"",
                                 ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL |
                                 WS_TABSTOP | WS_CLIPSIBLINGS, WS_EX_CLIENTEDGE, IDC_EDIT_SEND);
    g_app.btn_send = create_ctl(L"BUTTON", L"发送", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_BTN_SEND);
    g_app.lbl_sendhint = create_ctl(L"STATIC", L"Ctrl+Enter 快速发送", SS_LEFT, 0, IDC_LBL_SENDHINT);

    plot_create(g_app.wnd);

    SendMessageW(g_app.cmb_eol, CB_ADDSTRING, 0, (LPARAM)L"无行尾");
    SendMessageW(g_app.cmb_eol, CB_ADDSTRING, 0, (LPARAM)L"CRLF (\\r\\n)");
    SendMessageW(g_app.cmb_eol, CB_ADDSTRING, 0, (LPARAM)L"LF (\\n)");
    SendMessageW(g_app.cmb_eol, CB_SETCURSEL, 1, 0);

    SendMessageW(g_app.edit_recv, EM_SETLIMITTEXT, (WPARAM)2000000, 0);
    SendMessageW(g_app.edit_send, EM_SETLIMITTEXT, (WPARAM)1000000, 0);

    CheckDlgButton(g_app.wnd, IDC_CHK_AUTOSCROLL, BST_CHECKED);
    CheckDlgButton(g_app.wnd, IDC_CHK_SHOWSTATUS, BST_CHECKED);

    set_font(g_app.list, g_app.f_ui);
    set_font(g_app.btn_scan, g_app.f_ui);
    set_font(g_app.btn_stop, g_app.f_ui);
    set_font(g_app.btn_connect, g_app.f_ui);
    set_font(g_app.btn_disconnect, g_app.f_ui);
    set_font(g_app.lbl_name, g_app.f_ui);
    set_font(g_app.lbl_addr, g_app.f_ui);
    set_font(g_app.lbl_state, g_app.f_small);
    set_font(g_app.lbl_svc, g_app.f_small);
    set_font(g_app.lbl_notify, g_app.f_small);
    set_font(g_app.lbl_write, g_app.f_small);
    set_font(g_app.lbl_mtu, g_app.f_small);
    set_font(g_app.btn_clear_recv, g_app.f_ui);
    set_font(g_app.btn_save, g_app.f_ui);
    set_font(g_app.chk_autoscroll, g_app.f_ui);
    set_font(g_app.chk_hexview, g_app.f_ui);
    set_font(g_app.chk_showstatus, g_app.f_ui);
    set_font(g_app.edit_recv, g_app.f_mono);
    set_font(g_app.btn_clear_send, g_app.f_ui);
    set_font(g_app.chk_hexsend, g_app.f_ui);
    set_font(g_app.lbl_eol, g_app.f_ui);
    set_font(g_app.cmb_eol, g_app.f_ui);
    set_font(g_app.lbl_chunk, g_app.f_ui);
    set_font(g_app.edit_chunk, g_app.f_ui);
    set_font(g_app.lbl_interval, g_app.f_ui);
    set_font(g_app.edit_interval, g_app.f_ui);
    set_font(g_app.edit_send, g_app.f_mono);
    set_font(g_app.btn_send, g_app.f_ui);
    set_font(g_app.lbl_sendhint, g_app.f_small);

    SetWindowSubclass(g_app.edit_send, SendEditSubclass, 1, 0);
}

static void layout(HWND hwnd)
{
    RECT client;
    int margin = S(16);
    int header_h = S(62);
    int status_h = S(30);
    int gap = S(14);
    int left_w = S(348);
    int content_top, content_h;
    int right_x, right_w;
    int recv_h;
    int pad = S(14);
    int btn_h = S(30);

    GetClientRect(hwnd, &client);
    content_top = header_h;
    content_h = client.bottom - status_h - S(8) - content_top;
    if (content_h < S(320)) content_h = S(320);

    right_x = margin + left_w + gap;
    right_w = client.right - margin - right_x;
    if (right_w < S(380)) right_w = S(380);

    {
        int dev_h = content_h * 55 / 100;
        if (dev_h < S(280)) dev_h = S(280);
        if (dev_h > content_h - S(220)) dev_h = content_h - S(220);
        if (dev_h < S(240)) dev_h = S(240);
        g_app.card_dev.left = margin;
        g_app.card_dev.top = content_top;
        g_app.card_dev.right = margin + left_w;
        g_app.card_dev.bottom = content_top + dev_h;

        g_app.card_info.left = margin;
        g_app.card_info.top = g_app.card_dev.bottom + gap;
        g_app.card_info.right = margin + left_w;
        g_app.card_info.bottom = content_top + content_h;
    }

    recv_h = content_h * 58 / 100;
    if (recv_h < S(220)) recv_h = S(220);
    g_app.card_recv.left = right_x;
    g_app.card_recv.top = content_top;
    g_app.card_recv.right = right_x + right_w;
    g_app.card_recv.bottom = content_top + recv_h;
    g_app.card_send.left = right_x;
    g_app.card_send.top = g_app.card_recv.bottom + gap;
    g_app.card_send.right = right_x + right_w;
    g_app.card_send.bottom = content_top + content_h;

    /* 设备列表 */
    {
        int x = g_app.card_dev.left + pad;
        int y = g_app.card_dev.top + S(40);
        int w = (g_app.card_dev.right - g_app.card_dev.left) - pad * 2;
        int list_h = (g_app.card_dev.bottom - S(12) - btn_h - S(10)) - y;
        int bw;
        if (list_h < S(80)) list_h = S(80);
        MoveWindow(g_app.list, x, y, w, list_h, TRUE);
        y += list_h + S(10);
        bw = (w - S(24)) / 4;
        MoveWindow(g_app.btn_scan, x, y, bw, btn_h, TRUE);
        MoveWindow(g_app.btn_stop, x + bw + S(8), y, bw, btn_h, TRUE);
        MoveWindow(g_app.btn_connect, x + (bw + S(8)) * 2, y, bw, btn_h, TRUE);
        MoveWindow(g_app.btn_disconnect, x + (bw + S(8)) * 3, y, bw, btn_h, TRUE);
    }

    /* 连接信息 */
    {
        HWND values[] = {g_app.lbl_state, g_app.lbl_name, g_app.lbl_addr, g_app.lbl_svc,
                         g_app.lbl_notify, g_app.lbl_write, g_app.lbl_mtu};
        int x = g_app.card_info.left + pad;
        int y = g_app.card_info.top + S(38);
        int w = (g_app.card_info.right - g_app.card_info.left) - pad * 2;
        int row_h = S(25);
        int i;
        for (i = 0; i < 7; i++) {
            MoveWindow(values[i], x + S(48), y + i * row_h, w - S(48), row_h, TRUE);
        }
    }

    /* 接收区 */
    {
        int x = g_app.card_recv.left + pad;
        int y = g_app.card_recv.top + S(40);
        int w = (g_app.card_recv.right - g_app.card_recv.left) - pad * 2;
        int cx = x;
        MoveWindow(g_app.btn_clear_recv, cx, y, S(88), btn_h, TRUE);
        cx += S(96);
        MoveWindow(g_app.btn_save, cx, y, S(72), btn_h, TRUE);
        cx += S(88);
        MoveWindow(g_app.chk_autoscroll, cx, y + S(3), S(84), S(24), TRUE);
        cx += S(86);
        MoveWindow(g_app.chk_hexview, cx, y + S(3), S(76), S(24), TRUE);
        cx += S(78);
        MoveWindow(g_app.chk_showstatus, cx, y + S(3), S(80), S(24), TRUE);
        cx += S(82);
        MoveWindow(g_app.chk_plot, cx, y + S(3), S(58), S(24), TRUE);
        y += btn_h + S(8);
        MoveWindow(g_app.edit_recv, x, y, w, (g_app.card_recv.bottom - S(12)) - y, TRUE);
        {
            RECT pr;
            GetWindowRect(g_app.edit_recv, &pr);
            MapWindowPoints(NULL, g_app.wnd, (POINT *)&pr, 2);
            plot_layout(&pr);
            ShowWindow(g_app.edit_recv, plot_is_visible() ? SW_HIDE : SW_SHOW);
        }
    }

    /* 发送区 */
    {
        int x = g_app.card_send.left + pad;
        int y = g_app.card_send.top + S(40);
        int w = (g_app.card_send.right - g_app.card_send.left) - pad * 2;
        int cx = x;
        MoveWindow(g_app.btn_clear_send, cx, y, S(88), btn_h, TRUE);
        cx += S(96);
        MoveWindow(g_app.chk_hexsend, cx, y + S(3), S(84), S(24), TRUE);
        cx += S(94);
        MoveWindow(g_app.lbl_eol, cx, y, S(32), btn_h, TRUE);
        cx += S(34);
        MoveWindow(g_app.cmb_eol, cx, y, S(104), S(220), TRUE);
        cx += S(112);
        MoveWindow(g_app.lbl_chunk, cx, y, S(32), btn_h, TRUE);
        cx += S(34);
        MoveWindow(g_app.edit_chunk, cx, y + S(2), S(46), S(24), TRUE);
        cx += S(52);
        MoveWindow(g_app.lbl_interval, cx, y, S(56), btn_h, TRUE);
        cx += S(58);
        MoveWindow(g_app.edit_interval, cx, y + S(2), S(46), S(24), TRUE);

        y += btn_h + S(8);
        {
            int bottom_row = S(36);
            int edit_h = (g_app.card_send.bottom - S(12) - bottom_row - S(8)) - y;
            if (edit_h < S(60)) edit_h = S(60);
            MoveWindow(g_app.edit_send, x, y, w, edit_h, TRUE);
            y += edit_h + S(8);
            MoveWindow(g_app.lbl_sendhint, x, y, S(200), bottom_row, TRUE);
            MoveWindow(g_app.btn_send, x + w - S(110), y, S(110), bottom_row, TRUE);
        }
    }
}

/* ------------------------------------------------------------------ */
/* owner-draw                                                          */
/* ------------------------------------------------------------------ */
static void draw_button(const DRAWITEMSTRUCT *dis)
{
    BtnKind kind = button_kind((int)dis->CtlID);
    BOOL disabled = (dis->itemState & ODS_DISABLED) ? TRUE : FALSE;
    BOOL pressed = (dis->itemState & ODS_SELECTED) ? TRUE : FALSE;
    RECT r = dis->rcItem;
    COLORREF fill, border, text;
    wchar_t label[64];

    GetWindowTextW(dis->hwndItem, label, 64);

    if (kind == BTN_PRIMARY) {
        fill = disabled ? RGB(196, 205, 216) : (pressed ? CLR_ACCENT_D : CLR_ACCENT);
        border = fill;
        text = RGB(255, 255, 255);
    } else if (kind == BTN_DANGER) {
        fill = disabled ? RGB(245, 245, 245) : (pressed ? RGB(250, 235, 233) : RGB(255, 255, 255));
        border = disabled ? RGB(220, 224, 230) : RGB(236, 190, 184);
        text = disabled ? RGB(160, 168, 178) : CLR_RED;
    } else {
        fill = disabled ? RGB(245, 246, 248) : (pressed ? RGB(236, 240, 245) : RGB(255, 255, 255));
        border = disabled ? RGB(220, 224, 230) : RGB(206, 212, 220);
        text = disabled ? RGB(160, 168, 178) : CLR_TEXT;
    }

    fill_round_rect(dis->hDC, &r, S(7), fill, border);
    SelectObject(dis->hDC, g_app.f_ui);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text);
    DrawTextW(dis->hDC, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void draw_list_item(const DRAWITEMSTRUCT *dis)
{
    LRESULT data = SendMessageW(dis->hwndItem, LB_GETITEMDATA, (WPARAM)dis->itemID, 0);
    RECT r = dis->rcItem;
    BOOL selected = (dis->itemState & ODS_SELECTED) ? TRUE : FALSE;
    HBRUSH b;
    BleDevice dev;

    b = CreateSolidBrush(selected ? RGB(232, 241, 253) : CLR_CARD);
    FillRect(dis->hDC, &r, b);
    DeleteObject(b);
    if (selected) {
        HPEN p = CreatePen(PS_SOLID, 1, RGB(198, 219, 245));
        HGDIOBJ op = SelectObject(dis->hDC, p);
        HGDIOBJ ob = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
        SelectObject(dis->hDC, op);
        SelectObject(dis->hDC, ob);
        DeleteObject(p);
    }

    if ((int)data < 0 || !ble_device_get((int)data, &dev)) {
        RECT tr = r;
        tr.left += S(10);
        SelectObject(dis->hDC, g_app.f_ui);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, CLR_SUBTEXT);
        DrawTextW(dis->hDC, L"点击「扫描」查找附近的 BLE 设备", -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    {
        RECT tr = r;
        wchar_t mac[32];
        wchar_t sub[96];
        tr.left += S(12);
        tr.right -= S(10);
        tr.top += S(6);
        tr.bottom = tr.top + S(18);
        SelectObject(dis->hDC, g_app.f_ui_bold);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, CLR_TEXT);
        DrawTextW(dis->hDC, dev.name[0] ? dev.name : L"未知设备", -1, &tr,
                  DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        bt_addr_format(dev.address, mac, 32);
        _snwprintf_s(sub, _countof(sub), _TRUNCATE, L"%s   %d dBm", mac, dev.rssi);
        tr.top += S(18);
        tr.bottom = tr.top + S(16);
        SelectObject(dis->hDC, g_app.f_small);
        SetTextColor(dis->hDC, CLR_SUBTEXT);
        DrawTextW(dis->hDC, sub, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

/* ------------------------------------------------------------------ */
/* 业务动作                                                            */
/* ------------------------------------------------------------------ */
static BOOL query_radio(void)
{
    BLUETOOTH_FIND_RADIO_PARAMS params;
    HBLUETOOTH_RADIO_FIND find;
    HANDLE radio = NULL;
    BOOL ok = FALSE;

    params.dwSize = sizeof(params);
    find = BluetoothFindFirstRadio(&params, &radio);
    if (find) {
        ok = TRUE;
        if (radio) CloseHandle(radio);
        BluetoothFindRadioClose(find);
    }
    return ok;
}

static void do_scan(void)
{
    log_write(L"[ui] scan clicked");
    if (g_app.scanning) ble_scan_stop();
    ble_devices_clear();
    refresh_device_list();
    if (ble_scan_start()) {
        g_app.scanning = TRUE;
        g_app.radio_ok = TRUE;
        wcscpy_s(g_app.adapter_text, 64, L"蓝牙适配器：可用");
        SetWindowTextW(g_app.btn_scan, L"重新扫描");
    } else {
        g_app.scanning = FALSE;
        g_app.radio_ok = query_radio();
        wcscpy_s(g_app.adapter_text, 64,
                 g_app.radio_ok ? L"蓝牙适配器：可用，但扫描失败" : L"蓝牙适配器：未就绪，请检查蓝牙开关");
    }
    refresh_status();
}

static void do_connect(void)
{
    int sel = (int)SendMessageW(g_app.list, LB_GETCURSEL, 0, 0);
    LRESULT data;
    BleDevice dev;

    if (sel < 0) {
        MessageBoxW(g_app.wnd, L"请先在列表中选择一个 BLE 设备。", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    data = SendMessageW(g_app.list, LB_GETITEMDATA, (WPARAM)sel, 0);
    if ((int)data < 0 || !ble_device_get((int)data, &dev)) return;
    if (g_app.scanning) {
        ble_scan_stop();
        g_app.scanning = FALSE;
        SetWindowTextW(g_app.btn_scan, L"扫描");
    }
    log_write(L"[ui] connect to %s type=%d", dev.name[0] ? dev.name : L"(unnamed)", dev.address_type);
    ble_connect(dev.address, dev.address_type, dev.name);
    refresh_status();
}

static void do_send(void)
{
    wchar_t *text;
    int tlen;
    uint8_t *bytes = NULL;
    int blen = 0;
    int chunk, interval;
    int rc;
    BOOL hex = IsDlgButtonChecked(g_app.wnd, IDC_CHK_HEXSEND) == BST_CHECKED;
    static DWORD last_send_tick = 0;
    DWORD now = GetTickCount();

    /* 连续点击 / Ctrl+Enter 自动重复：300ms 内只发一次 */
    if (now - last_send_tick < 300) return;
    last_send_tick = now;

    tlen = GetWindowTextLengthW(g_app.edit_send);
    if (tlen <= 0) return;
    text = (wchar_t *)malloc(((size_t)tlen + 1) * sizeof(wchar_t));
    if (!text) return;
    GetWindowTextW(g_app.edit_send, text, tlen + 1);

    if (hex) {
        int cap = tlen + 4;
        bytes = (uint8_t *)malloc((size_t)cap);
        if (!bytes) { free(text); return; }
        blen = hex_parse(text, bytes, cap);
        if (blen < 0) {
            MessageBoxW(g_app.wnd,
                        L"十六进制格式不正确。\n请用空格分隔字节，例如：48 65 6C 6C 6F 0D 0A",
                        APP_NAME, MB_ICONWARNING);
            free(text);
            free(bytes);
            return;
        }
    } else {
        int need = WideCharToMultiByte(CP_UTF8, 0, text, tlen, NULL, 0, NULL, NULL);
        int eol = (int)SendMessageW(g_app.cmb_eol, CB_GETCURSEL, 0, 0);
        if (need < 0) need = 0;
        bytes = (uint8_t *)malloc((size_t)need + 3);
        if (!bytes) { free(text); return; }
        blen = WideCharToMultiByte(CP_UTF8, 0, text, tlen, (char *)bytes, need, NULL, NULL);
        if (blen < 0) blen = 0;
        if (eol == 1) {
            bytes[blen++] = '\r';
            bytes[blen++] = '\n';
        } else if (eol == 2) {
            bytes[blen++] = '\n';
        }
    }
    free(text);

    if (blen <= 0) {
        free(bytes);
        return;
    }

    chunk = GetDlgItemInt(g_app.wnd, IDC_EDIT_CHUNK, NULL, FALSE);
    interval = GetDlgItemInt(g_app.wnd, IDC_EDIT_INTERVAL, NULL, FALSE);
    if (chunk <= 0) chunk = 20;
    if (chunk > 512) chunk = 512;

    rc = ble_send(bytes, blen, chunk, interval);
    if (rc == BLE_ERR_NOT_CONN) {
        MessageBoxW(g_app.wnd, L"尚未连接 BLE 设备。", APP_NAME, MB_ICONINFORMATION);
    } else if (rc == BLE_ERR_BUSY) {
        MessageBoxW(g_app.wnd, L"发送队列已满，正在发送上一批数据，请稍候再试。",
                    APP_NAME, MB_ICONINFORMATION);
    } else if (rc != BLE_OK) {
        MessageBoxW(g_app.wnd, L"发送失败（内存不足或数据为空）。", APP_NAME, MB_ICONWARNING);
    }
    free(bytes);
}

static void do_save(void)
{
    OPENFILENAMEW ofn;
    wchar_t path[MAX_PATH] = L"ble_log.txt";
    HANDLE file;
    DWORD written;
    wchar_t *text;
    int tlen;
    char *utf8;
    int ulen;
    static const BYTE bom[3] = {0xEF, 0xBB, 0xBF};

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.wnd;
    ofn.lpstrFilter = L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    tlen = GetWindowTextLengthW(g_app.edit_recv);
    text = (wchar_t *)malloc(((size_t)tlen + 1) * sizeof(wchar_t));
    if (!text) return;
    GetWindowTextW(g_app.edit_recv, text, tlen + 1);
    ulen = WideCharToMultiByte(CP_UTF8, 0, text, tlen, NULL, 0, NULL, NULL);
    utf8 = (char *)malloc((size_t)ulen + 1);
    if (!utf8) { free(text); return; }
    WideCharToMultiByte(CP_UTF8, 0, text, tlen, utf8, ulen, NULL, NULL);

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_app.wnd, L"无法写入文件。", APP_NAME, MB_ICONERROR);
    } else {
        WriteFile(file, bom, 3, &written, NULL);
        WriteFile(file, utf8, (DWORD)ulen, &written, NULL);
        CloseHandle(file);
    }
    free(text);
    free(utf8);
}

static void open_log_file(void)
{
    /* 不依赖系统记事本/文件关联，直接在本程序里看日志 */
    logview_show(g_app.wnd);
}

/* ------------------------------------------------------------------ */
/* BLE 回调                                                            */
/* ------------------------------------------------------------------ */
static void on_ble_status(void)
{
    BleStatus st;
    ble_get_status(&st);
    log_write(L"[state] %s", st.message);
    if (g_app.wnd) PostMessageW(g_app.wnd, WM_APP_BLE_STATUS, 0, 0);
}

static void on_ble_log(const wchar_t *line)
{
    wchar_t *copy;
    size_t len;
    log_write(L"[ble] %s", line);
    if (!g_app.wnd || !line) return;
    len = wcslen(line);
    copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!copy) return;
    memcpy(copy, line, (len + 1) * sizeof(wchar_t));
    if (!PostMessageW(g_app.wnd, WM_APP_BLE_LOG, 0, (LPARAM)copy)) free(copy);
}

/* ------------------------------------------------------------------ */
/* 窗口过程                                                            */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        log_write(L"[ui] WM_CREATE");
        g_app.wnd = hwnd;
        g_app.br_bg = CreateSolidBrush(CLR_BG);
        g_app.br_card = CreateSolidBrush(CLR_CARD);
        create_fonts();
        create_controls(hwnd);
        SetTimer(hwnd, 1, 300, NULL);
        SetTimer(hwnd, 2, 50, NULL);
        SetTimer(hwnd, 3, 1000, NULL);
        refresh_status();
        return 0;

    case WM_SIZE:
        layout(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint_window(hwnd);
        return 0;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = S(1060);
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = S(660);
        return 0;

    case WM_TIMER:
        if (wp == 1) {
            refresh_device_list();
        } else if (wp == 2) {
            BYTE buf[4096];
            int n;
            while ((n = ble_rx_read(buf, (int)sizeof(buf))) > 0) {
                plot_feed(buf, n);
                rx_append_bytes(buf, n);
            }
        } else if (wp == 3) {
            static uint64_t last_rx = 0, last_tx = 0;
            BleStatus st;
            ble_get_status(&st);
            if (st.rx_bytes != last_rx || st.tx_bytes != last_tx) {
                last_rx = st.rx_bytes;
                last_tx = st.tx_bytes;
                _snwprintf_s(g_app.rx_text, 64, _TRUNCATE, L"RX  %llu 字节",
                             (unsigned long long)st.rx_bytes);
                _snwprintf_s(g_app.tx_text, 64, _TRUNCATE, L"TX  %llu 字节",
                             (unsigned long long)st.tx_bytes);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            ble_poll();
        }
        return 0;

    case WM_APP_BLE_STATUS:
        refresh_status();
        return 0;

    case WM_APP_BLE_LOG:
        if (lp) {
            append_log_line((const wchar_t *)lp);
            free((void *)lp);
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        {
            HDC dc = (HDC)wp;
            HWND ctl = (HWND)lp;
            int c;
            SetBkMode(dc, TRANSPARENT);
            if (ctl == g_app.lbl_state) {
                c = current_state_color();
                SetTextColor(dc, c == 0 ? CLR_GREEN : c == 2 ? CLR_RED : c == 1 ? CLR_AMBER : CLR_SUBTEXT);
            } else if (ctl == g_app.lbl_name || ctl == g_app.lbl_addr) {
                SetTextColor(dc, CLR_TEXT);
            } else {
                SetTextColor(dc, CLR_SUBTEXT);
            }
            return (LRESULT)g_app.br_card;
        }

    case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
            if (dis->CtlID == IDC_LIST_DEV) {
                draw_list_item(dis);
            } else {
                draw_button(dis);
            }
            return TRUE;
        }

    case WM_MEASUREITEM:
        {
            MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lp;
            if (mis->CtlID == IDC_LIST_DEV) {
                mis->itemHeight = S(46);
                return TRUE;
            }
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BTN_SCAN:
            do_scan();
            break;
        case IDC_BTN_STOP:
            if (g_app.scanning) {
                ble_scan_stop();
                g_app.scanning = FALSE;
                SetWindowTextW(g_app.btn_scan, L"扫描");
            }
            refresh_status();
            break;
        case IDC_BTN_CONNECT:
            do_connect();
            break;
        case IDC_BTN_DISCONNECT:
            log_write(L"[ui] disconnect clicked");
            ble_disconnect();
            refresh_status();
            break;
        case IDC_BTN_CLEAR_RECV:
            SetWindowTextW(g_app.edit_recv, L"");
            g_app.u8_pending_len = 0;
            plot_clear();
            break;
        case IDC_BTN_CLEAR_SEND:
            SetWindowTextW(g_app.edit_send, L"");
            break;
        case IDC_BTN_SAVE:
            do_save();
            break;
        case IDC_CHK_PLOT:
            plot_show(IsDlgButtonChecked(g_app.wnd, IDC_CHK_PLOT) == BST_CHECKED);
            layout(hwnd);
            break;
        case IDC_BTN_SEND:
            do_send();
            break;
        case IDC_LIST_DEV:
            if (HIWORD(wp) == LBN_DBLCLK) do_connect();
            if (HIWORD(wp) == LBN_SELCHANGE) refresh_status();
            break;
        default:
            break;
        }
        return 0;

    case WM_LBUTTONUP:
        {
            POINT pt;
            pt.x = GET_X_LPARAM(lp);
            pt.y = GET_Y_LPARAM(lp);
            if (PtInRect(&g_app.link_log_rect, pt)) {
                log_write(L"[ui] open log window");
                open_log_file();
            }
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (PtInRect(&g_app.link_log_rect, pt)) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_DPICHANGED:
        {
            RECT *r = (RECT *)lp;
            HWND controls[] = {g_app.list, g_app.btn_scan, g_app.btn_stop, g_app.btn_connect,
                               g_app.btn_disconnect, g_app.lbl_name, g_app.lbl_addr, g_app.lbl_state,
                               g_app.lbl_svc, g_app.lbl_notify, g_app.lbl_write, g_app.lbl_mtu,
                               g_app.btn_clear_recv, g_app.btn_save, g_app.chk_autoscroll,
                               g_app.chk_hexview, g_app.chk_showstatus, g_app.edit_recv,
                               g_app.btn_clear_send, g_app.chk_hexsend, g_app.cmb_eol, g_app.edit_chunk,
                               g_app.edit_interval, g_app.edit_send, g_app.btn_send, g_app.lbl_sendhint,
                               g_app.lbl_chunk, g_app.lbl_interval, g_app.lbl_eol};
            size_t i;

            g_app.dpi = HIWORD(wp);
            destroy_fonts();
            create_fonts();
            for (i = 0; i < _countof(controls); i++) {
                HFONT f = (controls[i] == g_app.edit_recv || controls[i] == g_app.edit_send)
                              ? g_app.f_mono : g_app.f_ui;
                set_font(controls[i], f);
            }
            SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout(hwnd);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;

    case WM_DESTROY:
        log_write(L"[ui] WM_DESTROY");
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        KillTimer(hwnd, 3);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */
static void enable_dpi_awareness(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
    SetCtxFn set_ctx = (SetCtxFn)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (set_ctx) {
        if (set_ctx((HANDLE)-4)) return;   /* PER_MONITOR_AWARE_V2 */
    }
    SetProcessDPIAware();
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    INITCOMMONCONTROLSEX icc;
    BleCallbacks cb;
    HDC screen;

    (void)prev;
    (void)cmd;
    enable_dpi_awareness();

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    g_app.inst = inst;
    g_app.dpi = 96;
    screen = GetDC(NULL);
    if (screen) {
        g_app.dpi = (UINT)GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(NULL, screen);
    }
    if (g_app.dpi < 96) g_app.dpi = 96;
    log_init();
    log_install_crash_handler();
    log_write(L"=== BLE UART Host start, dpi=%u ===", g_app.dpi);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, L"IDI_APPICON");
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = NULL;
    wc.lpszClassName = APP_CLASS;
    if (!RegisterClassExW(&wc)) return 1;

    RoInitialize(RO_INIT_MULTITHREADED);
    cb.on_status_changed = on_ble_status;
    cb.on_log = on_ble_log;
    ble_init(&cb);

    hwnd = CreateWindowExW(0, APP_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, S(1180), S(780),
                           NULL, NULL, inst, NULL);
    if (!hwnd) return 1;

    g_app.radio_ok = query_radio();
    wcscpy_s(g_app.adapter_text, 64,
             g_app.radio_ok ? L"蓝牙适配器：可用" : L"蓝牙适配器：未就绪");
    refresh_status();

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    log_write(L"=== exit ===");
    ble_shutdown();
    RoUninitialize();
    destroy_fonts();
    if (g_app.br_bg) DeleteObject(g_app.br_bg);
    if (g_app.br_card) DeleteObject(g_app.br_card);
    return 0;
}

