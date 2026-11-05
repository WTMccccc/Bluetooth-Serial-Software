/*
 * plot.h - 接收区曲线绘图
 */
#ifndef PLOT_H
#define PLOT_H

#include <windows.h>

void plot_create(HWND parent);
void plot_layout(const RECT *rc);
void plot_show(BOOL show);
BOOL plot_is_visible(void);
void plot_feed(const BYTE *data, int len);
void plot_clear(void);

#endif /* PLOT_H */
