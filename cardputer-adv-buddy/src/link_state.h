// cc-bridge 推来的会话状态快照 + 到 AgentState 的派生。
// 字段与派生逻辑逐字对照 ../claude-code-buddy/src/data.h `_applyJson`
// 与 main.cpp 的状态派生（waiting→attention / completed→celebrate / running→busy）。
#pragma once
#include "agent_state.h"
#include <stdint.h>
#include <string.h>

struct BuddyState {
    int  total = 0, running = 0, waiting = 0;
    bool completed = false;
    char msg[64] = {0};            // 当前状态串，如 "running: Bash"
    char entries[8][92] = {{0}};   // 会话列表行（≤8，每行 ≤91）
    uint8_t nEntries = 0;
    // 审批 prompt：空 promptId = 无待审批
    char promptId[40] = {0};
    char promptTool[40] = {0};     // 工具名，如 "Bash"
    char promptHint[92] = {0};     // 参数/提示，如 "terraform apply"
};

// 会话状态 → clawd 用的 AgentState（对齐 claude-code-buddy 派生顺序）。
// 注意：审批(promptId)由 UI 层单独处理为覆盖层，不在此派生（此处只管主形象）。
inline AgentState deriveAgentState(const BuddyState& s) {
    if (s.waiting > 0)    return AgentState::Approval;   // → attention.gif
    if (s.completed)      return AgentState::Done;       // → celebrate.gif
    // thinking 必须在 running 之前判：bridge 在 UserPromptSubmit 时同时设 running=1 + msg="thinking…"，
    // 若先判 running 会落到 ToolUse，thinking 永远命不中。
    if (strstr(s.msg, "thinking"))               return AgentState::Thinking;      // → clawd-thinking.gif
    if (s.running >= 1)   return AgentState::ToolUse;    // → busy.gif（msg "running: <tool>"）
    if (strstr(s.msg, "waiting for your input")) return AgentState::Notification;  // → clawd-notification.gif
    return AgentState::Idle;                             // → idle.gif
}
