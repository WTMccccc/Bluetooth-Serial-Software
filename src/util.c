#include "util.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* UTF-8                                                                */
/* ------------------------------------------------------------------ */

/* 判断从 src[i] 开始的一个 UTF-8 序列是否完整且合法 */
static int u8_seq_valid(const uint8_t *src, int len, int i, int *seq_len)
{
    uint8_t b = src[i];
    int n;

    if (b < 0x80) { *seq_len = 1; return 1; }
    if ((b & 0xE0) == 0xC0) { n = 2; if (b < 0xC2) return 0; }
    else if ((b & 0xF0) == 0xE0) { n = 3; }
    else if ((b & 0xF8) == 0xF0) { n = 4; if (b > 0xF4) return 0; }
    else return 0;

    if (i + n > len) { *seq_len = n; return 0; }   /* 不完整 */
    for (int k = 1; k < n; k++) {
        if ((src[i + k] & 0xC0) != 0x80) return 0;
    }
    *seq_len = n;
    return 1;
}

int u8_to_w(const uint8_t *src, int len, wchar_t *dst, int cap)
{
    int out = 0;
    int i = 0;

    while (i < len) {
        int n = 0;
        if (u8_seq_valid(src, len, i, &n)) {
            wchar_t tmp[4];
            int got = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          (const char *)src + i, n, tmp, 4);
            if (got > 0) {
                for (int k = 0; k < got; k++) {
                    if (dst && out < cap - 1) dst[out] = tmp[k];
                    out++;
                }
                i += n;
                continue;
            }
        }
        /* 非法字节：替换为 U+FFFD 并前进 1 字节 */
        if (dst && out < cap - 1) dst[out] = 0xFFFD;
        out++;
        i++;
    }
    if (dst && cap > 0) dst[out < cap ? out : cap - 1] = 0;
    return out;
}

int w_to_u8(const wchar_t *src, int len, char *dst, int cap)
{
    if (len < 0) len = (int)wcslen(src);
    if (len == 0) { if (dst && cap > 0) dst[0] = 0; return 0; }
    int need = WideCharToMultiByte(CP_UTF8, 0, src, len, NULL, 0, NULL, NULL);
    if (need <= 0) { if (dst && cap > 0) dst[0] = 0; return 0; }
    if (!dst) return need;
    if (need >= cap) need = cap - 1;
    int got = WideCharToMultiByte(CP_UTF8, 0, src, len, dst, need, NULL, NULL);
    if (got < 0) got = 0;
    dst[got] = 0;
    return got;
}

int u8_tail_incomplete(const uint8_t *src, int len)
{
    int i;

    if (len <= 0) return 0;
    /* 最多回看 3 个字节找起始字节 */
    for (i = 1; i <= 3 && i <= len; i++) {
        uint8_t b = src[len - i];
        int need;
        if ((b & 0xC0) == 0x80) continue;   /* 续字节，继续往前找 */
        if (b < 0x80) return 0;
        if ((b & 0xE0) == 0xC0) need = 2;
        else if ((b & 0xF0) == 0xE0) need = 3;
        else if ((b & 0xF8) == 0xF0) need = 4;
        else return 0;
        return (i < need) ? i : 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* HEX                                                                  */
/* ------------------------------------------------------------------ */
int hex_format(const uint8_t *src, int len, wchar_t *dst, int cap)
{
    int out = 0;
    for (int i = 0; i < len; i++) {
        if (out + 4 >= cap) break;
        if (i > 0) dst[out++] = L' ';
        static const wchar_t digits[] = L"0123456789ABCDEF";
        dst[out++] = digits[src[i] >> 4];
        dst[out++] = digits[src[i] & 0x0F];
    }
    if (cap > 0) dst[out < cap ? out : cap - 1] = 0;
    return out;
}

static int hex_val(wchar_t c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

int hex_parse(const wchar_t *src, uint8_t *dst, int cap)
{
    int out = 0;
    int hi = -1;

    for (const wchar_t *p = src; *p; p++) {
        wchar_t c = *p;
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L',' ||
            c == L':' || c == L'-') {
            continue;
        }
        if (c == L'0' && (p[1] == L'x' || p[1] == L'X')) { p++; continue; }
        int v = hex_val(c);
        if (v < 0) return -1;
        if (hi < 0) {
            hi = v;
        } else {
            if (out >= cap) return -1;
            dst[out++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    if (hi >= 0) return -1;   /* 单个半字节 */
    return out;
}

/* ------------------------------------------------------------------ */
/* 地址 / UUID / 时间                                                   */
/* ------------------------------------------------------------------ */
void bt_addr_format(uint64_t address, wchar_t *dst, int cap)
{
    swprintf(dst, cap, L"%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)((address >> 40) & 0xFF), (unsigned)((address >> 32) & 0xFF),
             (unsigned)((address >> 24) & 0xFF), (unsigned)((address >> 16) & 0xFF),
             (unsigned)((address >> 8) & 0xFF), (unsigned)(address & 0xFF));
}

void guid_format(const GUID *guid, wchar_t *dst, int cap)
{
    swprintf(dst, cap, L"%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             (unsigned long)guid->Data1, guid->Data2, guid->Data3,
             guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
             guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
}

void time_now_text(wchar_t *dst, int cap)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    swprintf(dst, cap, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
}
