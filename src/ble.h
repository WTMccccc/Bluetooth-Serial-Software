/*
 * ble.h - BLE 串口透传引擎（WinRT 调用全部封装在这里）
 *
 * 线程模型：
 *   - 扫描回调由 WinRT 线程池调用，只更新设备表（内部加锁）。
 *   - 连接/服务发现运行在独立工作线程，通过回调通知 UI 状态变化。
 *   - 发送使用独立线程 + 队列，按“分包大小/间隔”逐包写入。
 *   - 接收数据进入环形缓冲区，UI 定时用 ble_rx_read() 取走。
 */
#ifndef BLE_H
#define BLE_H

#include <windows.h>
#include <stdint.h>

#define BLE_MAX_DEVICES 128
#define BLE_NAME_MAX    64
#define BLE_UUID_MAX    64
#define BLE_MSG_MAX     200
#define BLE_RX_RING     65536

typedef struct BleDevice {
    uint64_t address;
    wchar_t  name[BLE_NAME_MAX];
    int      rssi;
    BOOL     connectable;
    int      address_type;   /* 0=公共地址, 1=随机地址, 2=未知 */
    DWORD    last_seen;      /* GetTickCount() */
} BleDevice;

typedef enum BleState {
    BLE_STATE_IDLE = 0,
    BLE_STATE_CONNECTING,
    BLE_STATE_DISCOVERING,
    BLE_STATE_CONNECTED,
    BLE_STATE_ERROR
} BleState;

typedef struct BleStatus {
    BleState state;
    wchar_t  device_name[BLE_NAME_MAX];
    wchar_t  address[24];
    wchar_t  service_uuid[BLE_UUID_MAX];
    wchar_t  notify_uuid[BLE_UUID_MAX];
    wchar_t  write_uuid[BLE_UUID_MAX];
    int      mtu;
    int      write_with_response;
    wchar_t  message[BLE_MSG_MAX];
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t dropped_bytes;
} BleStatus;

typedef struct BleCallbacks {
    /* 连接状态 / 信息变化（任意线程调用，实现里只应做 PostMessage） */
    void (*on_status_changed)(void);
    /* 一行日志文本（任意线程调用） */
    void (*on_log)(const wchar_t *line);
} BleCallbacks;

/* 返回值 / 错误码 */
#define BLE_OK              0
#define BLE_ERR_NOT_READY  -1
#define BLE_ERR_NOT_CONN   -2
#define BLE_ERR_EMPTY      -3
#define BLE_ERR_MEMORY     -4
#define BLE_ERR_STATE      -5
#define BLE_ERR_BUSY       -6

void  ble_init(const BleCallbacks *cb);
void  ble_shutdown(void);

/* ---- 扫描 ---- */
BOOL  ble_scan_start(void);
void  ble_scan_stop(void);
BOOL  ble_scan_is_active(void);
int   ble_device_count(void);
BOOL  ble_device_get(int index, BleDevice *out);
void  ble_devices_clear(void);

/* ---- 连接 ---- */
void  ble_connect(uint64_t address, int address_type, const wchar_t *name_hint);
void  ble_disconnect(void);
BOOL  ble_is_connected(void);
void  ble_get_status(BleStatus *out);
/* UI 定时调用：检测链路是否掉线 */
void  ble_poll(void);

/* ---- 收发 ---- */
int   ble_send(const uint8_t *data, int len, int chunk_size, int interval_ms);
int   ble_rx_read(uint8_t *dst, int cap);

#endif /* BLE_H */
