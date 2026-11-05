/*
 * quicksend.h - F1~F7 快捷发送
 */
#ifndef QUICKSEND_H
#define QUICKSEND_H

#include <windows.h>

/* 初始化并绑定发送回调；sender 由主程序提供 */
void quicksend_init(HWND main, void (*sender)(const wchar_t *text));

/* 处理 F1~F7；返回 TRUE 表示已消费该按键 */
BOOL quicksend_handle_key(int vk);

/* 打开“快速发送”设置窗口（模态） */
void quicksend_show_dialog(void);

BOOL quicksend_enabled(void);

#endif /* QUICKSEND_H */
