// 字段名与解析逐字对照 ../claude-code-buddy/src/data.h `_applyJson`；
// 决定回送格式对照 main.cpp:1428 `{"cmd":"permission","id":..,"decision":..}`。
#include "cclink.h"
#include "ble_link.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_mac.h>
#include <string.h>

// 品牌前缀：编译期开关,默认 Claude-。Cursor 变体只需 build_flags 覆盖
// -DBUDDY_BRAND_PREFIX='"Cursor-"'（同 StickC 的 -claude/-cursor 变体套路）。
#ifndef BUDDY_BRAND_PREFIX
#define BUDDY_BRAND_PREFIX "Claude-"
#endif

namespace {
BuddyState g_state;
bool g_changed = false;
char g_line[640];     // 一行状态 JSON（entries 最多 8×~91 + 头，留余量）
size_t g_li = 0;

// 设备名 = "Claude-" + BT MAC 末两字节（照搬 claude-code-buddy startBt）。
char g_name[16];

void applyJson(const char* line) {
    JsonDocument doc;
    if (deserializeJson(doc, line)) return;   // 非法 JSON 丢弃
    // 命令行（如 ack）不更新状态；只认带会话字段的状态帧。
    BuddyState& s = g_state;
    s.total     = doc["total"]     | s.total;
    s.running   = doc["running"]   | s.running;
    s.waiting   = doc["waiting"]   | s.waiting;
    s.completed = doc["completed"] | false;
    const char* m = doc["msg"];
    if (m) { strncpy(s.msg, m, sizeof(s.msg) - 1); s.msg[sizeof(s.msg) - 1] = 0; }

    JsonArray la = doc["entries"];
    if (!la.isNull()) {
        uint8_t n = 0;
        for (JsonVariant v : la) {
            if (n >= 8) break;
            const char* e = v.as<const char*>();
            strncpy(s.entries[n], e ? e : "", 91);
            s.entries[n][91] = 0;
            n++;
        }
        s.nEntries = n;
    }

    JsonObject pr = doc["prompt"];
    if (!pr.isNull()) {
        const char* pid = pr["id"]; const char* pt = pr["tool"]; const char* ph = pr["hint"];
        strncpy(s.promptId,   pid ? pid : "", sizeof(s.promptId) - 1);   s.promptId[sizeof(s.promptId) - 1] = 0;
        strncpy(s.promptTool, pt  ? pt  : "", sizeof(s.promptTool) - 1); s.promptTool[sizeof(s.promptTool) - 1] = 0;
        strncpy(s.promptHint, ph  ? ph  : "", sizeof(s.promptHint) - 1); s.promptHint[sizeof(s.promptHint) - 1] = 0;
    } else {
        s.promptId[0] = 0; s.promptTool[0] = 0; s.promptHint[0] = 0;
    }
    g_changed = true;
}
}  // namespace

namespace cclink {

void begin() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(g_name, sizeof(g_name), BUDDY_BRAND_PREFIX "%02X%02X", mac[4], mac[5]);
    bleInit(g_name);
}

void poll() {
    while (bleAvailable()) {
        int c = bleRead();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (g_li > 0) { g_line[g_li] = 0; if (g_line[0] == '{') applyJson(g_line); g_li = 0; }
        } else if (g_li < sizeof(g_line) - 1) {
            g_line[g_li++] = (char)c;
        } else {
            g_li = 0;   // 行超长，丢弃防溢出
        }
    }
}

const BuddyState& state() { return g_state; }

bool changed() { bool c = g_changed; g_changed = false; return c; }

bool connected() { return bleConnected(); }

void sendDecision(const char* id, const char* decision) {
    char cmd[96];
    int n = snprintf(cmd, sizeof(cmd),
                     "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}\n",
                     id, decision);
    if (n > 0) bleWrite((const uint8_t*)cmd, (size_t)n);
}

// 注入按键到 Mac 聚焦窗口（bridge core.py cmd=="key": ch→_type_unicode, key→kvk_for）。
// text 为本固件内置的固定 nudge 串（无引号/反斜杠），故不做 JSON 转义。
void sendKeyText(const char* text) {
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "{\"cmd\":\"key\",\"ch\":\"%s\"}\n", text);
    if (n > 0) bleWrite((const uint8_t*)buf, (size_t)n);
}
void sendKeyName(const char* name) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "{\"cmd\":\"key\",\"key\":\"%s\"}\n", name);
    if (n > 0) bleWrite((const uint8_t*)buf, (size_t)n);
}

}  // namespace cclink
