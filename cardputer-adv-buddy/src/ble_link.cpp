// 逐字复用自 ../claude-code-buddy/src/ble_bridge.cpp（buddy 家族成熟实现）。
// 唯一改动：顶部 #define BUDDY_BOARD_STICKS3 —— Cardputer(ESP32-S3) 复用 StickS3
// 的「开放 BLE」路径：所有 characteristic 不加密、不配对,cc-bridge 连未加密的
// debug service 即可,免掉 macOS bleak ↔ ESP32 passkey 配对的不稳定。其余 verbatim。
#define BUDDY_BOARD_STICKS3 1

#include "ble_link.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <string.h>

#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// Debug service for cc-bridge (Claude Code daemon) — same line-JSON
// protocol but UNENCRYPTED. The debug service skips pairing entirely.
#define DBG_SERVICE_UUID "b0c2dbe6-cc01-4000-8000-00805f9b34fb"
#define DBG_RX_UUID      "b0c2dbe6-cc02-4000-8000-00805f9b34fb"
#define DBG_TX_UUID      "b0c2dbe6-cc03-4000-8000-00805f9b34fb"

static const size_t RX_CAP = 2048;
static uint8_t  rxBuf[RX_CAP];
static volatile size_t rxHead = 0;
static volatile size_t rxTail = 0;

static BLEServer*         server = nullptr;
static BLECharacteristic* txChar = nullptr;
static BLECharacteristic* rxChar = nullptr;
static BLECharacteristic* dtxChar = nullptr;
static BLECharacteristic* drxChar = nullptr;
static volatile bool      connected = false;
static volatile bool      secure = false;
static volatile uint32_t  passkey = 0;
static volatile uint16_t  mtu = 23;

static uint32_t lastRxMs = 0;   // 最近一次收到 RX 的时刻（半开 watchdog 用）
static void rxPush(const uint8_t* p, size_t n) {
  lastRxMs = millis();
  for (size_t i = 0; i < n; i++) {
    size_t next = (rxHead + 1) % RX_CAP;
    if (next == rxTail) return;  // full — drop (upstream should keep up)
    rxBuf[rxHead] = p[i];
    rxHead = next;
  }
}

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    std::string v = c->getValue();
    if (!v.empty()) rxPush((const uint8_t*)v.data(), v.size());
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    connected = true;
    lastRxMs = millis();   // 重置：给 daemon 时间发首个 keepalive，避免 watchdog 误触
    Serial.println("[ble] connected");
  }
  void onDisconnect(BLEServer* s) override {
    connected = false;
    secure = false;
    passkey = 0;
    mtu = 23;
    Serial.println("[ble] disconnected");
    BLEDevice::startAdvertising();
  }
  void onMtuChanged(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
    mtu = param->mtu.mtu;
    Serial.printf("[ble] mtu=%u\n", mtu);
  }
};

class SecCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  bool onConfirmPIN(uint32_t) override { return false; }
  bool onSecurityRequest() override {
#ifdef BUDDY_BOARD_STICKS3
    return false;   // S3/Cardputer: refuse bonding — daemon uses open debug svc
#else
    return true;
#endif
  }
  void onPassKeyNotify(uint32_t pk) override {
    passkey = pk;
    Serial.printf("[ble] passkey %06lu\n", (unsigned long)pk);
  }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
    passkey = 0;
    secure = cmpl.success;
    Serial.printf("[ble] auth %s\n", cmpl.success ? "ok" : "FAIL");
    if (!cmpl.success && server) server->disconnect(server->getConnId());
  }
};

void bleInit(const char* deviceName) {
  BLEDevice::init(deviceName);
  BLEDevice::setMTU(517);

#ifndef BUDDY_BOARD_STICKS3
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
#endif
  BLEDevice::setSecurityCallbacks(new SecCallbacks());

  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* svc = server->createService(NUS_SERVICE_UUID);

  txChar = svc->createCharacteristic(
    NUS_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
#ifdef BUDDY_BOARD_STICKS3
  // Open NUS too: NO characteristic forces pairing, so an unbonded device is
  // connectable by the daemon without the flaky passkey dance.
  txChar->setAccessPermissions(ESP_GATT_PERM_READ);
  BLE2902* cccd = new BLE2902();
  cccd->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
#else
  txChar->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
  BLE2902* cccd = new BLE2902();
  cccd->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
#endif
  txChar->addDescriptor(cccd);

  rxChar = svc->createCharacteristic(
    NUS_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
#ifdef BUDDY_BOARD_STICKS3
  rxChar->setAccessPermissions(ESP_GATT_PERM_WRITE);
#else
  rxChar->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
#endif
  rxChar->setCallbacks(new RxCallbacks());

  svc->start();

  // Debug service — same line-JSON protocol but no encryption. Used by
  // tools/cc-bridge daemon. Open permissions so bleak never triggers pairing.
  BLEService* dsvc = server->createService(DBG_SERVICE_UUID);
  dtxChar = dsvc->createCharacteristic(
    DBG_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  dtxChar->setAccessPermissions(ESP_GATT_PERM_READ);
  BLE2902* dcccd = new BLE2902();
  dcccd->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
  dtxChar->addDescriptor(dcccd);
  drxChar = dsvc->createCharacteristic(
    DBG_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  drxChar->setAccessPermissions(ESP_GATT_PERM_WRITE);
  drxChar->setCallbacks(new RxCallbacks());   // same callback feeds shared rxBuf
  dsvc->start();

#ifndef BUDDY_BOARD_STICKS3
  BLESecurity* sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  sec->setCapability(ESP_IO_CAP_OUT);
  sec->setKeySize(16);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
#endif

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  adv->addServiceUUID(DBG_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.printf("[ble] advertising as '%s'\n", deviceName);
}

bool bleConnected() { return connected; }
bool bleSecure()    { return secure; }

// 半开链路 watchdog：macOS/CoreBluetooth 偶尔悄悄断开 GATT，但 ESP 收不到 disconnect 事件
// → connected 仍=1、不重新广播 → daemon 再也连不上（实测串口 conn=1 全程，daemon 却 not
// connected）。daemon 每 10s 必发一次 keepalive，故「连着却 >30s 收不到任何 RX」即判定半开：
// 主动 disconnect → onDisconnect 重启广播，让 daemon 在下一轮扫描重连。每帧由 cclink::poll 调。
void bleWatchdogTick() {
  if (connected && server && (millis() - lastRxMs > 30000UL)) {
    Serial.printf("[ble] half-open watchdog: %lus no RX -> disconnect+re-adv\n",
                  (unsigned long)((millis() - lastRxMs) / 1000));
    lastRxMs = millis();   // 防本帧后反复触发，等真正的 onDisconnect 完成
    server->disconnect(server->getConnId());
  }
}
uint32_t blePasskey() { return passkey; }

void bleClearBonds() {
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) return;
  esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(n * sizeof(esp_ble_bond_dev_t));
  if (!list) return;
  esp_ble_get_bond_device_list(&n, list);
  for (int i = 0; i < n; i++) esp_ble_remove_bond_device(list[i].bd_addr);
  free(list);
  Serial.printf("[ble] cleared %d bond(s)\n", n);
}

size_t bleAvailable() {
  return (rxHead + RX_CAP - rxTail) % RX_CAP;
}

int bleRead() {
  if (rxHead == rxTail) return -1;
  int b = rxBuf[rxTail];
  rxTail = (rxTail + 1) % RX_CAP;
  return b;
}

size_t bleWrite(const uint8_t* data, size_t len) {
  if (!connected) return 0;
  size_t chunk = mtu > 3 ? mtu - 3 : 20;
  if (chunk > 180) chunk = 180;
  size_t sent = 0;
  while (sent < len) {
    size_t n = len - sent;
    if (n > chunk) n = chunk;
    // Mirror to both NUS tx and debug tx so whichever central is connected
    // (Claude Desktop on NUS, cc-bridge on debug) sees the same stream.
    if (txChar)  { txChar->setValue((uint8_t*)(data + sent), n);  txChar->notify(); }
    if (dtxChar) { dtxChar->setValue((uint8_t*)(data + sent), n); dtxChar->notify(); }
    sent += n;
    delay(4);
  }
  return sent;
}
