// 本机语音笔记录播器：内置 MEMS mic → 16kHz/mono/16-bit WAV → microSD；设备端浏览+回放。
// NORMAL 态按 'n' 起停录音；按 'l' 弹出笔记列表、enter 回放。分帧融入主 loop()，不阻塞
// clawd GIF / BLE。与 sound_player 的 Speaker 互斥（共用 I2S）：起录让出 I2S，停录归还。
// 录音与回放互斥（同一时刻只能录或播）。
// 初始化(SD 引脚/挂载、Mic.begin/record/end、WAV header)逐字抄 upstream:
//   .pio/libdeps/cardputer-adv/M5Cardputer/examples/Basic/mic_wav_record/mic_wav_record.ino
//   .pio/libdeps/cardputer-adv/M5Cardputer/examples/Basic/sdcard/sdcard.ino
#pragma once
#include <stdint.h>

#define REC_MAX_NOTES       32    // 列表最多 32 条（超出截断并 log）
#define REC_NOTE_NAME_LEN   32    // 文件名最长 buffer

namespace recorder {
void begin();               // 预留初始化钩子（M5Cardputer.begin() 之后调）

// ── 录音 ──
bool beginRecord();         // 挂载 SD + 起录；成功 true，SD 缺失/失败 false（不崩、归还 Speaker）
void endRecord();           // 停录：回填 WAV header + close + 归还 Speaker
void tick();                // 录音态每帧抓一小段写盘（call in loop() while isRecording）
bool isRecording();         // 当前是否在录音
uint32_t elapsedMs();       // 本次录音已录时长 ms（非录音态返回 0）

// ── 回放 ──
uint8_t listNotes(char names[][REC_NOTE_NAME_LEN], uint8_t maxN);  // 扫 SD 根 note_*.wav，返回条数
bool playNote(const char* name);     // 打开文件 seek(44) → 置 playing 态（录音中拒绝）
void tickPlayback();                 // 回放态每帧读一小块 → Speaker.playRaw 流式播
void stopPlayback();                 // 停回放、关文件、退 playing 态
bool isPlaying();                    // 当前是否在回放
uint32_t playbackElapsedMs();        // 本次回放已播时长 ms（非回放态返回 0）
bool deleteNote(const char* name);   // 删指定笔记文件（SD.remove）；失败 false
void adjVolume(int delta);           // 回放中调硬件音量（±25），同步 g_savedVol
bool saveTextNote(const char* text);  // 存文本笔记到 SD /txt_XXXX.txt；失败 false
}
