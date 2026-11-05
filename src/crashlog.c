/*
 * crashlog.c - 运行日志 + 未处理异常日志
 */
#define WIN32_LEAN_AND_MEAN
#include "crashlog.h"

#include <dbghelp.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "shell32.lib")

static CRITICAL_SECTION g_log_lock;
static BOOL g_log_lock_ready;
static wchar_t g_dir[MAX_PATH];
static wchar_t g_file[MAX_PATH];
static HANDLE g_handle = INVALID_HANDLE_VALUE;

/* 依次尝试：%LOCALAPPDATA%\BLEUart、exe 所在目录、%TEMP%\BLEUart */
static int build_candidate(int index, wchar_t *dir, wchar_t *file)
{
    wchar_t base[MAX_PATH] = L"";

    if (index == 0) {
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) == 0) return 0;
        _snwprintf_s(dir, MAX_PATH, _TRUNCATE, L"%s\\BLEUart", base);
    } else if (index == 1) {
        if (GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH) == 0) return 0;
        _snwprintf_s(dir, MAX_PATH, _TRUNCATE, L"%s\\BLEUart", base);
    } else if (index == 2) {
        if (GetEnvironmentVariableW(L"USERPROFILE", base, MAX_PATH) == 0) return 0;
        _snwprintf_s(dir, MAX_PATH, _TRUNCATE, L"%s\\BLEUart", base);
    } else if (index == 3) {
        if (GetModuleFileNameW(NULL, base, MAX_PATH) == 0) return 0;
        {
            wchar_t *slash = wcsrchr(base, L'\\');
            if (!slash) return 0;
            *slash = 0;
        }
        _snwprintf_s(dir, MAX_PATH, _TRUNCATE, L"%s\\logs", base);
    } else {
        if (GetTempPathW(MAX_PATH, base) == 0) return 0;
        _snwprintf_s(dir, MAX_PATH, _TRUNCATE, L"%sBLEUart", base);
    }
    _snwprintf_s(file, MAX_PATH, _TRUNCATE, L"%s\\ble_uart.log", dir);
    return 1;
}

static void open_log_locked(void)
{
    LARGE_INTEGER size;
    int i;
    int first_fail = 0;
    DWORD first_err = 0;
    wchar_t first_dir[MAX_PATH] = L"";

    if (g_handle != INVALID_HANDLE_VALUE) return;

    for (i = 0; i < 5; i++) {
        wchar_t dir[MAX_PATH] = L"";
        wchar_t file[MAX_PATH] = L"";
        if (!build_candidate(i, dir, file)) continue;
        CreateDirectoryW(dir, NULL);
        g_handle = CreateFileW(file, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_handle == INVALID_HANDLE_VALUE) {
            if (i == 0) {
                first_fail = 1;
                first_err = GetLastError();
                wcscpy_s(first_dir, MAX_PATH, dir);
            }
            continue;
        }
        wcscpy_s(g_dir, MAX_PATH, dir);
        wcscpy_s(g_file, MAX_PATH, file);
        break;
    }
    if (g_handle == INVALID_HANDLE_VALUE) return;

    if (first_fail) {
        log_write(L"[log] 无法写入 %s（错误 %lu），日志已回退到 %s",
                  first_dir[0] ? first_dir : L"默认目录", (unsigned long)first_err, g_file);
    }

    /* 超过 2 MB 就清空重来，避免日志无限增长 */
    if (GetFileSizeEx(g_handle, &size) && size.QuadPart > 2 * 1024 * 1024) {
        CloseHandle(g_handle);
        g_handle = INVALID_HANDLE_VALUE;
        DeleteFileW(g_file);
        g_handle = CreateFileW(g_file, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
}

static void ensure_dir(void)
{
    if (!g_file[0]) {
        wchar_t dir[MAX_PATH] = L"";
        wchar_t file[MAX_PATH] = L"";
        if (build_candidate(0, dir, file)) {
            wcscpy_s(g_dir, MAX_PATH, dir);
            wcscpy_s(g_file, MAX_PATH, file);
        }
    }
}

static void write_raw(const char *utf8, int len)
{
    DWORD written = 0;
    if (g_handle == INVALID_HANDLE_VALUE) return;
    WriteFile(g_handle, utf8, (DWORD)len, &written, NULL);
}

void log_init(void)
{
    if (!g_log_lock_ready) {
        InitializeCriticalSection(&g_log_lock);
        g_log_lock_ready = TRUE;
    }
    EnterCriticalSection(&g_log_lock);
    open_log_locked();
    LeaveCriticalSection(&g_log_lock);
}

void log_write(const wchar_t *fmt, ...)
{
    wchar_t line[1024];
    char utf8[4096];
    SYSTEMTIME st;
    int pos = 0;
    int len;
    va_list ap;

    if (!g_log_lock_ready) return;

    GetLocalTime(&st);
    pos = _snwprintf_s(line, _countof(line), _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] ",
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                       st.wMilliseconds, (unsigned long)GetCurrentThreadId());
    if (pos < 0) pos = 0;

    va_start(ap, fmt);
    _vsnwprintf_s(line + pos, _countof(line) - (size_t)pos, _TRUNCATE, fmt, ap);
    va_end(ap);

    len = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, (int)sizeof(utf8) - 2, NULL, NULL);
    if (len <= 1) return;
    len -= 1;                 /* 去掉结尾 0 */
    utf8[len++] = '\r';
    utf8[len++] = '\n';

    EnterCriticalSection(&g_log_lock);
    open_log_locked();
    write_raw(utf8, len);
    LeaveCriticalSection(&g_log_lock);
}

const wchar_t *log_file_path(void)
{
    ensure_dir();
    return g_file;
}

/* ------------------------------------------------------------------ */
/* 崩溃处理                                                            */
/* ------------------------------------------------------------------ */
static void append_module_line(wchar_t *buf, size_t cap, const wchar_t *tag, DWORD64 addr)
{
    HMODULE mod = NULL;
    wchar_t path[MAX_PATH] = L"";
    DWORD64 base = 0;

    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(ULONG_PTR)addr, &mod) && mod) {
        GetModuleFileNameW(mod, path, MAX_PATH);
        base = (DWORD64)(ULONG_PTR)mod;
    }
    _snwprintf_s(buf, cap, _TRUNCATE, L"%s %p  %s+0x%llX", tag, (void *)(ULONG_PTR)addr,
                 path[0] ? path : L"(unknown)", (unsigned long long)(addr - base));
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    wchar_t report[8192];
    wchar_t line[MAX_PATH + 128];
    wchar_t dump[MAX_PATH];
    size_t used = 0;
    void *frames[40];
    USHORT frame_count;
    SYSTEMTIME st;
    const EXCEPTION_RECORD *er = ep ? ep->ExceptionRecord : NULL;

    GetLocalTime(&st);

    used += (size_t)_snwprintf_s(report + used, _countof(report) - used, _TRUNCATE,
                                 L"=== CRASH %04u-%02u-%02u %02u:%02u:%02u ===\r\n",
                                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    if (er) {
        used += (size_t)_snwprintf_s(report + used, _countof(report) - used, _TRUNCATE,
                                     L"code=0x%08lX flags=0x%08lX address=%p\r\n",
                                     (unsigned long)er->ExceptionCode,
                                     (unsigned long)er->ExceptionFlags,
                                     (void *)er->ExceptionAddress);
        append_module_line(line, _countof(line), L"fault:", (DWORD64)(ULONG_PTR)er->ExceptionAddress);
        used += (size_t)_snwprintf_s(report + used, _countof(report) - used, _TRUNCATE, L"%s\r\n", line);
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
            used += (size_t)_snwprintf_s(report + used, _countof(report) - used, _TRUNCATE,
                                         L"access=%s at %p\r\n",
                                         er->ExceptionInformation[0] ? L"write" : L"read",
                                         (void *)er->ExceptionInformation[1]);
        }
    } else {
        used += (size_t)_snwprintf_s(report + used, _countof(report) - used, _TRUNCATE,
                                     L"no exception record\r\n");
    }

    frame_count = RtlCaptureStackBackTrace(0, (DWORD)_countof(frames), frames, NULL);
    used += (size_t)_snwprintf_s(report + used, _countof(report) - used, _TRUNCATE,
                                 L"stack (%u frames):\r\n", frame_count);
    for (USHORT i = 0; i < frame_count && used < _countof(report) - 300; i++) {
        append_module_line(line, _countof(line), L"  #", (DWORD64)(ULONG_PTR)frames[i]);
        used += (size_t)_snwprintf_s(report + used, _countof(report) - used, _TRUNCATE, L"%s\r\n", line);
    }

    /* minidump */
    dump[0] = 0;
    if (g_dir[0]) {
        _snwprintf_s(dump, MAX_PATH, _TRUNCATE, L"%s\\crash_%04u%02u%02u_%02u%02u%02u.dmp",
                     g_dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        HANDLE f = CreateFileW(dump, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                                   (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo),
                                   ep ? &mei : NULL, NULL, NULL)) {
                dump[0] = 0;
            }
            CloseHandle(f);
        } else {
            dump[0] = 0;
        }
    }
    if (dump[0]) {
        used += (size_t)_snwprintf_s(report + used, _countof(report) - used, _TRUNCATE,
                                     L"minidump: %s\r\n", dump);
    }

    /* 写日志（直接用 WriteFile，避免崩溃时锁状态不确定） */
    if (g_file[0]) {
        HANDLE f = CreateFileW(g_file, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            char utf8[16384];
            int len = WideCharToMultiByte(CP_UTF8, 0, report, -1, utf8, (int)sizeof(utf8) - 2, NULL, NULL);
            if (len > 1) {
                DWORD written = 0;
                utf8[len - 1] = '\r';
                utf8[len++] = '\n';
                WriteFile(f, utf8, (DWORD)len, &written, NULL);
            }
            CloseHandle(f);
        }
    }

    /* 提示用户日志位置 */
    {
        wchar_t msg[2048];
        _snwprintf_s(msg, _countof(msg), _TRUNCATE,
                     L"程序遇到异常并需要关闭。\n\n崩溃日志：\n%s\n\n"
                     L"请把该日志（以及同名 .dmp）发给开发者。",
                     g_file[0] ? g_file : L"(无法写入日志)");
        MessageBoxW(NULL, msg, L"BLE 串口助手 - 崩溃", MB_ICONERROR | MB_OK);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

void log_install_crash_handler(void)
{
    ensure_dir();
    SetUnhandledExceptionFilter(crash_filter);
}
