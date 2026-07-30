// 控制面板的跨任务数据结构 —— 纯逻辑、Arduino-free，好让 native 测试环境
// 直接编译验证（同 audio_ringbuf.h / frame_framing.h 的做法）。
//
// 两个方向，都是单向的，避免 HTTP 任务和主循环互相踩：
//   主循环 --发布--> Snapshot --只读--> HTTP 任务      （看状态）
//   HTTP 任务 --暂存--> Pending --取走并应用--> 主循环   （改设置）
//
// 为什么改设置不让 HTTP 任务直接做（design 决策 4）：M5Unified 的外设 API 与
// NVS 都不是为并发设计的。HTTP 任务只负责解析+夹取，主循环下一 tick 取走应用。

#pragma once

#include <stdint.h>
#include <string.h>

namespace panel {

// --- 取值夹取（与 settings.cpp 的范围一致；面板提交非法值时夹到边界而非拒绝） ---
inline uint16_t clampIdleSec(long v)    { return v < 30 ? 30 : (v > 3600 ? 3600 : (uint16_t)v); }
inline uint8_t  clampTurnLimit(long v)  { return v < 1  ? 1  : (v > 100  ? 100  : (uint8_t)v); }
// 音量下限 96：再低语音回放就听不清了（真机实测，见语音固件 setup 的夹逼）。
inline uint8_t  clampVolume(long v)     { return v < 96 ? 96 : (v > 255  ? 255  : (uint8_t)v); }
inline uint8_t  clampBrightness(long v) { return v < 20 ? 20 : (v > 255  ? 255  : (uint8_t)v); }
inline uint8_t  clampTilt(long v)       { return v < 0  ? 0  : (v > 90   ? 90   : (uint8_t)v); }

constexpr size_t VOICE_CAP   = 40;
constexpr size_t PERSONA_CAP = 512;
constexpr size_t SUBTITLE_CAP = 384;

// --- HTTP 任务 → 主循环：待应用的设置 ---
// 部分更新语义：只有 has_* 为真的字段才会被应用。take() 取走并清空，
// 保证同一批改动只被应用一次。
struct Pending {
    bool any = false;

    bool has_volume = false;      uint8_t  volume = 0;
    bool has_brightness = false;  uint8_t  brightness = 0;
    bool has_idle_sec = false;    uint16_t idle_sec = 0;
    bool has_turn_limit = false;  uint8_t  turn_limit = 0;
    bool has_motion = false;      bool     motion = false;
    bool has_idle_wiggle = false; bool     idle_wiggle = false;
    bool has_tilt = false;        uint8_t  tilt = 0;
    bool has_dance = false;       bool     dance = false;
    bool has_voice = false;       char     voice[VOICE_CAP] = {0};
    bool has_persona = false;     char     persona[PERSONA_CAP] = {0};
    // 一次性动作（"dance" / "disconnect"）。同样由主循环执行 —— 舵机与断连
    // 都不能从 HTTP 任务碰。
    bool has_action = false;      char     action[16] = {0};

    void setVolume(long v)      { volume = clampVolume(v);         has_volume = any = true; }
    void setBrightness(long v)  { brightness = clampBrightness(v); has_brightness = any = true; }
    void setIdleSec(long v)     { idle_sec = clampIdleSec(v);      has_idle_sec = any = true; }
    void setTurnLimit(long v)   { turn_limit = clampTurnLimit(v);  has_turn_limit = any = true; }
    void setMotion(bool v)      { motion = v;                      has_motion = any = true; }
    void setIdleWiggle(bool v)  { idle_wiggle = v;                 has_idle_wiggle = any = true; }
    void setTilt(long v)        { tilt = clampTilt(v);             has_tilt = any = true; }
    void setDance(bool v)       { dance = v;                       has_dance = any = true; }

    void setVoice(const char* s) {
        if (!s) s = "";
        strncpy(voice, s, VOICE_CAP - 1);
        voice[VOICE_CAP - 1] = 0;
        has_voice = any = true;
    }
    void setPersona(const char* s) {
        if (!s) s = "";
        strncpy(persona, s, PERSONA_CAP - 1);
        persona[PERSONA_CAP - 1] = 0;
        has_persona = any = true;
    }
    void setAction(const char* s) {
        if (!s) s = "";
        strncpy(action, s, sizeof(action) - 1);
        action[sizeof(action) - 1] = 0;
        has_action = any = true;
    }

    // 取走并清空。返回 false 表示没有待应用的改动（主循环可直接跳过）。
    bool take(Pending* out) {
        if (!any) return false;
        *out = *this;
        *this = Pending();
        return true;
    }
};

// --- 主循环 → HTTP 任务：只读状态快照 ---
// 主循环整体覆盖写，HTTP 任务整体读取，避免读到半更新的状态。
struct Snapshot {
    uint8_t  state = 0;             // 与 main.cpp 的 State 枚举一致
    bool     wifi_up = false;
    bool     ds_connected = false;
    uint8_t  turns = 0;             // 当前连接内已完成轮数
    uint8_t  turn_limit = 20;
    uint16_t last_latency_ms = 0;   // 上轮松手 → 首声
    uint16_t last_tokens = 0;       // 上轮 usage total
    uint16_t underruns = 0;         // 上轮播放卡顿次数
    uint16_t dry_ms = 0;            // 上轮干涸总时长
    int8_t   battery_pct = -1;      // -1 = 未知
    uint32_t idle_remain_sec = 0;   // 距空闲断开还有多久（0 = 不适用）
    char     subtitle[SUBTITLE_CAP] = {0};   // 当前屏幕字幕
    char     last_user_text[SUBTITLE_CAP] = {0};   // 最近一轮：用户说了什么（ASR）
    char     last_reply_text[SUBTITLE_CAP] = {0};  // 最近一轮：小咪答了什么

    void setSubtitle(const char* s)  { copyInto(subtitle, s); }
    void setUserText(const char* s)  { copyInto(last_user_text, s); }
    void setReplyText(const char* s) { copyInto(last_reply_text, s); }

   private:
    static void copyInto(char* dst, const char* s) {
        if (!s) s = "";
        strncpy(dst, s, SUBTITLE_CAP - 1);
        dst[SUBTITLE_CAP - 1] = 0;
    }
};

}  // namespace panel
