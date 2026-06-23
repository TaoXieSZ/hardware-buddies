// 字段名与解析逐字对照 ../claude-code-buddy/src/data.h `_applyJson`；
// 决定回送格式对照 main.cpp:1428 `{"cmd":"permission","id":..,"decision":..}`。
#include "cclink.h"
#include "ble_link.h"
#include "sound_player.h"
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
// 一行状态 JSON 缓冲。daemon 的 heartbeat 含 entries[8]×~91(=728) + sessions[8]
// + tokens/model/limits 等，多 session 时轻松 >1KB。旧值 640 会把整帧丢弃
// (poll 里 OVERFLOW 分支)，导致 prompt 永远收不到、审批面板不弹。2048 留足余量。
char g_line[2048];
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
    if (m) utf8lcpy(s.msg, m, sizeof(s.msg));

    JsonArray la = doc["entries"];
    if (!la.isNull()) {
        uint8_t n = 0;
        for (JsonVariant v : la) {
            if (n >= 8) break;
            const char* e = v.as<const char*>();
            if (!e || strncmp(e, "subagent", 8) == 0) continue;  // 过滤子 agent 事件
            utf8lcpy(s.entries[n], e, sizeof(s.entries[n]));
            n++;
        }
        s.nEntries = n;
    }

    JsonObject pr = doc["prompt"];
    if (!pr.isNull()) {
        const char* pid = pr["id"]; const char* pt = pr["tool"]; const char* ph = pr["hint"];
        strncpy(s.promptId,   pid ? pid : "", sizeof(s.promptId) - 1);   s.promptId[sizeof(s.promptId) - 1] = 0;
        strncpy(s.promptTool, pt  ? pt  : "", sizeof(s.promptTool) - 1); s.promptTool[sizeof(s.promptTool) - 1] = 0;
        utf8lcpy(s.promptHint, ph  ? ph  : "", sizeof(s.promptHint));
    } else {
        s.promptId[0] = 0; s.promptTool[0] = 0; s.promptHint[0] = 0;
    }

    // per-session 列表（payload sessions[]，每条 {sid, running}）。供可选中切换器用：
    // sid = Claude session_id = cmux checkpoint_id，选中后原样回送 selectSession。
    // bridge 在无会话时省略该字段（to_payload 仅 _sessions 非空才输出），故 null = 清零。
    JsonArray sa = doc["sessions"];
    if (!sa.isNull()) {
        uint8_t n = 0;
        for (JsonVariant v : sa) {
            if (n >= 16) break;
            const char* sid = v["sid"];
            if (!sid) continue;
            strncpy(s.sessions[n].sid, sid, sizeof(s.sessions[n].sid) - 1);
            s.sessions[n].sid[sizeof(s.sessions[n].sid) - 1] = 0;
            s.sessions[n].running = v["running"] | false;
            const char* lbl = v["label"];   // cmux auto-name；可缺省
            if (lbl) {
                utf8lcpy(s.sessions[n].label, lbl, sizeof(s.sessions[n].label));
            } else {
                s.sessions[n].label[0] = 0;
            }
            n++;
        }
        s.nSessions = n;
    } else {
        s.nSessions = 0;
    }

    // 待应答的 AskUserQuestion（payload question；来自 cmux feed，经 cc-bridge）。
    // {rid, header, text, multi, options:[{id,label}]}。无 question 时 bridge 省略 → 清空。
    JsonObject q = doc["question"];
    if (!q.isNull() && q["rid"]) {
        const char* rid = q["rid"];
        strncpy(s.question.rid, rid, sizeof(s.question.rid) - 1);
        s.question.rid[sizeof(s.question.rid) - 1] = 0;
        const char* h = q["header"]; const char* t = q["text"];
        utf8lcpy(s.question.header, h ? h : "", sizeof(s.question.header));
        utf8lcpy(s.question.text, t ? t : "", sizeof(s.question.text));
        s.question.multi = q["multi"] | false;
        uint8_t n = 0;
        JsonArray qopts = q["options"];
        for (JsonVariant ov : qopts) {
            if (n >= 6) break;
            const char* oid = ov["id"];
            if (!oid) continue;
            strncpy(s.question.options[n].id, oid, sizeof(s.question.options[n].id) - 1);
            s.question.options[n].id[sizeof(s.question.options[n].id) - 1] = 0;
            const char* olb = ov["label"];
            utf8lcpy(s.question.options[n].label, olb ? olb : "", sizeof(s.question.options[n].label));
            n++;
        }
        s.question.nOptions = n;
        s.hasQuestion = (n > 0);
    } else {
        s.hasQuestion = false;
        s.question.rid[0] = 0;
    }

    // bridge 的 play 字段(one-shot 事件名小写)→ 播放 /sounds/<name>.wav。
    // 只有关键事件放了 wav 文件，其余事件 playEvent 找不到文件自动忽略。
    const char* pl = doc["play"];
    if (pl && pl[0]) sound::playEvent(pl);

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
            if (g_li > 0) {
                g_line[g_li] = 0;
                if (g_line[0] == '{') applyJson(g_line);
                g_li = 0;
            }
        } else if (g_li < sizeof(g_line) - 1) {
            g_line[g_li++] = (char)c;
        } else {
            Serial.printf("[cclink] OVERFLOW drop at %u bytes\n", (unsigned)g_li);
            g_li = 0;   // 行超长，丢弃防溢出
        }
    }
    bleWatchdogTick();   // 半开链路看门狗（详见 ble_link.cpp）
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

// AskUserQuestion 应答 → cc-bridge 经 cmux feed.question.reply 回灌。
// ids = 选中 option 的稳定 id 数组（bridge 侧 id→label 再填 selections）。
// rid/id 由 cmux 生成（无引号/反斜杠），不做 JSON 转义。
void sendAnswerQuestion(const char* rid, const char* const* ids, uint8_t nIds) {
    if (!rid || !rid[0]) return;
    char cmd[256];
    int p = snprintf(cmd, sizeof(cmd),
                     "{\"cmd\":\"answerQuestion\",\"rid\":\"%s\",\"ids\":[", rid);
    for (uint8_t i = 0; i < nIds && p > 0 && p < (int)sizeof(cmd) - 12; i++) {
        p += snprintf(cmd + p, sizeof(cmd) - p, "%s\"%s\"", i ? "," : "", ids[i]);
    }
    if (p > 0 && p < (int)sizeof(cmd) - 4) {
        p += snprintf(cmd + p, sizeof(cmd) - p, "]}\n");
        bleWrite((const uint8_t*)cmd, (size_t)p);
    }
}

// 选中会话 → 回送给 bridge，由其调 cmux 把对应 pane 切到前台。
// sid 是 payload sessions[] 里的值（UUID，无引号/反斜杠），不做 JSON 转义。
// 格式对照 REFERENCE.md「Session switch」+ bridge core.py on_stick_line cmd=="selectSession"。
void sendSelectSession(const char* sid) {
    if (!sid || !sid[0]) return;
    char cmd[80];
    int n = snprintf(cmd, sizeof(cmd),
                     "{\"cmd\":\"selectSession\",\"sid\":\"%s\"}\n", sid);
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
