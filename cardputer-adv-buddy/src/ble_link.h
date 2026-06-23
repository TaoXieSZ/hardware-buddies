#pragma once
#include <stdint.h>
#include <stddef.h>

// 逐字复用自 ../claude-code-buddy/src/ble_bridge.h —— buddy 家族的 Nordic UART
// Service BLE 桥。cc-bridge(Claude Code 守护进程)扫 "Claude-XXXX" 前缀连上,经
// NUS/debug 通道收发行分隔 JSON。Cardputer 走开放(未加密)路径,见 ble_link.cpp。
//
// Service UUID  6e400001-b5a3-f393-e0a9-e50e24dcca9e
// RX char       6e400002-…   (client → device, WRITE)
// TX char       6e400003-…   (device → client, NOTIFY)
// Debug service b0c2dbe6-cc01-… (cc-bridge 实际连的未加密通道,RX cc02 / TX cc03)

void bleInit(const char* deviceName);
bool bleConnected();
bool bleSecure();
uint32_t blePasskey();
void bleClearBonds();
size_t bleAvailable();
int bleRead();
size_t bleWrite(const uint8_t* data, size_t len);
void bleWatchdogTick();   // 半开链路看门狗：连着却长时间无 RX → 强制断开重广播（见 .cpp）
