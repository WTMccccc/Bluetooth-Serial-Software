/*
 * settings.h - 程序设置（INI，与日志同目录）
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <windows.h>

const wchar_t *settings_ini_path(void);

void settings_get_str(const wchar_t *section, const wchar_t *key,
                      const wchar_t *def, wchar_t *out, int cap);
void settings_put_str(const wchar_t *section, const wchar_t *key, const wchar_t *value);

int  settings_get_int(const wchar_t *section, const wchar_t *key, int def);
void settings_put_int(const wchar_t *section, const wchar_t *key, int value);

#endif /* SETTINGS_H */
