/*
 * quicksend.c - F1~F7 快捷发送（文本可自定义，可开关，随 settings.ini 保存）
 */
#define WIN32_LEAN_AND_MEAN
#include "quicksend.h"
#include "settings.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define QS_COUNT 7
#define QS_TEXT_MAX 220

#define QS_CLASS L"BLEUartQuickSend"

#define IDC_QS_ENABLE 4001
#define IDC_QS_EDIT   4010   /* 4010 + i */
#define IDC_QS_OK     4020
#define IDC_QS_CANCEL 4021

static HWND g_main;
static HWND g_dlg;
static BOOL g_enabled;
static wchar_t g_text[QS_COUNT][QS_TEXT_MAX];
static void (*g_sender)(const wchar_t *text);

static const wchar_t *qs_key(int i)
{
    static const wchar_t *keys[QS_COUNT] = { L"f1", L"f2", L"f3", L"f4", L"f5", L"f6", L"f7" };
    return keys[i];
}

static void qs_load(void)
{
    int i;
    g_enabled = settings_get_int(L"quick", L"enabled", 0) ? TRUE : FALSE;
    for (i = 0; i < QS_COUNT; i++) {
        settings_get_str(L"quick", qs_key(i), L"", g_text[i], QS_TEXT_MAX);
    }
}

static void qs_save(void)
{
    int i;
    settings_put_int(L"quick", L"enabled", g_enabled ? 1 : 0);
    for (i = 0; i < QS_COUNT; i++) {
        settings_put_str(L"quick", qs_key(i), g_text[i]);
    }
}

static void qs_trim(wchar_t *s)
{
    wchar_t *p = s;
    size_t n;
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n') p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));
    n = wcslen(s);
    while (n > 0) {
        wchar_t c = s[n - 1];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') s[--n] = 0;
        else break;
    }
}

static void qs_apply_and_close(HWND dlg)
{
    int i;
    g_enabled = IsDlgButtonChecked(dlg, IDC_QS_ENABLE) == BST_CHECKED;
    for (i = 0; i < QS_COUNT; i++) {
        HWND ed = GetDlgItem(dlg, IDC_QS_EDIT + i);
        g_text[i][0] = 0;
        if (ed) GetWindowTextW(ed, g_text[i], QS_TEXT_MAX);
        qs_trim(g_text[i]);
    }
    qs_save();
    DestroyWindow(dlg);
}

static HWND qs_make(HWND parent, const wchar_t *cls, const wchar_t *text,
                    DWORD style, DWORD ex, int id)
{
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
                           0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           (HINSTANCE)(ULONG_PTR)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
}

static void qs_layout(HWND dlg)
{
    RECT rc;
    int i;
    int top = 48, row = 44;

    GetClientRect(dlg, &rc);
    MoveWindow(GetDlgItem(dlg, IDC_QS_ENABLE), 16, 14, 320, 24, TRUE);
    for (i = 0; i < QS_COUNT; i++) {
        wchar_t label[8];
        HWND lb = GetDlgItem(dlg, IDC_QS_EDIT + 100 + i);
        _snwprintf_s(label, 8, _TRUNCATE, L"F%d", i + 1);
        if (lb) {
            SetWindowTextW(lb, label);
            MoveWindow(lb, 16, top + i * row + 6, 36, 20, TRUE);
        }
        MoveWindow(GetDlgItem(dlg, IDC_QS_EDIT + i), 60, top + i * row,
                   rc.right - 60 - 16, 28, TRUE);
    }
    MoveWindow(GetDlgItem(dlg, IDC_QS_OK), rc.right - 16 - 96 - 8 - 96,
               rc.bottom - 16 - 34, 96, 34, TRUE);
    MoveWindow(GetDlgItem(dlg, IDC_QS_CANCEL), rc.right - 16 - 96,
               rc.bottom - 16 - 34, 96, 34, TRUE);
}

static LRESULT CALLBACK QsWndProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        {
            int i;
            qs_make(dlg, L"BUTTON", L"启用 F1~F7 快速发送",
                    BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_QS_ENABLE);
            for (i = 0; i < QS_COUNT; i++) {
                qs_make(dlg, L"STATIC", L"", SS_RIGHT, 0, IDC_QS_EDIT + 100 + i);
                qs_make(dlg, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP,
                        WS_EX_CLIENTEDGE, IDC_QS_EDIT + i);
            }
            qs_make(dlg, L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, IDC_QS_OK);
            qs_make(dlg, L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_QS_CANCEL);

            CheckDlgButton(dlg, IDC_QS_ENABLE, g_enabled ? BST_CHECKED : BST_UNCHECKED);
            for (i = 0; i < QS_COUNT; i++) {
                HWND ed = GetDlgItem(dlg, IDC_QS_EDIT + i);
                if (ed) {
                    SetWindowTextW(ed, g_text[i]);
                    SendMessageW(ed, EM_SETLIMITTEXT, QS_TEXT_MAX - 1, 0);
                    SendMessageW(ed, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
                }
            }
            qs_layout(dlg);
        }
        return 0;

    case WM_SIZE:
        qs_layout(dlg);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_QS_OK:
            qs_apply_and_close(dlg);
            return 0;
        case IDC_QS_CANCEL:
            DestroyWindow(dlg);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            DestroyWindow(dlg);
            return 0;
        }
        if (wp == VK_RETURN) {
            qs_apply_and_close(dlg);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(dlg);
        return 0;

    case WM_DESTROY:
        EnableWindow(g_main, TRUE);
        SetForegroundWindow(g_main);
        g_dlg = NULL;
        return 0;
    }
    return DefWindowProcW(dlg, msg, wp, lp);
}

void quicksend_init(HWND main, void (*sender)(const wchar_t *text))
{
    g_main = main;
    g_sender = sender;
    qs_load();
}

BOOL quicksend_enabled(void)
{
    return g_enabled;
}

BOOL quicksend_handle_key(int vk)
{
    HWND focus;
    HWND fg;
    BOOL ours = FALSE;
    int idx;

    if (!g_enabled || !g_sender || g_dlg) return FALSE;
    if (vk < VK_F1 || vk > VK_F7) return FALSE;
    if (GetKeyState(VK_CONTROL) & 0x8000) return FALSE;
    if (GetKeyState(VK_MENU) & 0x8000) return FALSE;

    focus = GetFocus();
    if (focus && GetAncestor(focus, GA_ROOT) == g_main) ours = TRUE;
    fg = GetForegroundWindow();
    if (!ours && fg && GetAncestor(fg, GA_ROOT) == g_main) ours = TRUE;
    if (!ours) return FALSE;

    idx = vk - VK_F1;
    if (!g_text[idx][0]) {
        MessageBeep(MB_ICONASTERISK);
        return TRUE;
    }
    g_sender(g_text[idx]);
    return TRUE;
}

void quicksend_show_dialog(void)
{
    static BOOL registered = FALSE;
    MSG msg;

    if (g_dlg) {
        SetForegroundWindow(g_dlg);
        return;
    }

    if (!registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = QsWndProc;
        wc.hInstance = (HINSTANCE)(ULONG_PTR)GetWindowLongPtrW(g_main, GWLP_HINSTANCE);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = QS_CLASS;
        if (!RegisterClassExW(&wc)) return;
        registered = TRUE;
    }

    EnableWindow(g_main, FALSE);
    g_dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, QS_CLASS, L"快速发送（F1~F7）",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                            CW_USEDEFAULT, CW_USEDEFAULT, 540, 480,
                            g_main, NULL,
                            (HINSTANCE)(ULONG_PTR)GetWindowLongPtrW(g_main, GWLP_HINSTANCE), NULL);
    if (!g_dlg) {
        EnableWindow(g_main, TRUE);
        return;
    }
    ShowWindow(g_dlg, SW_SHOW);
    UpdateWindow(g_dlg);

    while (g_dlg && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(g_dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}
