#pragma once
#include <stddef.h>
#include <stdint.h>

// 语音助手音频链路：半双工 Mic/Speaker 调度 + PTT 采集 + PCM 流式播放。
//
// 半双工规则（ground truth: M5Unified examples/Basic/Microphone/Microphone.ino
// 第 38/97 行注释）：CoreS3 上 M5.Mic 与 M5.Speaker 不能同时启用，切换序列
// 必须是 Speaker.end()→Mic.begin() / 等 isRecording 清空→Mic.end()→Speaker.begin()。
// 本模块把切换点收敛到 audioMicStart()/audioSpkStart() 两处（design.md 决策 4）。
//
// 播放缓冲为什么不是 audio_ringbuf.h：DashScope 回复是突发下发（≈11s 音频
// 几秒内到齐），overwrite-oldest 的 32KB ring 会把还没播的语音冲掉。这里改用
// PSRAM 线性缓冲（VOICE_PLAY_CAP ≈30s@24k），write/read 双指针，满了拒收
// （宁可截尾不吃中段）。喂 M5.Speaker 的 2 槽 playRaw 队列模式照抄
// src/stackchan/audio_play.cpp（DMA 期间源指针必须存活 → 4 缓冲池）。

// 分配 PSRAM 缓冲。M5.begin() 之后调用一次。false = PSRAM 分配失败。
bool audioIoInit();

// --- 半双工切换（唯二的切换点） ---
bool audioMicStart();   // Speaker.end → Mic.begin；true = mic 已启用
void audioSpkStart();   // 等 mic 空闲 → Mic.end → Speaker.begin

// --- PTT 采集（audioMicStart 之后） ---
// 非阻塞：驱动 M5.Mic.record 把 16k mono s16le 追加进采集缓冲。
// 返回累计字节数；缓冲满（VOICE_REC_CAP）后不再增长。
size_t audioMicPump();
// 当前采集缓冲（松手后读取；下次 audioMicStart 清零）。
const uint8_t* audioMicData();
size_t audioMicBytes();

// --- 流式播放（audioSpkStart 之后） ---
void voicePlayStart(uint32_t sample_rate);            // 清缓冲、设采样率
size_t voicePlayFeed(const uint8_t* pcm, size_t len); // 追加 PCM；返回实收字节
void voicePlayPump();                                 // 每 tick 喂 2 槽队列
bool voicePlayActive();                               // 在播或仍有待播数据
