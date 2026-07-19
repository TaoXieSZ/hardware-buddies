#pragma once
#include <stdint.h>

// 小咪的脸：黑白美短猫头，纯 M5GFX 矢量绘制（无 GIF/LittleFS 依赖）。
// 造型 1:1 移植自 UX demo 的 SVG（黑白双色、右眼淡灰眼罩斑、粉腮红、
// ω 嘴、豆豆眼双高光），布局对齐 character_chan 的 voice mode：
// 横屏 320×240，脸区 0..194，底部 46px 中文字幕滚动带（efontCN_24）。
//
// 状态复用 character_chan.h 的 CharState 枚举（motion.cpp 同一套映射）：
//   CHAR_SLEEP=眯眼打盹  CHAR_IDLE=常态+眨眼  CHAR_ATTENTION=竖耳大眼(倾听)
//   CHAR_BUSY=眼睛上瞟(思考)  其余=常态。说话嘴型由 setTalking 单独驱动。

void catFaceInit();                     // M5.begin 之后调用；画初始打盹脸
void catFaceSetState(uint8_t char_state);
void catFaceSetTalking(bool on);        // true=张嘴动画（随 tick 抖动）
void catFaceSetSubtitle(const char* text);
void catFaceTick();                     // 每 loop：眨眼/嘴型/字幕滚动
