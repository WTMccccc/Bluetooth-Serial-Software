/*
 * ble.c - BLE 串口透传引擎（纯 C 调用 Windows WinRT API）
 *
 * 实现要点见 winrt_abi.h：
 *   - WinRT 没有给 C 提供 BLE 头文件，本文件按 SDK 中 C 版 vtable 的布局调用。
 *   - 事件回调对象是“IUnknown + Invoke”的委托（不是 IInspectable 派生）。
 */
#include "ble.h"
#include "util.h"
#include "winrt_abi.h"
#include "crashlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <intrin.h>

/* ------------------------------------------------------------------ */
/* IID / UUID                                                          */
/* ------------------------------------------------------------------ */
static const GUID kIID_Watcher =
    {0xA6AC336F, 0xF3D3, 0x4297, {0x8D, 0x6C, 0xC8, 0x1E, 0xA6, 0x62, 0x3F, 0x40}};
static const GUID kIID_AdvArgs2 =
    {0x12D9C87B, 0x0399, 0x5F0E, {0xA3, 0x48, 0x53, 0xB0, 0x2B, 0x6B, 0x16, 0x2E}};
static const GUID kIID_DeviceStatics =
    {0xC8CF1A19, 0xF0B6, 0x4BF0, {0x86, 0x89, 0x41, 0x30, 0x3D, 0xE2, 0xD9, 0xF4}};
static const GUID kIID_DeviceStatics2 =
    {0x5F12C06B, 0x3BAC, 0x43E8, {0xAD, 0x16, 0x56, 0x32, 0x71, 0xBD, 0x41, 0xC2}};
static const GUID kIID_Device3 =
    {0xAEE9E493, 0x44AC, 0x40DC, {0xAF, 0x33, 0xB2, 0xC1, 0x3C, 0x01, 0xCA, 0x46}};
static const GUID kIID_Service3 =
    {0xB293A950, 0x0C53, 0x437C, {0xA9, 0xB3, 0x5C, 0x32, 0x10, 0xC6, 0xE5, 0x69}};
static const GUID kIID_AsyncInfo =
    {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kIID_BufferByteAccess =
    {0x905A0FEF, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
static const GUID kIID_Buffer =
    {0x905A0FE0, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
static const GUID kIID_Closable =
    {0x30D5A829, 0x7FA4, 0x4026, {0x83, 0xBB, 0xD7, 0x5B, 0xAE, 0x4E, 0xA9, 0x9E}};
static const GUID kIID_AgileObject =
    {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
static const GUID kIID_Unknown =
    {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

/* Nordic UART Service（ESP32 常见） */
static const GUID kUUID_NusService =
    {0x6E400001, 0xB5A3, 0xF393, {0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E}};
static const GUID kUUID_NusRx =
    {0x6E400002, 0xB5A3, 0xF393, {0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E}};
static const GUID kUUID_NusTx =
    {0x6E400003, 0xB5A3, 0xF393, {0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E}};
/* HM-10 / AT-09 / JDY-08 等透传模块 */
static const GUID kUUID_Ffe0 =
    {0x0000FFE0, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};
static const GUID kUUID_Ffe1 =
    {0x0000FFE1, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

/* ------------------------------------------------------------------ */
/* 全局状态                                                            */
/* ------------------------------------------------------------------ */
static BleCallbacks g_cb;
static CRITICAL_SECTION g_lock;        /* 连接对象 + 状态 + 信息 */
static CRITICAL_SECTION g_scan_lock;   /* 设备表 */
static CRITICAL_SECTION g_tx_lock;     /* 发送队列 */
static CRITICAL_SECTION g_rx_lock;     /* 接收环形缓冲区 */

static BleStatus g_status;
static volatile LONG64 g_rx_total;
static volatile LONG64 g_tx_total;
static volatile LONG64 g_rx_dropped;

/* 扫描 */
static BleDevice g_devices[BLE_MAX_DEVICES];
static int g_device_count;
static BOOL g_scan_active;
static IBluetoothLEAdvertisementWatcher *g_watcher;
static EventRegistrationToken_ g_scan_token;
static BOOL g_scan_token_valid;
static void *g_scan_delegate;

/* 连接对象 */
static IBluetoothLEDevice  *g_dev;
static IBluetoothLEDevice3 *g_dev3;
static IGattDeviceService  *g_svc;
static IGattCharacteristic *g_notify_chr;
static IGattCharacteristic *g_write_chr;
static IGattSession        *g_session;
static EventRegistrationToken_ g_notify_token;
static BOOL g_notify_token_valid;
static int  g_write_option;            /* GATT_WRITE_* */
static int  g_write_props;             /* 写特征的属性位 */
static volatile LONG g_connect_cancel;
static volatile LONG g_connect_gen;   /* 每次连接/断开递增，防止旧连接线程回写状态 */
static HANDLE g_connect_thread;
static int g_disc_seen;                /* 连续检测到掉线的次数 */
static volatile LONG g_rx_enabled;     /* 接收回调闸门：断开/异常时关闭 */
static volatile LONG g_rx_fault;       /* 接收回调异常，由 UI 线程做清理 */
static volatile LONG g_rx_packets;     /* 收到的通知包数（诊断用） */

/* 发送 */
typedef struct TxChunk {
    struct TxChunk *next;
    int len;
    uint8_t data[1];
} TxChunk;
static TxChunk *g_tx_head;
static TxChunk *g_tx_tail;
static HANDLE g_tx_event;
static HANDLE g_tx_thread;
static volatile LONG g_tx_stop;
static int g_tx_interval;
static volatile LONG g_tx_queued;      /* 已入队但未发送的包数，用于限流 */

/* 接收 */
static uint8_t g_rx_ring[BLE_RX_RING];
static int g_rx_head;
static int g_rx_tail;
static int g_rx_count;

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */
static void notify_status(void)
{
    if (g_cb.on_status_changed) g_cb.on_status_changed();
}

static void ble_log(const wchar_t *fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    if (!g_cb.on_log) return;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_cb.on_log(buf);
}

static HSTRING make_hstring(const wchar_t *s)
{
    HSTRING h = NULL;
    if (FAILED(WindowsCreateString(s, (UINT32)wcslen(s), &h))) return NULL;
    return h;
}

static void hstring_to_buf(HSTRING h, wchar_t *dst, int cap)
{
    UINT32 len = 0;
    const wchar_t *s;
    if (!dst || cap <= 0) return;
    dst[0] = 0;
    if (!h) return;
    s = WindowsGetStringRawBuffer(h, &len);
    if (!s) return;
    if ((int)len > cap - 1) len = (UINT32)(cap - 1);
    memcpy(dst, s, len * sizeof(wchar_t));
    dst[len] = 0;
}

static void set_state(BleState st, const wchar_t *fmt, ...)
{
    va_list ap;
    wchar_t msg[BLE_MSG_MAX] = L"";
    EnterCriticalSection(&g_lock);
    g_status.state = st;
    if (fmt) {
        va_start(ap, fmt);
        _vsnwprintf_s(g_status.message, BLE_MSG_MAX, _TRUNCATE, fmt, ap);
        va_end(ap);
        wcscpy_s(msg, BLE_MSG_MAX, g_status.message);
    }
    LeaveCriticalSection(&g_lock);
    if (msg[0]) ble_log(L"%s", msg);
    notify_status();
}

/* 把常见 ATT 错误翻译成可操作的中文提示 */
static const wchar_t *att_error_hint(HRESULT hr)
{
    switch ((unsigned long)hr) {
    case 0x80650005: return L"（特征要求配对/认证，请先在系统蓝牙设置中配对设备）";
    case 0x8065000C: return L"（链路未加密，请先配对设备）";
    case 0x8065000F: return L"（权限不足，设备拒绝了本次操作）";
    case 0x80650008: return L"（设备忙，请稍后重试）";
    default: return L"";
    }
}

/* ------------------------------------------------------------------ */
/* 异步操作等待（轮询 IAsyncInfo::Status，避免再实现一套 Completed 委托） */
/* ------------------------------------------------------------------ */
static HRESULT async_wait(IAsyncOperation *op, DWORD timeout_ms, void *result)
{
    IAsyncInfo *info = NULL;
    HRESULT hr;
    int status = ASYNC_STATUS_STARTED;
    DWORD start;

    if (!op) return E_POINTER;
    hr = ((IInspectable *)op)->lpVtbl->QueryInterface((IInspectable *)op, &kIID_AsyncInfo, (void **)&info);
    if (FAILED(hr) || !info) return FAILED(hr) ? hr : E_NOINTERFACE;

    start = GetTickCount();
    for (;;) {
        hr = info->lpVtbl->get_Status(info, &status);
        if (FAILED(hr)) break;
        if (status != ASYNC_STATUS_STARTED) break;
        if (GetTickCount() - start > timeout_ms) {
            info->lpVtbl->Cancel(info);
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            break;
        }
        Sleep(10);
    }

    if (SUCCEEDED(hr)) {
        if (status == ASYNC_STATUS_COMPLETED) {
            hr = op->lpVtbl->GetResults(op, result);
        } else if (status == ASYNC_STATUS_CANCELED) {
            hr = E_ABORT;
        } else if (status == ASYNC_STATUS_ERROR) {
            HRESULT err = E_FAIL;
            info->lpVtbl->get_ErrorCode(info, &err);
            hr = FAILED(err) ? err : E_FAIL;
        } else {
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
    }
    info->lpVtbl->Release(info);
    return hr;
}

static const GUID kIID_DataReaderStatics =
    {0x11FCBFC8, 0xF93A, 0x471B, {0xB1, 0x21, 0xF3, 0x79, 0xE3, 0x49, 0x31, 0x3C}};
static const GUID kIID_DataWriter =
    {0x64B89265, 0xD341, 0x4922, {0xB3, 0x8A, 0xDD, 0x4A, 0xF8, 0x80, 0x8C, 0x4E}};

/* ------------------------------------------------------------------ */
/* IBuffer 的 C 实现（写特征值时需要把字节交给 WinRT）                    */
/* ------------------------------------------------------------------ */
typedef struct WinrtBuffer {
    IBuffer iface;
    IBufferByteAccess byte_access;
    LONG ref;
    UINT32 len;
    BYTE *data;
} WinrtBuffer;

static HRESULT STDMETHODCALLTYPE buf_QI(IBuffer *This, REFIID riid, void **ppv);
static ULONG   STDMETHODCALLTYPE buf_AddRef(IBuffer *This);
static ULONG   STDMETHODCALLTYPE buf_Release(IBuffer *This);
static HRESULT STDMETHODCALLTYPE buf_GetIids(IBuffer *This, ULONG *c, IID **i);
static HRESULT STDMETHODCALLTYPE buf_GetClassName(IBuffer *This, HSTRING *n);
static HRESULT STDMETHODCALLTYPE buf_GetTrust(IBuffer *This, TrustLevel_ *l);
static HRESULT STDMETHODCALLTYPE buf_get_Capacity(IBuffer *This, UINT32 *v);
static HRESULT STDMETHODCALLTYPE buf_get_Length(IBuffer *This, UINT32 *v);
static HRESULT STDMETHODCALLTYPE buf_put_Length(IBuffer *This, UINT32 v);

static const IBufferVtbl buffer_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(IBuffer *, REFIID, void **))buf_QI,
    (ULONG (STDMETHODCALLTYPE *)(IBuffer *))buf_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(IBuffer *))buf_Release,
    (HRESULT (STDMETHODCALLTYPE *)(IBuffer *, ULONG *, IID **))buf_GetIids,
    (HRESULT (STDMETHODCALLTYPE *)(IBuffer *, HSTRING *))buf_GetClassName,
    (HRESULT (STDMETHODCALLTYPE *)(IBuffer *, TrustLevel_ *))buf_GetTrust,
    buf_get_Capacity, buf_get_Length, buf_put_Length
};

static HRESULT STDMETHODCALLTYPE ba_QI(IBufferByteAccess *This, REFIID riid, void **ppv)
{
    WinrtBuffer *self = (WinrtBuffer *)((BYTE *)This - offsetof(WinrtBuffer, byte_access));
    return buf_QI(&self->iface, riid, ppv);
}
static ULONG STDMETHODCALLTYPE ba_AddRef(IBufferByteAccess *This)
{
    WinrtBuffer *self = (WinrtBuffer *)((BYTE *)This - offsetof(WinrtBuffer, byte_access));
    return buf_AddRef(&self->iface);
}
static ULONG STDMETHODCALLTYPE ba_Release(IBufferByteAccess *This)
{
    WinrtBuffer *self = (WinrtBuffer *)((BYTE *)This - offsetof(WinrtBuffer, byte_access));
    return buf_Release(&self->iface);
}
static HRESULT STDMETHODCALLTYPE ba_GetIids(IBufferByteAccess *This, ULONG *c, IID **i)
{
    (void)This; (void)c; (void)i; return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE ba_GetClassName(IBufferByteAccess *This, HSTRING *n)
{
    (void)This; (void)n; return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE ba_GetTrust(IBufferByteAccess *This, TrustLevel_ *l)
{
    (void)This; (void)l; return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE ba_Buffer(IBufferByteAccess *This, BYTE **value)
{
    WinrtBuffer *self = (WinrtBuffer *)((BYTE *)This - offsetof(WinrtBuffer, byte_access));
    if (!value) return E_POINTER;
    *value = self->data;
    return S_OK;
}

static const IBufferByteAccessVtbl byte_access_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(IBufferByteAccess *, REFIID, void **))ba_QI,
    (ULONG (STDMETHODCALLTYPE *)(IBufferByteAccess *))ba_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(IBufferByteAccess *))ba_Release,
    (HRESULT (STDMETHODCALLTYPE *)(IBufferByteAccess *, ULONG *, IID **))ba_GetIids,
    (HRESULT (STDMETHODCALLTYPE *)(IBufferByteAccess *, HSTRING *))ba_GetClassName,
    (HRESULT (STDMETHODCALLTYPE *)(IBufferByteAccess *, TrustLevel_ *))ba_GetTrust,
    ba_Buffer
};

static HRESULT STDMETHODCALLTYPE buf_QI(IBuffer *This, REFIID riid, void **ppv)
{
    WinrtBuffer *self = (WinrtBuffer *)This;
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &kIID_BufferByteAccess)) {
        *ppv = &self->byte_access;
        InterlockedIncrement(&self->ref);
        return S_OK;
    }
    if (IsEqualIID(riid, &kIID_Buffer)) {
        *ppv = This;
        InterlockedIncrement(&self->ref);
        return S_OK;
    }
    if (IsEqualIID(riid, &kIID_Unknown) || IsEqualIID(riid, &kIID_AgileObject) ||
        (riid->Data3 & 0xF000) == 0x5000) {
        *ppv = This;
        InterlockedIncrement(&self->ref);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE buf_AddRef(IBuffer *This)
{
    return (ULONG)InterlockedIncrement(&((WinrtBuffer *)This)->ref);
}
static ULONG STDMETHODCALLTYPE buf_Release(IBuffer *This)
{
    WinrtBuffer *self = (WinrtBuffer *)This;
    LONG r = InterlockedDecrement(&self->ref);
    if (r == 0) {
        free(self->data);
        free(self);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE buf_GetIids(IBuffer *This, ULONG *c, IID **i)
{
    (void)This; (void)c; (void)i; return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE buf_GetClassName(IBuffer *This, HSTRING *n)
{
    (void)This; (void)n; return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE buf_GetTrust(IBuffer *This, TrustLevel_ *l)
{
    (void)This; (void)l; return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE buf_get_Capacity(IBuffer *This, UINT32 *v)
{
    if (!v) return E_POINTER;
    *v = ((WinrtBuffer *)This)->len;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE buf_get_Length(IBuffer *This, UINT32 *v)
{
    if (!v) return E_POINTER;
    *v = ((WinrtBuffer *)This)->len;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE buf_put_Length(IBuffer *This, UINT32 v)
{
    WinrtBuffer *self = (WinrtBuffer *)This;
    if (v > self->len) v = self->len;
    self->len = v;
    return S_OK;
}

static IBuffer *buffer_create(const uint8_t *data, int len)
{
    WinrtBuffer *b;
    if (len < 0) return NULL;
    b = (WinrtBuffer *)calloc(1, sizeof(WinrtBuffer));
    if (!b) return NULL;
    b->data = (BYTE *)malloc((size_t)(len > 0 ? len : 1));
    if (!b->data) { free(b); return NULL; }
    if (len > 0) memcpy(b->data, data, (size_t)len);
    b->len = (UINT32)len;
    b->ref = 1;
    b->iface.lpVtbl = &buffer_vtbl;
    b->byte_access.lpVtbl = &byte_access_vtbl;
    return &b->iface;
}

/* 用系统自带的 DataWriter 生成 IBuffer，跨公寓封送更可靠 */
static IBuffer *os_buffer_create(const uint8_t *data, int len)
{
    HSTRING cls;
    IInspectable *inst = NULL;
    IDataWriter *writer = NULL;
    IBuffer *out = NULL;

    if (len <= 0) return NULL;
    cls = make_hstring(L"Windows.Storage.Streams.DataWriter");
    if (!cls) return NULL;
    if (FAILED(RoActivateInstance(cls, (void **)&inst)) || !inst) {
        WindowsDeleteString(cls);
        return NULL;
    }
    WindowsDeleteString(cls);
    if (FAILED(inst->lpVtbl->QueryInterface(inst, &kIID_DataWriter, (void **)&writer)) || !writer) {
        inst->lpVtbl->Release(inst);
        return NULL;
    }
    inst->lpVtbl->Release(inst);

    if (SUCCEEDED(writer->lpVtbl->WriteBytes(writer, (UINT32)len, (BYTE *)data))) {
        writer->lpVtbl->DetachBuffer(writer, &out);
    }
    ((IInspectable *)writer)->lpVtbl->Release((IInspectable *)writer);
    return out;
}

/* ------------------------------------------------------------------ */
/* 委托对象（事件回调）                                                 */
/*                                                                      */
/* WinRT 委托以 IUnknown 为基类：vtable = QI/AddRef/Release + Invoke。    */
/* QueryInterface 只接受 IUnknown、IAgileObject 和“版本 5 的 WinRT       */
/* 参数化接口 IID”（委托本身的 IID）。其它探测一律 E_NOINTERFACE，       */
/* 否则 COM 会按 IMarshal/IAgileReference 等接口解析错误的 vtable。      */
/* ------------------------------------------------------------------ */
typedef struct Delegate {
    ITypedEventHandler iface;
    LONG ref;
    void (*fn)(IInspectable *args, void *user);
    void *user;
} Delegate;

static HRESULT STDMETHODCALLTYPE del_QI(ITypedEventHandler *This, REFIID riid, void **ppv)
{
    Delegate *self = (Delegate *)This;
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &kIID_Unknown) || IsEqualIID(riid, &kIID_AgileObject) ||
        (riid->Data3 & 0xF000) == 0x5000) {
        *ppv = This;
        InterlockedIncrement(&self->ref);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE del_AddRef(ITypedEventHandler *This)
{
    return (ULONG)InterlockedIncrement(&((Delegate *)This)->ref);
}
static ULONG STDMETHODCALLTYPE del_Release(ITypedEventHandler *This)
{
    Delegate *self = (Delegate *)This;
    LONG r = InterlockedDecrement(&self->ref);
    if (r == 0) free(self);
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE del_Invoke(ITypedEventHandler *This, IInspectable *sender, IInspectable *args)
{
    Delegate *self = (Delegate *)This;
    (void)sender;
    if (self->fn) self->fn(args, self->user);
    return S_OK;
}
static const ITypedEventHandlerVtbl delegate_vtbl = { del_QI, del_AddRef, del_Release, del_Invoke };

static Delegate *delegate_create(void (*fn)(IInspectable *, void *), void *user)
{
    Delegate *d = (Delegate *)calloc(1, sizeof(Delegate));
    if (!d) return NULL;
    d->iface.lpVtbl = &delegate_vtbl;
    d->ref = 1;
    d->fn = fn;
    d->user = user;
    return d;
}

/* ------------------------------------------------------------------ */
/* 扫描                                                                 */
/* ------------------------------------------------------------------ */
static void scan_on_adv(IInspectable *args_in, void *user)
{
    IBluetoothLEAdvertisementReceivedEventArgs *args = (IBluetoothLEAdvertisementReceivedEventArgs *)args_in;
    IBluetoothLEAdvertisementReceivedEventArgs2 *args2 = NULL;
    IBluetoothLEAdvertisement *adv = NULL;
    HSTRING hname = NULL;
    UINT64 addr = 0;
    INT16 rssi = 0;
    BOOLEAN connectable = TRUE;
    int addr_type = 2;
    wchar_t name[BLE_NAME_MAX] = L"";
    int i;

    (void)user;
    if (!args) return;
    args->lpVtbl->get_BluetoothAddress(args, &addr);
    args->lpVtbl->get_RawSignalStrengthInDBm(args, &rssi);

    if (SUCCEEDED(args_in->lpVtbl->QueryInterface(args_in, &kIID_AdvArgs2,
                                                  (void **)&args2)) && args2) {
        args2->lpVtbl->get_IsConnectable(args2, &connectable);
        args2->lpVtbl->get_BluetoothAddressType(args2, &addr_type);
        args2->lpVtbl->Release(args2);
    }
    if (SUCCEEDED(args->lpVtbl->get_Advertisement(args, &adv)) && adv) {
        if (SUCCEEDED(adv->lpVtbl->get_LocalName(adv, &hname)) && hname) {
            hstring_to_buf(hname, name, BLE_NAME_MAX);
            WindowsDeleteString(hname);
        }
        adv->lpVtbl->Release(adv);
    }

    EnterCriticalSection(&g_scan_lock);
    for (i = 0; i < g_device_count; i++) {
        if (g_devices[i].address == addr) break;
    }
    if (i < g_device_count) {
        BleDevice *d = &g_devices[i];
        if (name[0]) wcscpy_s(d->name, BLE_NAME_MAX, name);
        d->rssi = rssi;
        d->connectable = connectable ? TRUE : FALSE;
        if (d->address_type == 2) d->address_type = addr_type;
        d->last_seen = GetTickCount();
    } else if (g_device_count < BLE_MAX_DEVICES && connectable) {
        BleDevice *d = &g_devices[g_device_count];
        memset(d, 0, sizeof(*d));
        d->address = addr;
        d->rssi = rssi;
        d->connectable = TRUE;
        d->address_type = addr_type;
        d->last_seen = GetTickCount();
        if (name[0]) wcscpy_s(d->name, BLE_NAME_MAX, name);
        g_device_count++;
    }
    LeaveCriticalSection(&g_scan_lock);
}

BOOL ble_scan_start(void)
{
    HSTRING cls = NULL;
    IInspectable *inst = NULL;
    IBluetoothLEAdvertisementWatcher *watcher = NULL;
    HRESULT hr;

    if (g_scan_active) return TRUE;

    cls = make_hstring(L"Windows.Devices.Bluetooth.Advertisement.BluetoothLEAdvertisementWatcher");
    if (!cls) return FALSE;
    hr = RoActivateInstance(cls, (void **)&inst);
    WindowsDeleteString(cls);
    if (FAILED(hr) || !inst) {
        set_state(BLE_STATE_ERROR, L"无法创建 BLE 扫描器（0x%08lX）", (unsigned long)hr);
        return FALSE;
    }
    hr = inst->lpVtbl->QueryInterface(inst, &kIID_Watcher, (void **)&watcher);
    inst->lpVtbl->Release(inst);
    if (FAILED(hr) || !watcher) {
        set_state(BLE_STATE_ERROR, L"当前系统不支持 BLE 扫描（0x%08lX）", (unsigned long)hr);
        return FALSE;
    }

    g_scan_delegate = delegate_create(scan_on_adv, NULL);
    if (!g_scan_delegate) {
        ((IInspectable *)watcher)->lpVtbl->Release((IInspectable *)watcher);
        return FALSE;
    }

    watcher->lpVtbl->put_ScanningMode(watcher, BLE_SCANNING_ACTIVE);
    hr = watcher->lpVtbl->add_Received(watcher, g_scan_delegate, &g_scan_token);
    if (FAILED(hr)) {
        ((IInspectable *)watcher)->lpVtbl->Release((IInspectable *)watcher);
        del_Release(&((Delegate *)g_scan_delegate)->iface);
        g_scan_delegate = NULL;
        set_state(BLE_STATE_ERROR, L"订阅扫描事件失败（0x%08lX）", (unsigned long)hr);
        return FALSE;
    }
    g_scan_token_valid = TRUE;

    EnterCriticalSection(&g_scan_lock);
    g_device_count = 0;
    LeaveCriticalSection(&g_scan_lock);

    hr = watcher->lpVtbl->Start(watcher);
    if (FAILED(hr)) {
        watcher->lpVtbl->remove_Received(watcher, g_scan_token);
        g_scan_token_valid = FALSE;
        ((IInspectable *)watcher)->lpVtbl->Release((IInspectable *)watcher);
        del_Release(&((Delegate *)g_scan_delegate)->iface);
        g_scan_delegate = NULL;
        set_state(BLE_STATE_ERROR,
                  hr == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)
                      ? L"蓝牙未开启或不支持 BLE，请检查蓝牙开关"
                      : L"启动扫描失败（0x%08lX）", (unsigned long)hr);
        return FALSE;
    }

    g_watcher = watcher;
    g_scan_active = TRUE;
    set_state(BLE_STATE_IDLE, L"开始扫描 BLE 设备…");
    return TRUE;
}

void ble_scan_stop(void)
{
    if (!g_scan_active || !g_watcher) return;
    if (g_scan_token_valid) {
        g_watcher->lpVtbl->remove_Received(g_watcher, g_scan_token);
        g_scan_token_valid = FALSE;
    }
    g_watcher->lpVtbl->Stop(g_watcher);
    ((IInspectable *)g_watcher)->lpVtbl->Release((IInspectable *)g_watcher);
    g_watcher = NULL;
    if (g_scan_delegate) {
        del_Release(&((Delegate *)g_scan_delegate)->iface);
        g_scan_delegate = NULL;
    }
    g_scan_active = FALSE;
    set_state(BLE_STATE_IDLE, L"扫描已停止，共发现 %d 个设备", ble_device_count());
}

BOOL ble_scan_is_active(void)
{
    return g_scan_active;
}

int ble_device_count(void)
{
    int n;
    EnterCriticalSection(&g_scan_lock);
    n = g_device_count;
    LeaveCriticalSection(&g_scan_lock);
    return n;
}

BOOL ble_device_get(int index, BleDevice *out)
{
    BOOL ok = FALSE;
    EnterCriticalSection(&g_scan_lock);
    if (index >= 0 && index < g_device_count && out) {
        *out = g_devices[index];
        ok = TRUE;
    }
    LeaveCriticalSection(&g_scan_lock);
    return ok;
}

void ble_devices_clear(void)
{
    EnterCriticalSection(&g_scan_lock);
    g_device_count = 0;
    LeaveCriticalSection(&g_scan_lock);
}

/* ------------------------------------------------------------------ */
/* GATT 辅助                                                            */
/* ------------------------------------------------------------------ */
static const wchar_t *gatt_status_text(int st)
{
    switch (st) {
    case GATT_COMMUNICATION_SUCCESS:        return L"成功";
    case GATT_COMMUNICATION_UNREACHABLE:    return L"设备不可达（Unreachable）";
    case GATT_COMMUNICATION_PROTOCOL_ERROR: return L"GATT 协议错误（ProtocolError）";
    case GATT_COMMUNICATION_ACCESS_DENIED:  return L"访问被拒绝（AccessDenied）";
    default:                                return L"未知状态";
    }
}

static const wchar_t *gatt_status_hint(int st)
{
    switch (st) {
    case GATT_COMMUNICATION_UNREACHABLE:
        return L"设备可能已被手机/其它主机连接，或信号太弱、固件的 GATT 服务未响应";
    case GATT_COMMUNICATION_PROTOCOL_ERROR:
        return L"固件服务可能尚未就绪，或设备要求配对/加密";
    case GATT_COMMUNICATION_ACCESS_DENIED:
        return L"请先在 Windows「蓝牙和其他设备」里配对后再连接";
    default:
        return L"";
    }
}

static HRESULT get_device_services(IBluetoothLEDevice3 *dev3, IVectorView **out, int *status_out)
{
    IAsyncOperation *op = NULL;
    IGattDeviceServicesResult *res = NULL;
    int status = -1;
    HRESULT hr;

    *out = NULL;
    if (status_out) *status_out = -1;
    hr = dev3->lpVtbl->GetGattServicesWithCacheModeAsync(dev3, 1 /* Uncached */, &op);
    if (FAILED(hr)) return hr;
    hr = async_wait(op, 20000, &res);
    ((IInspectable *)op)->lpVtbl->Release((IInspectable *)op);
    if (FAILED(hr) || !res) return FAILED(hr) ? hr : E_FAIL;

    res->lpVtbl->get_Status(res, &status);
    if (status_out) *status_out = status;
    if (status != GATT_COMMUNICATION_SUCCESS) {
        ((IInspectable *)res)->lpVtbl->Release((IInspectable *)res);
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    hr = res->lpVtbl->get_Services(res, out);
    ((IInspectable *)res)->lpVtbl->Release((IInspectable *)res);
    return hr;
}

static HRESULT get_service_chars(IGattDeviceService3 *svc3, IVectorView **out)
{
    IAsyncOperation *op = NULL;
    IGattCharacteristicsResult *res = NULL;
    int status = -1;
    HRESULT hr;

    *out = NULL;
    hr = svc3->lpVtbl->GetCharacteristicsWithCacheModeAsync(svc3, 1 /* Uncached */, &op);
    if (FAILED(hr)) return hr;
    hr = async_wait(op, 20000, &res);
    ((IInspectable *)op)->lpVtbl->Release((IInspectable *)op);
    if (FAILED(hr) || !res) return FAILED(hr) ? hr : E_FAIL;

    res->lpVtbl->get_Status(res, &status);
    if (status != GATT_COMMUNICATION_SUCCESS) {
        ((IInspectable *)res)->lpVtbl->Release((IInspectable *)res);
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    hr = res->lpVtbl->get_Characteristics(res, out);
    ((IInspectable *)res)->lpVtbl->Release((IInspectable *)res);
    return hr;
}

/* 在给定服务的一组特征里挑选写/通知特征（优先 NUS 的 6E400002/6E400003） */
typedef struct CharPick {
    IGattCharacteristic *notify;
    IGattCharacteristic *write;
    int notify_props;
    int write_props;
} CharPick;

static void pick_chars(IVectorView *chars, CharPick *pick)
{
    UINT32 n = 0, i;
    if (!chars) return;
    chars->lpVtbl->get_Size(chars, &n);
    for (i = 0; i < n; i++) {
        IGattCharacteristic *ch = NULL;
        GUID uuid;
        int props = 0;
        int is_notify = 0, is_write = 0;

        if (FAILED(chars->lpVtbl->GetAt(chars, i, (void **)&ch)) || !ch) continue;
        ch->lpVtbl->get_Uuid(ch, &uuid);
        ch->lpVtbl->get_CharacteristicProperties(ch, &props);
        is_notify = (props & (GATT_PROP_NOTIFY | GATT_PROP_INDICATE)) ? 1 : 0;
        is_write = (props & (GATT_PROP_WRITE | GATT_PROP_WRITE_WITHOUT_RESPONSE)) ? 1 : 0;

        if (is_notify && (!pick->notify || IsEqualGUID(&uuid, &kUUID_NusTx))) {
            if (pick->notify) {
                ((IInspectable *)pick->notify)->lpVtbl->Release((IInspectable *)pick->notify);
            }
            pick->notify = ch;
            pick->notify_props = props;
            ((IInspectable *)ch)->lpVtbl->AddRef((IInspectable *)ch);
        }
        if (is_write && (!pick->write || IsEqualGUID(&uuid, &kUUID_NusRx))) {
            if (pick->write) {
                ((IInspectable *)pick->write)->lpVtbl->Release((IInspectable *)pick->write);
            }
            pick->write = ch;
            pick->write_props = props;
            ((IInspectable *)ch)->lpVtbl->AddRef((IInspectable *)ch);
        }
        ((IInspectable *)ch)->lpVtbl->Release((IInspectable *)ch);
    }
}

/* ------------------------------------------------------------------ */
/* 接收回调                                                             */
/* ------------------------------------------------------------------ */
static HRESULT ibuffer_read(IBuffer *buf, BYTE *dst, UINT32 cap, UINT32 *out_len)
{
    HSTRING cls = NULL;
    IDataReaderStatics *statics = NULL;
    IDataReader *reader = NULL;
    HRESULT hr;
    UINT32 avail = 0;

    *out_len = 0;
    cls = make_hstring(L"Windows.Storage.Streams.DataReader");
    if (!cls) return E_OUTOFMEMORY;
    hr = RoGetActivationFactory(cls, &kIID_DataReaderStatics, (void **)&statics);
    WindowsDeleteString(cls);
    if (FAILED(hr) || !statics) return FAILED(hr) ? hr : E_FAIL;
    hr = statics->lpVtbl->FromBuffer(statics, buf, &reader);
    ((IInspectable *)statics)->lpVtbl->Release((IInspectable *)statics);
    if (FAILED(hr) || !reader) return FAILED(hr) ? hr : E_FAIL;

    hr = reader->lpVtbl->get_UnconsumedBufferLength(reader, &avail);
    if (SUCCEEDED(hr) && avail > 0) {
        UINT32 n = avail > cap ? cap : avail;
        hr = reader->lpVtbl->ReadBytes(reader, n, dst);
        if (SUCCEEDED(hr)) *out_len = n;
    }
    ((IInspectable *)reader)->lpVtbl->Release((IInspectable *)reader);
    return hr;
}

static void rx_handle_args(IInspectable *args_in)
{
    IGattValueChangedEventArgs *args = (IGattValueChangedEventArgs *)args_in;
    IBuffer *buf = NULL;
    BYTE tmp[1024];
    UINT32 len = 0;
    HRESULT hr;

    if (!args) return;
    if (FAILED(args->lpVtbl->get_CharacteristicValue(args, &buf)) || !buf) return;

    /* 用 DataReader 读：事件里的 IBuffer 常是跨公寓代理，
       IBufferByteAccess::Buffer() 可能返回失败甚至崩溃。 */
    hr = ibuffer_read(buf, tmp, (UINT32)sizeof(tmp), &len);
    ((IInspectable *)buf)->lpVtbl->Release((IInspectable *)buf);

    {
        LONG n = InterlockedIncrement(&g_rx_packets);
        if (FAILED(hr)) {
            if (n <= 5 || (n % 50) == 0)
                log_write(L"[rx] #%ld read failed hr=0x%08lX", n, (unsigned long)hr);
            return;
        }
        if (len == 0) {
            if (n <= 3 || (n % 50) == 0) log_write(L"[rx] #%ld empty notification", n);
            return;
        }
        if (n <= 5 || (n % 50) == 0) {
            wchar_t hex[96];
            int show = len > 16 ? 16 : (int)len;
            hex_format(tmp, show, hex, 96);
            log_write(L"[rx] #%ld len=%u data=%s%s", n, len, hex, len > 16 ? L" ..." : L"");
        }
    }

    {
        UINT32 i;
        EnterCriticalSection(&g_rx_lock);
        for (i = 0; i < len; i++) {
            if (g_rx_count >= BLE_RX_RING) {
                InterlockedIncrement64(&g_rx_dropped);
                break;
            }
            g_rx_ring[g_rx_head] = tmp[i];
            g_rx_head = (g_rx_head + 1) % BLE_RX_RING;
            g_rx_count++;
        }
        LeaveCriticalSection(&g_rx_lock);
        InterlockedAdd64(&g_rx_total, len);
    }
}
/*
 * 通知回调。Windows.Devices.Bluetooth.dll 在设备异常断连时，
 * 偶尔会在 GattValueChangedEventArgs 内部读空指针，这里用 SEH 兜住，
 * 避免第三方 DLL 的访问冲突把整个上位机带走。
 */
static void rx_on_value(IInspectable *args_in, void *user)
{
    (void)user;
    if (!g_rx_enabled) return;
    __try {
        rx_handle_args(args_in);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_rx_enabled, 0);
        ble_log(L"接收回调触发异常（Windows 蓝牙栈），已暂停接收，请断开后重新连接");
    }
}

/* ------------------------------------------------------------------ */
/* 连接                                                                 */
/* ------------------------------------------------------------------ */
typedef struct ConnectParams {
    uint64_t address;
    LONG     gen;
    int      address_type;
    wchar_t  name[BLE_NAME_MAX];
} ConnectParams;

static void fill_uuid_field(wchar_t *dst, const GUID *g)
{
    guid_format(g, dst, BLE_UUID_MAX);
}

static DWORD WINAPI connect_thread_proc(LPVOID param)
{
    ConnectParams *cp = (ConnectParams *)param;
    IBluetoothLEDevice *dev = NULL;
    IBluetoothLEDevice3 *dev3 = NULL;
    IVectorView *services = NULL;
    IGattDeviceService *best_svc = NULL;
    IGattCharacteristic *best_notify = NULL;
    IGattCharacteristic *best_write = NULL;
    IGattSession *best_session = NULL;
    int best_score = -1;
    int best_notify_props = 0;
    int best_write_props = 0;
    int mtu = 23;
    BOOL canceled = FALSE;
    BOOL published = FALSE;
    BOOL subscribed = FALSE;
    EventRegistrationToken_ notify_token = {0};
    HSTRING hcls = NULL, hname = NULL;
    IAsyncOperation *op = NULL;
    HRESULT hr;

    RoInitialize(RO_INIT_MULTITHREADED);

    set_state(BLE_STATE_CONNECTING, L"正在连接 %s …", cp->name[0] ? cp->name : L"BLE 设备");

    /* 1. 用蓝牙地址创建设备对象 */
    hcls = make_hstring(L"Windows.Devices.Bluetooth.BluetoothLEDevice");
    hr = E_FAIL;
    if (hcls) {
        IBluetoothLEDeviceStatics *statics = NULL;
        hr = RoGetActivationFactory(hcls, &kIID_DeviceStatics, (void **)&statics);
        if (SUCCEEDED(hr) && statics) {
            IBluetoothLEDeviceStatics2 *statics2 = NULL;
            if (cp->address_type == 0 || cp->address_type == 1) {
                if (SUCCEEDED(((IInspectable *)statics)->lpVtbl->QueryInterface(
                        (IInspectable *)statics, &kIID_DeviceStatics2, (void **)&statics2)) && statics2) {
                    hr = statics2->lpVtbl->FromBluetoothAddressWithBluetoothAddressTypeAsync(
                        statics2, cp->address, cp->address_type, &op);
                    ((IInspectable *)statics2)->lpVtbl->Release((IInspectable *)statics2);
                    if (SUCCEEDED(hr) && op) {
                        ble_log(L"使用%s地址连接", cp->address_type == 1 ? L"随机" : L"公共");
                    }
                }
            }
            if (!op) {
                hr = statics->lpVtbl->FromBluetoothAddressAsync(statics, cp->address, &op);
            }
            ((IInspectable *)statics)->lpVtbl->Release((IInspectable *)statics);
        }
        WindowsDeleteString(hcls);
    }
    if (FAILED(hr) || !op) {
        set_state(BLE_STATE_ERROR, L"无法创建设备对象（0x%08lX）", (unsigned long)hr);
        goto done;
    }
    hr = async_wait(op, 20000, &dev);
    ((IInspectable *)op)->lpVtbl->Release((IInspectable *)op);
    op = NULL;
    if (g_connect_cancel || cp->gen != g_connect_gen) { canceled = TRUE; goto done; }
    if (FAILED(hr) || !dev) {
        if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            set_state(BLE_STATE_ERROR,
                      L"找不到设备（0x%08lX）。设备可能已休眠或走远，或使用了需要配对的随机地址；"
                      L"可先在 Windows「蓝牙和其他设备」中配对后重试。", (unsigned long)hr);
        } else {
            set_state(BLE_STATE_ERROR, L"连接失败：设备不在范围内、已休眠或被其它程序占用（0x%08lX）",
                      (unsigned long)hr);
        }
        goto done;
    }

    /* 2. 读取设备名 */
    {
        wchar_t name[BLE_NAME_MAX] = L"";
        if (SUCCEEDED(dev->lpVtbl->get_Name(dev, &hname)) && hname) {
            hstring_to_buf(hname, name, BLE_NAME_MAX);
            WindowsDeleteString(hname);
            hname = NULL;
        }
        if (name[0] || cp->name[0]) {
            EnterCriticalSection(&g_lock);
            wcscpy_s(g_status.device_name, BLE_NAME_MAX, name[0] ? name : cp->name);
            LeaveCriticalSection(&g_lock);
        }
    }

    hr = ((IInspectable *)dev)->lpVtbl->QueryInterface((IInspectable *)dev, &kIID_Device3,
                                                       (void **)&dev3);
    if (FAILED(hr) || !dev3) {
        set_state(BLE_STATE_ERROR, L"系统版本过低，需要 Windows 10 1809 及以上（0x%08lX）", (unsigned long)hr);
        goto done;
    }

    /* 3. 等链路就绪再发现服务（部分设备 FromBluetoothAddressAsync 返回时链路还没建立） */
    {
        int conn = -1;
        int i;
        for (i = 0; i < 30; i++) {
            if (SUCCEEDED(dev->lpVtbl->get_ConnectionStatus(dev, &conn)) &&
                conn == BLUETOOTH_CONNECTION_CONNECTED) {
                break;
            }
            if (g_connect_cancel || cp->gen != g_connect_gen) { canceled = TRUE; goto done; }
            Sleep(100);
        }
        if (conn != BLUETOOTH_CONNECTION_CONNECTED) {
            ble_log(L"链路状态未就绪，继续尝试发现服务…");
        }
    }

    set_state(BLE_STATE_DISCOVERING, L"正在发现 GATT 服务 …");
    {
        int attempt;
        int gatt_status = -1;
        for (attempt = 1; attempt <= 3; attempt++) {
            gatt_status = -1;
            hr = get_device_services(dev3, &services, &gatt_status);
            if (SUCCEEDED(hr) && services) break;
            if (g_connect_cancel || cp->gen != g_connect_gen) { canceled = TRUE; goto done; }
            ble_log(L"服务发现失败（第 %d/3 次）：%s%s%s", attempt,
                    gatt_status_text(gatt_status),
                    gatt_status_hint(gatt_status)[0] ? L" — " : L"",
                    gatt_status_hint(gatt_status));
            if (attempt < 3) Sleep(900);
        }
        if (FAILED(hr) || !services) {
            set_state(BLE_STATE_ERROR, L"发现服务失败：%s。%s",
                      gatt_status_text(gatt_status), gatt_status_hint(gatt_status));
            goto done;
        }
    }

    {
        UINT32 count = 0, i;
        services->lpVtbl->get_Size(services, &count);
        ble_log(L"发现 %u 个 GATT 服务：", count);
        for (i = 0; i < count; i++) {
            IGattDeviceService *svc = NULL;
            IGattDeviceService3 *svc3 = NULL;
            IGattSession *session = NULL;
            IVectorView *chars = NULL;
            CharPick pick;
            GUID uuid;
            wchar_t uuid_text[BLE_UUID_MAX];
            int score = 0;

            if (FAILED(services->lpVtbl->GetAt(services, i, (void **)&svc)) || !svc) continue;
            if (FAILED(((IInspectable *)svc)->lpVtbl->QueryInterface((IInspectable *)svc,
                                                                     &kIID_Service3,
                                                                     (void **)&svc3)) || !svc3) {
                ((IInspectable *)svc)->lpVtbl->Release((IInspectable *)svc);
                continue;
            }
            svc->lpVtbl->get_Uuid(svc, &uuid);
            fill_uuid_field(uuid_text, &uuid);
            ble_log(L"  服务 %s", uuid_text);

            /* 打开会话并保持连接，同时读取 MTU */
            if (SUCCEEDED(svc3->lpVtbl->get_Session(svc3, (void **)&session)) && session) {
                UINT16 pdu = 0;
                session->lpVtbl->put_MaintainConnection(session, TRUE);
                if (SUCCEEDED(session->lpVtbl->get_MaxPduSize(session, &pdu)) && pdu > 23) mtu = pdu;
            }

            memset(&pick, 0, sizeof(pick));
            if (SUCCEEDED(get_service_chars(svc3, &chars))) {
                pick_chars(chars, &pick);
            }

            if (pick.notify && pick.write) {
                if (IsEqualGUID(&uuid, &kUUID_NusService)) score = 100;
                else if (IsEqualGUID(&uuid, &kUUID_Ffe0)) score = 90;
                else score = 50;
            } else if (pick.notify) {
                score = 10;
            }

            if (score > best_score && pick.notify) {
                /* 换更好的服务：释放旧的候选 */
                if (best_session) {
                    best_session->lpVtbl->put_MaintainConnection(best_session, FALSE);
                    ((IInspectable *)best_session)->lpVtbl->Release((IInspectable *)best_session);
                }
                if (best_svc) ((IInspectable *)best_svc)->lpVtbl->Release((IInspectable *)best_svc);
                if (best_notify) ((IInspectable *)best_notify)->lpVtbl->Release((IInspectable *)best_notify);
                if (best_write) ((IInspectable *)best_write)->lpVtbl->Release((IInspectable *)best_write);

                best_svc = svc;
                best_notify = pick.notify;
                best_write = pick.write;
                best_session = session;
                best_notify_props = pick.notify_props;
                best_write_props = pick.write_props;
                best_score = score;
                svc = NULL;            /* 所有权转移 */
                session = NULL;
                pick.notify = NULL;
                pick.write = NULL;
            } else {
                if (pick.notify) ((IInspectable *)pick.notify)->lpVtbl->Release((IInspectable *)pick.notify);
                if (pick.write) ((IInspectable *)pick.write)->lpVtbl->Release((IInspectable *)pick.write);
            }

            if (session) {
                session->lpVtbl->put_MaintainConnection(session, FALSE);
                ((IInspectable *)session)->lpVtbl->Release((IInspectable *)session);
            }
            if (chars) ((IInspectable *)chars)->lpVtbl->Release((IInspectable *)chars);
            if (svc3) ((IInspectable *)svc3)->lpVtbl->Release((IInspectable *)svc3);
            if (svc) ((IInspectable *)svc)->lpVtbl->Release((IInspectable *)svc);

            if (g_connect_cancel || cp->gen != g_connect_gen) { canceled = TRUE; goto done; }
        }
    }

    if (!best_notify) {
        set_state(BLE_STATE_ERROR, L"未找到可通知的特征值：请确认固件已开启 Notify（CCCD）");
        goto done;
    }
    if (!best_write) {
        ble_log(L"注意：该服务没有可写特征，只能接收数据");
    }

    /* 4. 订阅通知 */
    {
        Delegate *rx_delegate = delegate_create(rx_on_value, NULL);
        if (!rx_delegate) {
            set_state(BLE_STATE_ERROR, L"内存不足");
            goto done;
        }
        hr = best_notify->lpVtbl->add_ValueChanged(best_notify, rx_delegate, &notify_token);
        if (FAILED(hr)) {
            del_Release(&rx_delegate->iface);
            set_state(BLE_STATE_ERROR, L"订阅通知失败（0x%08lX）", (unsigned long)hr);
            goto done;
        }
        subscribed = TRUE;
        /* 事件源会持有委托引用，本地引用释放即可 */
        del_Release(&rx_delegate->iface);

        {
            int cccd = (best_notify_props & GATT_PROP_NOTIFY) ? GATT_CCCD_NOTIFY : GATT_CCCD_INDICATE;
            int status = -1;
            op = NULL;
            hr = best_notify->lpVtbl->WriteClientCharacteristicConfigurationDescriptorAsync(best_notify, cccd, &op);
            if (SUCCEEDED(hr) && op) {
                hr = async_wait(op, 10000, &status);
                ((IInspectable *)op)->lpVtbl->Release((IInspectable *)op);
                op = NULL;
            }
            if (FAILED(hr) || status != GATT_COMMUNICATION_SUCCESS) {
                set_state(BLE_STATE_ERROR, L"开启通知失败（0x%08lX）%s", (unsigned long)hr,
                          att_error_hint(hr));
                goto done;
            }
        }
    }

    if (g_connect_cancel || cp->gen != g_connect_gen) { canceled = TRUE; goto done; }

    /* 5. 发布连接结果 */
    {
        wchar_t svc_uuid[BLE_UUID_MAX] = L"-", notify_uuid[BLE_UUID_MAX] = L"-", write_uuid[BLE_UUID_MAX] = L"-";
        GUID g;
        if (best_svc) {
            best_svc->lpVtbl->get_Uuid(best_svc, &g);
            fill_uuid_field(svc_uuid, &g);
        }
        if (best_notify) {
            best_notify->lpVtbl->get_Uuid(best_notify, &g);
            fill_uuid_field(notify_uuid, &g);
        }
        if (best_write) {
            best_write->lpVtbl->get_Uuid(best_write, &g);
            fill_uuid_field(write_uuid, &g);
        }

        EnterCriticalSection(&g_lock);
        g_dev = dev; dev = NULL;
        g_dev3 = dev3; dev3 = NULL;
        g_svc = best_svc; best_svc = NULL;
        g_notify_chr = best_notify; best_notify = NULL;
        g_write_chr = best_write; best_write = NULL;
        g_session = best_session; best_session = NULL;
        g_notify_token = notify_token;
        g_notify_token_valid = TRUE;
        g_write_props = best_write_props;
        g_write_option = (best_write_props & GATT_PROP_WRITE) ? GATT_WRITE_WITH_RESPONSE
                                                              : GATT_WRITE_WITHOUT_RESPONSE;
        g_status.mtu = mtu;
        g_status.write_with_response = (g_write_option == GATT_WRITE_WITH_RESPONSE);
        wcscpy_s(g_status.service_uuid, BLE_UUID_MAX, svc_uuid);
        wcscpy_s(g_status.notify_uuid, BLE_UUID_MAX, notify_uuid);
        wcscpy_s(g_status.write_uuid, BLE_UUID_MAX, write_uuid);
        InterlockedExchange(&g_rx_enabled, 1);
        g_status.state = BLE_STATE_CONNECTED;
        _snwprintf_s(g_status.message, BLE_MSG_MAX, _TRUNCATE, L"已连接 %s",
                     g_status.device_name[0] ? g_status.device_name : L"BLE 设备");
        g_disc_seen = 0;
        LeaveCriticalSection(&g_lock);
        published = TRUE;

        ble_log(L"  服务   %s", svc_uuid);
        ble_log(L"  通知   %s (%s)", notify_uuid,
                (best_notify_props & GATT_PROP_NOTIFY) ? L"Notify" : L"Indicate");
        ble_log(L"  写入   %s (%s)", write_uuid[0] ? write_uuid : L"无",
                g_write_option == GATT_WRITE_WITH_RESPONSE ? L"带响应" : L"无响应");
        ble_log(L"  MTU    %d 字节（单包有效数据约 %d 字节）", mtu, mtu > 3 ? mtu - 3 : 20);
        notify_status();
    }

done:
    if (canceled) {
        ble_log(L"已取消连接");
        set_state(BLE_STATE_IDLE, L"已取消");
    } else if (!published) {
        BleState st;
        EnterCriticalSection(&g_lock);
        st = g_status.state;
        LeaveCriticalSection(&g_lock);
        if (st == BLE_STATE_CONNECTING || st == BLE_STATE_DISCOVERING) {
            set_state(BLE_STATE_ERROR, L"连接未完成");
        }
    }
    if (op) ((IInspectable *)op)->lpVtbl->Release((IInspectable *)op);
    if (subscribed && best_notify) {
        best_notify->lpVtbl->remove_ValueChanged(best_notify, notify_token);
    }
    if (best_notify) ((IInspectable *)best_notify)->lpVtbl->Release((IInspectable *)best_notify);
    if (best_write) ((IInspectable *)best_write)->lpVtbl->Release((IInspectable *)best_write);
    if (best_session) {
        best_session->lpVtbl->put_MaintainConnection(best_session, FALSE);
        ((IInspectable *)best_session)->lpVtbl->Release((IInspectable *)best_session);
    }
    if (best_svc) ((IInspectable *)best_svc)->lpVtbl->Release((IInspectable *)best_svc);
    if (services) ((IInspectable *)services)->lpVtbl->Release((IInspectable *)services);
    if (dev3) ((IInspectable *)dev3)->lpVtbl->Release((IInspectable *)dev3);
    if (dev) ((IInspectable *)dev)->lpVtbl->Release((IInspectable *)dev);
    if (g_connect_thread) {
        CloseHandle(g_connect_thread);
        g_connect_thread = NULL;
    }
    free(cp);
    RoUninitialize();
    return 0;
}

void ble_connect(uint64_t address, int address_type, const wchar_t *name_hint)
{
    ConnectParams *cp;
    BleStatus st;

    ble_get_status(&st);
    if (st.state == BLE_STATE_CONNECTED || st.state == BLE_STATE_CONNECTING ||
        st.state == BLE_STATE_DISCOVERING) {
        ble_log(L"请先断开当前连接");
        return;
    }

    cp = (ConnectParams *)calloc(1, sizeof(ConnectParams));
    if (!cp) return;
    cp->address = address;
    cp->address_type = address_type;
    cp->gen = InterlockedIncrement(&g_connect_gen);
    if (name_hint) wcscpy_s(cp->name, BLE_NAME_MAX, name_hint);

    InterlockedExchange(&g_connect_cancel, 0);
    EnterCriticalSection(&g_lock);
    g_status.state = BLE_STATE_CONNECTING;
    bt_addr_format(address, g_status.address, 24);
    wcscpy_s(g_status.device_name, BLE_NAME_MAX, name_hint ? name_hint : L"");
    LeaveCriticalSection(&g_lock);

    if (g_connect_thread) {
        CloseHandle(g_connect_thread);
        g_connect_thread = NULL;
    }
    g_connect_thread = CreateThread(NULL, 0, connect_thread_proc, cp, 0, NULL);
    if (!g_connect_thread) {
        free(cp);
        set_state(BLE_STATE_ERROR, L"无法创建连接线程");
    }
}

void ble_disconnect(void)
{
    IBluetoothLEDevice *dev;
    IBluetoothLEDevice3 *dev3;
    IGattDeviceService *svc;
    IGattCharacteristic *notify;
    IGattCharacteristic *write;
    IGattSession *session;
    EventRegistrationToken_ token;
    BOOL have_token;

    InterlockedExchange(&g_connect_cancel, 1);
    InterlockedIncrement(&g_connect_gen);
    InterlockedExchange(&g_rx_enabled, 0);

    EnterCriticalSection(&g_lock);
    dev = g_dev; g_dev = NULL;
    dev3 = g_dev3; g_dev3 = NULL;
    svc = g_svc; g_svc = NULL;
    notify = g_notify_chr; g_notify_chr = NULL;
    write = g_write_chr; g_write_chr = NULL;
    session = g_session; g_session = NULL;
    token = g_notify_token;
    have_token = g_notify_token_valid;
    g_notify_token_valid = FALSE;
    g_status.state = BLE_STATE_IDLE;
    wcscpy_s(g_status.message, BLE_MSG_MAX, L"已断开");
    LeaveCriticalSection(&g_lock);

    if (notify && have_token) {
        notify->lpVtbl->remove_ValueChanged(notify, token);
    }
    if (notify) ((IInspectable *)notify)->lpVtbl->Release((IInspectable *)notify);
    if (write) ((IInspectable *)write)->lpVtbl->Release((IInspectable *)write);
    if (session) {
        session->lpVtbl->put_MaintainConnection(session, FALSE);
        ((IInspectable *)session)->lpVtbl->Release((IInspectable *)session);
    }
    if (svc) {
        IClosable *closable = NULL;
        if (SUCCEEDED(((IInspectable *)svc)->lpVtbl->QueryInterface((IInspectable *)svc,
                                                                     &kIID_Closable,
                                                                     (void **)&closable)) && closable) {
            closable->lpVtbl->Close(closable);
            ((IInspectable *)closable)->lpVtbl->Release((IInspectable *)closable);
        }
        ((IInspectable *)svc)->lpVtbl->Release((IInspectable *)svc);
    }
    if (dev3) ((IInspectable *)dev3)->lpVtbl->Release((IInspectable *)dev3);
    if (dev) ((IInspectable *)dev)->lpVtbl->Release((IInspectable *)dev);

    /* 清空发送队列 */
    EnterCriticalSection(&g_tx_lock);
    while (g_tx_head) {
        TxChunk *c = g_tx_head;
        g_tx_head = c->next;
        free(c);
    }
    g_tx_tail = NULL;
    g_tx_queued = 0;
    LeaveCriticalSection(&g_tx_lock);

    if (dev || svc || notify) {
        set_state(BLE_STATE_IDLE, L"已断开");
    } else {
        notify_status();
    }
}

BOOL ble_is_connected(void)
{
    BOOL b;
    EnterCriticalSection(&g_lock);
    b = (g_status.state == BLE_STATE_CONNECTED) ? TRUE : FALSE;
    LeaveCriticalSection(&g_lock);
    return b;
}

void ble_get_status(BleStatus *out)
{
    if (!out) return;
    EnterCriticalSection(&g_lock);
    *out = g_status;
    LeaveCriticalSection(&g_lock);
    out->rx_bytes = (uint64_t)g_rx_total;
    out->tx_bytes = (uint64_t)g_tx_total;
    out->dropped_bytes = (uint64_t)g_rx_dropped;
}

void ble_poll(void)
{
    IBluetoothLEDevice *dev = NULL;
    int status = -1;
    BOOL lost = FALSE;

    EnterCriticalSection(&g_lock);
    if (g_status.state == BLE_STATE_CONNECTED && g_dev) {
        dev = g_dev;
        ((IInspectable *)dev)->lpVtbl->AddRef((IInspectable *)dev);
    }
    LeaveCriticalSection(&g_lock);
    if (!dev) { g_disc_seen = 0; return; }

    {
        HRESULT hr = E_FAIL;
        __try {
            hr = dev->lpVtbl->get_ConnectionStatus(dev, &status);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            hr = E_FAIL;
            ble_log(L"查询连接状态时触发异常（Windows 蓝牙栈），按掉线处理");
        }
        if (SUCCEEDED(hr)) {
            if (status != BLUETOOTH_CONNECTION_CONNECTED) {
                if (++g_disc_seen >= 2) lost = TRUE;
            } else {
                g_disc_seen = 0;
            }
        } else {
            lost = TRUE;
        }
    }
    ((IInspectable *)dev)->lpVtbl->Release((IInspectable *)dev);

    if (lost) {
        ble_log(L"链路已断开（设备掉线或超出范围）");
        ble_disconnect();
        set_state(BLE_STATE_ERROR, L"连接已断开");
    }
}

/* ------------------------------------------------------------------ */
/* 发送                                                                 */
/* ------------------------------------------------------------------ */
static void tx_write_one(const uint8_t *data, int len)
{
    IGattCharacteristic *chr = NULL;
    IBuffer *buf;
    IAsyncOperation *op = NULL;
    int option, props, status = -1;
    HRESULT hr;

    EnterCriticalSection(&g_lock);
    if (g_write_chr) {
        chr = g_write_chr;
        ((IInspectable *)chr)->lpVtbl->AddRef((IInspectable *)chr);
    }
    option = g_write_option;
    props = g_write_props;
    LeaveCriticalSection(&g_lock);
    if (!chr) return;

    buf = os_buffer_create(data, len);
    if (!buf) buf = buffer_create(data, len);
    if (!buf) {
        ((IInspectable *)chr)->lpVtbl->Release((IInspectable *)chr);
        return;
    }
    __try {
        hr = chr->lpVtbl->WriteValueWithOptionAsync(chr, buf, option, &op);
        if (SUCCEEDED(hr) && op) {
            hr = async_wait(op, 5000, &status);
            ((IInspectable *)op)->lpVtbl->Release((IInspectable *)op);
            op = NULL;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hr = E_FAIL;
        status = -1;
        ble_log(L"写入特征值时触发异常（Windows 蓝牙栈）");
    }
    /* 带响应写入被设备拒绝时，若支持无响应写入则自动重试一次 */
    if ((FAILED(hr) || status != GATT_COMMUNICATION_SUCCESS) && option == GATT_WRITE_WITH_RESPONSE &&
        (props & GATT_PROP_WRITE_WITHOUT_RESPONSE)) {
        op = NULL;
        status = -1;
        hr = chr->lpVtbl->WriteValueWithOptionAsync(chr, buf, GATT_WRITE_WITHOUT_RESPONSE, &op);
        if (SUCCEEDED(hr) && op) {
            hr = async_wait(op, 5000, &status);
            ((IInspectable *)op)->lpVtbl->Release((IInspectable *)op);
        }
        if (SUCCEEDED(hr) && status == GATT_COMMUNICATION_SUCCESS) {
            EnterCriticalSection(&g_lock);
            g_write_option = GATT_WRITE_WITHOUT_RESPONSE;
            g_status.write_with_response = 0;
            LeaveCriticalSection(&g_lock);
            ble_log(L"设备不接受带响应写入，已自动切换为无响应写入");
        }
    }
    ((IInspectable *)buf)->lpVtbl->Release((IInspectable *)buf);
    ((IInspectable *)chr)->lpVtbl->Release((IInspectable *)chr);

    if (FAILED(hr) || status != GATT_COMMUNICATION_SUCCESS) {
        ble_log(L"发送失败（0x%08lX）%s", (unsigned long)hr, att_error_hint(hr));
        if (status == GATT_COMMUNICATION_UNREACHABLE && ble_is_connected()) {
            ble_disconnect();
            set_state(BLE_STATE_ERROR, L"连接已断开");
        }
        return;
    }
    InterlockedAdd64(&g_tx_total, len);
    {
        static volatile LONG tx_logged;
        LONG n = InterlockedIncrement(&tx_logged);
        if (n <= 5 || (n % 50) == 0) {
            wchar_t hex[96];
            int show = (len > 16) ? 16 : len;
            hex_format(data, show, hex, 96);
            log_write(L"[tx] #%ld len=%d data=%s%s", n, len, hex, len > 16 ? L" ..." : L"");
        }
    }
}

static DWORD WINAPI tx_thread_proc(LPVOID param)
{
    (void)param;
    RoInitialize(RO_INIT_MULTITHREADED);
    for (;;) {
        DWORD wait;
        if (g_tx_stop) break;
        wait = WaitForSingleObject(g_tx_event, 200);
        if (g_tx_stop) break;
        if (wait != WAIT_OBJECT_0) continue;
        for (;;) {
            TxChunk *c;
            int interval;
            EnterCriticalSection(&g_tx_lock);
            c = g_tx_head;
            if (c) {
                g_tx_head = c->next;
                if (!g_tx_head) g_tx_tail = NULL;
                if (g_tx_queued > 0) g_tx_queued--;
            }
            interval = g_tx_interval;
            LeaveCriticalSection(&g_tx_lock);
            if (!c) break;
            if (g_tx_stop) { free(c); break; }
            tx_write_one(c->data, c->len);
            free(c);
            if (interval > 0) Sleep(interval);
        }
    }
    RoUninitialize();
    return 0;
}

int ble_send(const uint8_t *data, int len, int chunk_size, int interval_ms)
{
    int offset = 0;
    int queued = 0;

    if (!data || len <= 0) return BLE_ERR_EMPTY;
    if (!ble_is_connected()) return BLE_ERR_NOT_CONN;
    if (g_tx_queued >= 64) return BLE_ERR_BUSY;
    if (chunk_size < 1) chunk_size = 20;
    if (chunk_size > 512) chunk_size = 512;
    if (interval_ms < 0) interval_ms = 0;
    if (interval_ms > 10000) interval_ms = 10000;

    while (offset < len) {
        int n = len - offset;
        TxChunk *c;
        if (n > chunk_size) n = chunk_size;
        c = (TxChunk *)malloc(sizeof(TxChunk) + (size_t)n);
        if (!c) break;
        c->next = NULL;
        c->len = n;
        memcpy(c->data, data + offset, (size_t)n);
        EnterCriticalSection(&g_tx_lock);
        if (g_tx_tail) g_tx_tail->next = c;
        else g_tx_head = c;
        g_tx_tail = c;
        g_tx_queued++;
        LeaveCriticalSection(&g_tx_lock);
        offset += n;
        queued++;
    }

    if (queued == 0) return BLE_ERR_MEMORY;
    log_write(L"[tx] enqueue %d bytes, chunk=%d, interval=%dms", len, chunk_size, interval_ms);
    EnterCriticalSection(&g_tx_lock);
    g_tx_interval = interval_ms;
    LeaveCriticalSection(&g_tx_lock);
    SetEvent(g_tx_event);
    return BLE_OK;
}

int ble_rx_read(uint8_t *dst, int cap)
{
    int n = 0;
    if (!dst || cap <= 0) return 0;
    EnterCriticalSection(&g_rx_lock);
    while (n < cap && g_rx_count > 0) {
        dst[n++] = g_rx_ring[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1) % BLE_RX_RING;
        g_rx_count--;
    }
    LeaveCriticalSection(&g_rx_lock);
    return n;
}

/* ------------------------------------------------------------------ */
/* 生命周期                                                             */
/* ------------------------------------------------------------------ */
void ble_init(const BleCallbacks *cb)
{
    InitializeCriticalSection(&g_lock);
    InitializeCriticalSection(&g_scan_lock);
    InitializeCriticalSection(&g_tx_lock);
    InitializeCriticalSection(&g_rx_lock);

    if (cb) g_cb = *cb;
    memset(&g_status, 0, sizeof(g_status));
    g_status.state = BLE_STATE_IDLE;
    g_status.mtu = 23;
    wcscpy_s(g_status.message, BLE_MSG_MAX, L"就绪，请扫描并选择 BLE 设备");

    g_tx_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_tx_thread = CreateThread(NULL, 0, tx_thread_proc, NULL, 0, NULL);
}

void ble_shutdown(void)
{
    ble_scan_stop();
    ble_disconnect();

    InterlockedExchange(&g_tx_stop, 1);
    if (g_tx_event) SetEvent(g_tx_event);
    if (g_tx_thread) {
        WaitForSingleObject(g_tx_thread, 2000);
        CloseHandle(g_tx_thread);
        g_tx_thread = NULL;
    }
    if (g_tx_event) {
        CloseHandle(g_tx_event);
        g_tx_event = NULL;
    }
    if (g_connect_thread) {
        WaitForSingleObject(g_connect_thread, 5000);
        CloseHandle(g_connect_thread);
        g_connect_thread = NULL;
    }

    /* 进程即将退出：临界区/句柄由系统回收。
       这里不 DeleteCriticalSection，避免仍有线程在使用时崩溃。 */
}
