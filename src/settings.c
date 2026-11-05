/*
 * settings.c - 简单的 INI 配置读写（与运行日志放在同一目录）
 */
#define WIN32_LEAN_AND_MEAN
#include "settings.h"
#include "crashlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static wchar_t g_path[MAX_PATH];

const wchar_t *settings_ini_path(void)
{
    if (!g_path[0]) {
        const wchar_t *logp = log_file_path();
        wchar_t *slash;

        wcscpy_s(g_path, MAX_PATH, logp);
        slash = wcsrchr(g_path, L'\\');
        if (slash) {
            *slash = 0;
            wcscat_s(g_path, MAX_PATH, L"\\settings.ini");
        }
    }
    return g_path;
}

void settings_get_str(const wchar_t *section, const wchar_t *key,
                      const wchar_t *def, wchar_t *out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = 0;
    GetPrivateProfileStringW(section, key, def ? def : L"", out, (DWORD)cap,
                             settings_ini_path());
}

void settings_put_str(const wchar_t *section, const wchar_t *key, const wchar_t *value)
{
    WritePrivateProfileStringW(section, key, value ? value : L"", settings_ini_path());
}

int settings_get_int(const wchar_t *section, const wchar_t *key, int def)
{
    wchar_t buf[32];
    wchar_t *end;
    long v;

    settings_get_str(section, key, L"", buf, 32);
    if (!buf[0]) return def;
    v = wcstol(buf, &end, 10);
    if (end == buf) return def;
    return (int)v;
}

void settings_put_int(const wchar_t *section, const wchar_t *key, int value)
{
    wchar_t buf[32];
    _snwprintf_s(buf, 32, _TRUNCATE, L"%d", value);
    settings_put_str(section, key, buf);
}
