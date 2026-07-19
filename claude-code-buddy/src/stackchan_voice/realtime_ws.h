#pragma once
#include <stddef.h>
#include <stdint.h>

// DashScope Realtime WebSocket 客户端（Manual/PTT 模式）。
// 协议常量全部来自 tools/voice-prototype/ 2026-07-18 真连实测
// （design.md "实测定案"）：经典 URL、Bearer 鉴权、必须显式关 server_vad、
// delta 文本帧 ~25KB（库上限已由 tools/patch_websockets_max.py 抬到 48KB）。
//
// 事件解析策略（design.md 决策 6）：大帧不进 ArduinoJson——先 strstr 嗅探
// type，audio 字段原地定位后 mbedtls base64 解码直入播放缓冲；小事件同样
// 走字符串定位（本模块只关心 6 种事件，全 JSON 解析不值得）。

struct RtCallbacks {
  void (*onSessionReady)();                            // session.updated 已确认
  void (*onAudio)(const uint8_t* pcm24k, size_t len);  // 解码后的回复音频
  void (*onTranscript)(const char* utf8, size_t len);  // 回复文本增量（字幕用）
  void (*onDone)();                                    // response.done（usage 已打串口）
  void (*onError)(const char* msg);                    // 服务端 error / WS 断开
};

// 建立 WSS 连接（WiFi 须已连上）。key 为 DashScope API key。
bool rtBegin(const RtCallbacks& cb, const char* api_key);
void rtLoop();            // 每个 loop tick 调用（驱动 WS 收发）
void rtDisconnect();      // 主动断开（懒连接空闲超时用）
bool rtSessionReady();    // 可以开始一轮对话
// 上行：录音期间流式 append（内部 base64 封包）；松手后 commit+response.create。
void rtAppendAudio(const uint8_t* pcm16k, size_t len);
void rtCommitAndRespond();
