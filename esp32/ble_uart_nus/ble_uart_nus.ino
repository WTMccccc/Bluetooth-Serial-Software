/*
 * ESP32 BLE UART 测试固件（Nordic UART Service）
 * ---------------------------------------------------------------
 * 配合上位机 "BLE 串口助手" 使用：
 *   1. Arduino IDE 选择 ESP32 开发板，烧录本示例；
 *   2. 打开上位机 → 扫描 → 选择 "ESP32-BLE-UART" → 连接；
 *   3. 上位机发送任意文本，本机会原样 Notify 回显，并每秒主动发一行 tick。
 *
 * 服务/特征（与 Nordic UART Service 一致，上位机会自动识别）：
 *   服务   6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *   RX     6E400002-...  上位机 → ESP32（Write / Write Without Response）
 *   TX     6E400003-...  ESP32 → 上位机（Notify，必须带 0x2902 CCCD）
 *
 * 依赖：arduino-esp32 2.x / 3.x 自带的 BLE 库
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

static BLECharacteristic *txChar = nullptr;
static volatile bool deviceConnected = false;

/* 连接 / 断开回调 */
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override {
        (void)server;
        deviceConnected = true;
        Serial.println("[BLE] 上位机已连接");
    }

    void onDisconnect(BLEServer *server) override {
        deviceConnected = false;
        Serial.println("[BLE] 上位机已断开，重新广播");
        server->startAdvertising();
    }
};

/* 收到上位机写入的数据 */
class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *ch) override {
        uint8_t *data = ch->getData();
        size_t len = ch->getLength();

        if (!len) return;
        Serial.printf("[RX] %u 字节:", (unsigned)len);
        for (size_t i = 0; i < len; i++) Serial.printf(" %02X", data[i]);
        Serial.println();

        /* 原样回显，便于验证双向收发 */
        if (txChar) {
            txChar->setValue(data, len);
            txChar->notify();
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("BLE UART (NUS) demo");

    BLEDevice::init("ESP32-BLE-UART");

    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService *service = server->createService(SERVICE_UUID);

    /* TX：ESP32 -> 上位机，Notify，必须加 0x2902 描述符 */
    txChar = service->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    txChar->addDescriptor(new BLE2902());

    /* RX：上位机 -> ESP32，读写均可 */
    BLECharacteristic *rxChar = service->createCharacteristic(
        RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rxChar->setCallbacks(new RxCallbacks());

    service->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);   /* 让扫描响应里带名字，Windows 才能显示设备名 */
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("[BLE] 广播中，设备名 ESP32-BLE-UART");
}

void loop() {
    static uint32_t last = 0;

    if (millis() - last >= 1000) {
        last = millis();
        if (deviceConnected && txChar) {
            char buf[48];
            int n = snprintf(buf, sizeof(buf), "tick %lu\r\n",
                             (unsigned long)(millis() / 1000));
            txChar->setValue((uint8_t *)buf, (size_t)n);
            txChar->notify();
        }
    }
    delay(10);
}
