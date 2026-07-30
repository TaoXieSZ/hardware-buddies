// 小咪的多网络凭据表（openspec change xiaomi-wifi-provisioning）。
//
// 为什么要有它：WiFi 凭据原本编译期烧进固件，换个地方就得接电脑重烧
// （2026-07-19 一天为此重烧三次）。存进 NVS 并支持多组后，家里/公司各存一次，
// 带着走就自动连。
//
// 本头文件是纯逻辑（Arduino-free），好让 native 测试环境直接验证槽位语义；
// NVS 持久化在 wifi_store.cpp。同 panel_state.h / audio_ringbuf.h 的做法。

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace wifi_store {

// 5 组够覆盖家/公司/常去的两三个地方；再多会拖慢 WiFiMulti 的扫描连接。
constexpr size_t MAX_NETWORKS = 5;
constexpr size_t SSID_CAP = 33;   // 802.11 SSID 最长 32 字节 + NUL
constexpr size_t PASS_CAP = 64;   // WPA2 PSK 最长 63 字符 + NUL

struct Entry {
    char ssid[SSID_CAP] = {0};
    char pass[PASS_CAP] = {0};
    bool used = false;
};

enum class PutResult : uint8_t {
    Added,      // 新增了一个槽位
    Updated,    // 同名 SSID 覆盖（改密码走这条）
    Full,       // 槽位已满 —— 明确报错，不静默淘汰用户存过的网络
    Invalid,    // SSID 为空或超长
};

class Store {
   public:
    size_t count() const {
        size_t n = 0;
        for (const auto& e : e_) if (e.used) ++n;
        return n;
    }

    // 按存储顺序取第 i 个"已用"槽位；越界返回 nullptr。
    const Entry* at(size_t i) const {
        size_t n = 0;
        for (const auto& e : e_) {
            if (!e.used) continue;
            if (n++ == i) return &e;
        }
        return nullptr;
    }

    bool has(const char* ssid) const { return find(ssid) >= 0; }

    // 添加或更新。同名 SSID 覆盖而非追加 —— 否则改一次密码就吃掉一个槽位，
    // 而且 WiFiMulti 会拿到两条冲突记录。
    PutResult put(const char* ssid, const char* pass) {
        if (!validSsid(ssid)) return PutResult::Invalid;
        if (!validPass(pass)) return PutResult::Invalid;
        if (!pass) pass = "";

        int idx = find(ssid);
        if (idx >= 0) {
            copyInto(e_[idx].pass, pass, PASS_CAP);
            return PutResult::Updated;
        }
        for (auto& e : e_) {
            if (e.used) continue;
            copyInto(e.ssid, ssid, SSID_CAP);
            copyInto(e.pass, pass, PASS_CAP);
            e.used = true;
            return PutResult::Added;
        }
        return PutResult::Full;
    }

    bool remove(const char* ssid) {
        int idx = find(ssid);
        if (idx < 0) return false;
        e_[idx] = Entry();
        return true;
    }

    void clear() { for (auto& e : e_) e = Entry(); }

    // 出厂种子只灌一次的标记：用户删掉种子网络后不该在下次启动被复活。
    bool seeded() const { return seeded_; }
    void markSeeded() { seeded_ = true; }
    void setSeeded(bool v) { seeded_ = v; }

    static bool validSsid(const char* s) {
        if (!s || !*s) return false;
        return strlen(s) < SSID_CAP;
    }
    static bool validPass(const char* p) {
        if (!p) return true;               // 空密码 = 开放网络，合法
        return strlen(p) < PASS_CAP;
    }

   private:
    int find(const char* ssid) const {
        if (!ssid || !*ssid) return -1;
        for (size_t i = 0; i < MAX_NETWORKS; ++i) {
            if (e_[i].used && strcmp(e_[i].ssid, ssid) == 0) return (int)i;
        }
        return -1;
    }
    static void copyInto(char* dst, const char* src, size_t cap) {
        strncpy(dst, src ? src : "", cap - 1);
        dst[cap - 1] = 0;
    }

    Entry e_[MAX_NETWORKS];
    bool  seeded_ = false;
};

// --- 设备侧 API（实现在 wifi_store.cpp；native 测试不链接这部分） ---
// 签名刻意只用纯 C 类型，保持本头文件在 native 环境可编译。

// 从 NVS 载入凭据表。首次启动且表为空时，把编译期种子（出厂凭据）灌入槽位 0
// 并打上 seeded 标记 —— 用户之后删掉它不会被复活。
void deviceLoad(const char* seed_ssid, const char* seed_pass);

// 取全局表（面板与连接逻辑共用）。
Store& device();

// 把当前表写回 NVS。任何增删改后必须调用。
void devicePersist();

// 连接进度回调：把"正在找网络 / 正在连 X / 失败"透出去给屏幕字幕。
typedef void (*ProgressFn)(const char* human_readable);

// 用 WiFiMulti 扫描现场并连接信号最强的已知网络。
// timeout_ms 建议 15000（要先扫描再关联）。成功返回 true。
// 表为空时直接返回 false（调用方据此进入配网模式）。
bool deviceConnect(uint32_t timeout_ms, ProgressFn progress);

// 当前已连上的 SSID；未连接返回空串。
const char* deviceCurrentSsid();

}  // namespace wifi_store
