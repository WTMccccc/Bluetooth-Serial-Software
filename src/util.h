/*
 * util.h - UTF-8 / HEX / 时间格式化等小工具
 */
#ifndef UTIL_H
#define UTIL_H

#include <windows.h>
#include <stdint.h>

/* UTF-8 -> UTF-16（宽松模式：非法字节替换为 U+FFFD）。
 * 返回写入 dst 的字符数（不含结尾 0）。dst 为 NULL 时只计算长度。 */
int u8_to_w(const uint8_t *src, int len, wchar_t *dst, int cap);

/* UTF-16 -> UTF-8，返回字节数（不含结尾 0）。dst 为 NULL 时只计算长度。 */
int w_to_u8(const wchar_t *src, int len, char *dst, int cap);

/* 返回缓冲区末尾“不完整的 UTF-8 序列”字节数（0~3）。 */
int u8_tail_incomplete(const uint8_t *src, int len);

/* 把字节格式化成十六进制文本，例如 "01 1B FF"。返回字符数。 */
int hex_format(const uint8_t *src, int len, wchar_t *dst, int cap);

/* 解析十六进制文本（允许空格、逗号、换行、0x 前缀）。
 * 成功返回字节数；失败返回 -1。 */
int hex_parse(const wchar_t *src, uint8_t *dst, int cap);

/* 把 6 字节地址格式化成 "AA:BB:CC:DD:EE:FF"（64 位蓝牙地址取低 48 位）。 */
void bt_addr_format(uint64_t address, wchar_t *dst, int cap);

/* 把 GUID 格式化成 "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"。 */
void guid_format(const GUID *guid, wchar_t *dst, int cap);

/* 当前时间 "HH:MM:SS"。 */
void time_now_text(wchar_t *dst, int cap);

#endif /* UTIL_H */
