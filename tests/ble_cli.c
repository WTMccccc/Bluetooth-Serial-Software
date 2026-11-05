/* ble_cli.c - console test: scan, connect (by index or name), send, dump RX */
#include "../src/ble.h"
#include "../src/util.h"
#include "../src/winrt_abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_status(void)
{
    BleStatus st;
    char u8[512];
    ble_get_status(&st);
    w_to_u8(st.message, -1, u8, sizeof(u8));
    printf("[state] %s\n", u8);
}

static void on_log(const wchar_t *line)
{
    char u8[1024];
    w_to_u8(line, -1, u8, sizeof(u8));
    printf("[log] %s\n", u8);
}

static int find_by_name(const char *name)
{
    wchar_t wname[128];
    int n, i;
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 128) <= 0) return -1;
    n = ble_device_count();
    for (i = 0; i < n; i++) {
        BleDevice d;
        if (!ble_device_get(i, &d)) continue;
        if (wcsstr(d.name, wname)) return i;
    }
    return -1;
}

int main(int argc, char **argv)
{
    BleCallbacks cb;
    int n, i, pick = -1;

    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_MULTITHREADED);
    cb.on_status_changed = on_status;
    cb.on_log = on_log;
    ble_init(&cb);

    printf("scanning 5s...\n");
    if (!ble_scan_start()) {
        printf("scan start failed\n");
        ble_shutdown();
        return 1;
    }
    Sleep(5000);
    ble_scan_stop();

    n = ble_device_count();
    printf("devices=%d\n", n);
    for (i = 0; i < n; i++) {
        BleDevice d;
        wchar_t addr[32];
        char name_utf8[256] = "";
        char addr_utf8[64] = "";
        if (!ble_device_get(i, &d)) continue;
        bt_addr_format(d.address, addr, 32);
        w_to_u8(d.name, -1, name_utf8, sizeof(name_utf8));
        w_to_u8(addr, -1, addr_utf8, sizeof(addr_utf8));
        printf("  [%d] %-24s %s  rssi=%d\n", i,
               name_utf8[0] ? name_utf8 : "(unnamed)", addr_utf8, d.rssi);
    }

    if (argc > 1) {
        if (strncmp(argv[1], "name:", 5) == 0) pick = find_by_name(argv[1] + 5);
        else pick = atoi(argv[1]);
    }
    if (pick >= 0 && pick < n) {
        BleDevice d;
        BleStatus st;
        ble_device_get(pick, &d);
        {
            const char *force = getenv("BLE_FORCE_TYPE");
            if (force) {
                d.address_type = atoi(force);
                printf("forced address type=%d\n", d.address_type);
            }
        }
        printf("connecting to [%d] ...\n", pick);
        ble_connect(d.address, d.address_type, d.name);

        for (i = 0; i < 30; i++) {
            Sleep(1000);
            ble_get_status(&st);
            if (st.state == BLE_STATE_CONNECTED || st.state == BLE_STATE_ERROR ||
                st.state == BLE_STATE_IDLE) break;
        }
        ble_get_status(&st);
        {
            char msg[512];
            w_to_u8(st.message, -1, msg, sizeof(msg));
            printf("state=%d mtu=%d msg=%s\n", (int)st.state, st.mtu, msg);
        }

        if (st.state == BLE_STATE_CONNECTED) {
            const char *hello = "hello from C host\r\n";
            uint8_t buf[512];
            int got;
            printf("sending...\n");
            ble_send((const uint8_t *)hello, (int)strlen(hello), 20, 20);
            Sleep(3000);
            got = ble_rx_read(buf, (int)sizeof(buf));
            ble_get_status(&st);
            printf("tx=%llu rx=%llu dropped=%llu\n",
                   (unsigned long long)st.tx_bytes, (unsigned long long)st.rx_bytes,
                   (unsigned long long)st.dropped_bytes);
            printf("rx_chunk=%d\n", got);
            if (got > 0) {
                printf("data: ");
                for (i = 0; i < got && i < 64; i++) printf("%02X ", buf[i]);
                printf("\n");
            }
            Sleep(2000);
            got = ble_rx_read(buf, (int)sizeof(buf));
            printf("rx_more=%d\n", got);
            ble_disconnect();
        }
    }

    ble_shutdown();
    RoUninitialize();
    return 0;
}
