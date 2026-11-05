/*
 * crashlog.h - 运行日志与崩溃日志
 *
 * 日志位置：%LOCALAPPDATA%\BLEUart\ble_uart.log
 * 崩溃时额外写：%LOCALAPPDATA%\BLEUart\crash_YYYYMMDD_HHMMSS.dmp
 */
#ifndef CRASHLOG_H
#define CRASHLOG_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_init(void);
void log_write(const wchar_t *fmt, ...);
const wchar_t *log_file_path(void);

/* 安装未处理异常过滤器（崩溃时写调用栈 + minidump） */
void log_install_crash_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* CRASHLOG_H */
