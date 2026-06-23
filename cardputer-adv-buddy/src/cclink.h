// cc-bridge 链路：BLE 广播 Claude-XXXX、解析推来的状态 JSON、回送审批决定。
#pragma once
#include "link_state.h"

namespace cclink {
void begin();                                    // 广播 Claude-<MAC末2字节>
void poll();                                     // 排空 BLE → 解析行进 state
const BuddyState& state();
bool changed();                                  // 自上次以来 state 变过（取走即清）
bool connected();
void sendDecision(const char* id, const char* decision);  // "once"/"deny"/"always"
void sendSelectSession(const char* sid);  // 选中会话 → bridge 切对应 cmux pane 到前台
void sendAnswerQuestion(const char* rid, const char* const* ids, uint8_t nIds);  // AskUserQuestion 应答 → bridge feed.question.reply
void sendMic(bool down);              // PTT hold-to-talk {"cmd":"mic","state":"down"/"up"}
void sendKeyText(const char* text);   // {"cmd":"key","ch":"<text>"} → 打进聚焦窗口
void sendKeyName(const char* name);   // {"cmd":"key","key":"<name>"} enter/escape/...
}
