// 音效播放器：M5Unified Speaker 非阻塞音符序列。
// 每个 Claude Code hook 事件对应一段短旋律，在主 loop() 的 tick() 中推进。
#pragma once

namespace sound {
void begin();               // 初始化音量（M5Cardputer.begin() 之后调）
void tick();                // 每帧推进序列，call in loop()
void play(const char* name); // "approval" "done" "tool" "stop_fail"
                             // "connect" "disconnect" "nudge"
bool busy();                // 当前是否在播放
void playEvent(const char* name);  // 播放 /sounds/<name>.wav（hook 事件，文件不存在则忽略）
void volumeUp();                   // 音量 +25（= 键）
void volumeDown();                 // 音量 -25（- 键）
int  volume();                     // 当前音量 0-255
}
