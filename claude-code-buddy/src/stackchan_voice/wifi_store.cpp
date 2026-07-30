#include "wifi_store.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiMulti.h>

namespace wifi_store {
namespace {

// 独立命名空间，不与 settings.cpp 的 "stackbuddy" 混用 —— 键不会撞，
// 清空 WiFi 配置也不会误伤音量/人设等设置。
constexpr const char* NS = "xiaomiwifi";

Preferences g_nvs;
Store       g_store;
char        g_current[SSID_CAP] = {0};
bool        g_loaded = false;

void keyFor(char* out, size_t cap, size_t slot, char which) {
    snprintf(out, cap, "n%u%c", (unsigned)slot, which);   // n0s / n0p …
}

}  // namespace

void deviceLoad(const char* seed_ssid, const char* seed_pass) {
    if (g_loaded) return;
    g_loaded = true;

    if (!g_nvs.begin(NS, /*readOnly=*/false)) {
        Serial.println("[wifi] NVS 打开失败，本次仅用编译期种子");
        if (Store::validSsid(seed_ssid)) g_store.put(seed_ssid, seed_pass);
        return;
    }

    char ks[8], kp[8];
    for (size_t i = 0; i < MAX_NETWORKS; ++i) {
        keyFor(ks, sizeof(ks), i, 's');
        keyFor(kp, sizeof(kp), i, 'p');
        String ssid = g_nvs.getString(ks, "");
        if (!ssid.length()) continue;
        g_store.put(ssid.c_str(), g_nvs.getString(kp, "").c_str());
    }

    // 出厂种子：确保编译期网络始终在表里（占位符除外）。刷了带真实凭据的固件
    // 就一定连得上；即便用户误删，重启也会加回来当"回家保底"。改网重烧同样即时生效
    // （新 SSID 不在表里 → 加入）。占位符 REPLACE_ME_* 不灌，免得塞进垃圾网络。
    bool seedValid = Store::validSsid(seed_ssid) &&
                     strncmp(seed_ssid, "REPLACE_ME", 10) != 0;
    if (seedValid && !g_store.has(seed_ssid)) {
        if (g_store.put(seed_ssid, seed_pass) != PutResult::Full) {
            Serial.printf("[wifi] 确保出厂网络在列表：'%s'\n", seed_ssid);
        }
        devicePersist();
    }
    Serial.printf("[wifi] 已存 %u 个网络\n", (unsigned)g_store.count());
}

Store& device() { return g_store; }

void devicePersist() {
    // 关键：不要在这里重复 g_nvs.begin() —— 句柄在 deviceLoad 已打开，Preferences
    // 对已打开句柄再 begin 会返回 false，导致本函数被从 deviceLoad 内部调用时
    // 直接 return、一个字节都不写（凭据只活在内存里，重启即失）。这个 bug 让
    // 多网络存储从第一版起就没真正落盘过（2026-07-22 查出）。
    if (!g_loaded) return;   // 保险：deviceLoad 必先跑（setup 中），句柄才是开的
    char ks[8], kp[8];
    for (size_t i = 0; i < MAX_NETWORKS; ++i) {
        keyFor(ks, sizeof(ks), i, 's');
        keyFor(kp, sizeof(kp), i, 'p');
        const Entry* e = g_store.at(i);
        if (e) {
            g_nvs.putString(ks, e->ssid);
            g_nvs.putString(kp, e->pass);
        } else {
            g_nvs.remove(ks);
            g_nvs.remove(kp);
        }
    }
    // 种子名（wseedname）由 deviceLoad 管理，这里不碰。
}

bool deviceConnect(uint32_t timeout_ms, ProgressFn progress) {
    size_t n = g_store.count();
    g_current[0] = 0;
    if (n == 0) {
        if (progress) progress("还没存过任何网络");
        return false;
    }

    // WiFiMulti 会先扫描现场，再连信号最强的已知网络 —— 家里和公司的网都存着
    // 时不用自己判断该连哪个。每次重建实例：用户可能刚在面板增删过网络。
    WiFiMulti multi;
    for (size_t i = 0; i < n; ++i) {
        const Entry* e = g_store.at(i);
        if (e) multi.addAP(e->ssid, e->pass);
    }

    if (progress) progress("找网络中……");
    WiFi.mode(WIFI_STA);

    uint32_t deadline = millis() + timeout_ms;
    while (millis() < deadline) {
        if (multi.run() == WL_CONNECTED) {
            strncpy(g_current, WiFi.SSID().c_str(), SSID_CAP - 1);
            g_current[SSID_CAP - 1] = 0;
            Serial.printf("[wifi] 已连上 '%s'  ip=%s rssi=%d\n", g_current,
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            if (progress) {
                char msg[80];
                snprintf(msg, sizeof(msg), "连上 %s 啦", g_current);
                progress(msg);
            }
            return true;
        }
        delay(100);
    }

    Serial.printf("[wifi] %u 个已知网络都连不上（status=%d）\n",
                  (unsigned)n, (int)WiFi.status());
    if (progress) progress("附近没有认识的网络");
    return false;
}

const char* deviceCurrentSsid() { return g_current; }

}  // namespace wifi_store
