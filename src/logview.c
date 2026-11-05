/*
 * logview.c - 应用内日志窗口（不依赖系统记事本/设置应用）
 */
#define WIN32_LEAN_AND_MEAN
#include "logview.h"
#include "crashlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <shellapi.h>

#define LOGVIEW_CLASS L"BLEUartLogView"

#define IDC_LOG_EDIT   2001
#define IDC_LOG_REFRESH 2002
#define IDC_LOG_FOLDER 2003
#define IDC_LOG_CLOSE  2004
#define IDC_LOG_PATH   2005

static HWND g_wnd;
static HWND g_edit;
static HWND g_path;
static HFONT g_font;

static void load_log(void)
{
    const wchar_t *path = log_file_path();
    HANDLE h;
    DWORD size, read = 0;
    char *buf;
    wchar_t *wbuf;
    int wlen;

    if (!g_edit) return;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        SetWindowTextW(g_edit, L"(日志文件还不存在)");
        return;
    }
    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE) size = 0;
    if (size > 1024 * 1024) {
        LARGE_INTEGER off;
        off.QuadPart = (LONGLONG)size - 1024 * 1024;
        SetFilePointerEx(h, off, NULL, FILE_BEGIN);
        size = 1024 * 1024;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        CloseHandle(h);
        return;
    }
    ReadFile(h, buf, size, &read, NULL);
    buf[read] = 0;
    CloseHandle(h);

    wlen = MultiByteToWideChar(CP_UTF8, 0, buf, (int)read, NULL, 0);
    wbuf = (wchar_t *)malloc(((size_t)wlen + 1) * sizeof(wchar_t));
    if (wbuf) {
        MultiByteToWideChar(CP_UTF8, 0, buf, (int)read, wbuf, wlen);
        wbuf[wlen] = 0;
        SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
        SetWindowTextW(g_edit, wbuf);
        SendMessageW(g_edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
        SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_edit, NULL, TRUE);
        free(wbuf);
    }
    free(buf);
    SetWindowTextW(g_path, path);
}

static void layout(HWND wnd)
{
    RECT rc;
    int margin = 12, btn_h = 30, btn_w = 88, gap = 8;
    int w, h;

    GetClientRect(wnd, &rc);
    w = rc.right;
    h = rc.bottom;

    MoveWindow(g_edit, margin, margin, w - margin * 2, h - margin * 2 - btn_h - 26, TRUE);
    MoveWindow(g_path, margin, h - margin - btn_h, w - margin * 2 - (btn_w + gap) * 3 - gap,
               22, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_LOG_REFRESH), w - margin - btn_w * 3 - gap * 2,
               h - margin - btn_h + 4, btn_w, btn_h, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_LOG_FOLDER), w - margin - btn_w * 2 - gap,
               h - margin - btn_h + 4, btn_w, btn_h, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_LOG_CLOSE), w - margin - btn_w,
               h - margin - btn_h + 4, btn_w, btn_h, TRUE);
}

static HWND make_ctl(HWND parent, const wchar_t *cls, const wchar_t *text,
                     DWORD style, DWORD exstyle, int id)
{
    return CreateWindowExW(exstyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                           0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           (HINSTANCE)(ULONG_PTR)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
}

static LRESULT CALLBACK LogWndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_edit = make_ctl(wnd, L"EDIT", L"",
                          ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL |
                          WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_LOG_EDIT);
        g_path = make_ctl(wnd, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, IDC_LOG_PATH);
        make_ctl(wnd, L"BUTTON", L"刷新", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_LOG_REFRESH);
        make_ctl(wnd, L"BUTTON", L"打开文件夹", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_LOG_FOLDER);
        make_ctl(wnd, L"BUTTON", L"关闭", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_LOG_CLOSE);

        g_font = CreateFontW(-MulDiv(10, (int)GetDeviceCaps(GetDC(wnd), LOGPIXELSY), 72),
                             0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (g_edit) SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, TRUE);
        if (g_path) SendMessageW(g_path, WM_SETFONT,
                                 (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        {
            HWND b;
            b = GetDlgItem(wnd, IDC_LOG_REFRESH);
            if (b) SendMessageW(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            b = GetDlgItem(wnd, IDC_LOG_FOLDER);
            if (b) SendMessageW(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            b = GetDlgItem(wnd, IDC_LOG_CLOSE);
            if (b) SendMessageW(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        }
        layout(wnd);
        load_log();
        return 0;

    case WM_SIZE:
        layout(wnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_LOG_REFRESH:
            load_log();
            return 0;
        case IDC_LOG_FOLDER:
            {
                const wchar_t *path = log_file_path();
                wchar_t folder[MAX_PATH];
                wchar_t *slash;
                HINSTANCE r;

                wcscpy_s(folder, MAX_PATH, path);
                slash = wcsrchr(folder, L'\\');
                if (slash) *slash = 0;

                /* 直接打开目录，避免 explorer /select 的引号解析问题 */
                r = ShellExecuteW(NULL, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
                if ((INT_PTR)r <= 32) {
                    MessageBoxW(wnd, path, L"日志位置", MB_ICONINFORMATION);
                }
            }
            return 0;
        case IDC_LOG_CLOSE:
            DestroyWindow(wnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(wnd);
        return 0;

    case WM_DESTROY:
        g_wnd = NULL;
        g_edit = NULL;
        g_path = NULL;
        if (g_font) { DeleteObject(g_font); g_font = NULL; }
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void logview_show(HWND owner)
{
    static BOOL registered = FALSE;

    if (g_wnd) {
        load_log();
        ShowWindow(g_wnd, SW_SHOW);
        SetForegroundWindow(g_wnd);
        return;
    }
    if (!registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = LogWndProc;
        wc.hInstance = (HINSTANCE)(ULONG_PTR)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = LOGVIEW_CLASS;
        if (!RegisterClassExW(&wc)) return;
        registered = TRUE;
    }
    g_wnd = CreateWindowExW(0, LOGVIEW_CLASS, L"运行日志",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
                            owner, NULL,
                            (HINSTANCE)(ULONG_PTR)GetWindowLongPtrW(owner, GWLP_HINSTANCE), NULL);
    if (!g_wnd) return;
    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);
}
