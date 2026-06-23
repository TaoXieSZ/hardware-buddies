// cc-bridge 推来的会话状态快照 + 到 AgentState 的派生。
// 字段与派生逻辑逐字对照 ../claude-code-buddy/src/data.h `_applyJson`
// 与 main.cpp 的状态派生（waiting→attention / completed→celebrate / running→busy）。
#pragma once
#include "agent_state.h"
#include <stdint.h>
#include <string.h>

// UTF-8 边界安全的截断复制：拷 src 到 dst（≤dstSize-1 字节），且绝不在多字节序列中间切断，
// 避免中文被按字节截断后留半个 code point → efontCN 渲染出乱码尾。dst 总以 \0 结尾。
inline void utf8lcpy(char* dst, const char* src, size_t dstSize) {
    if (!dstSize) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = 0, max = dstSize - 1;
    while (n < max && src[n]) ++n;
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) --n;  // 回退到 lead byte
    memcpy(dst, src, n);
    dst[n] = 0;
}

// payload sessions[] 的一条：可选中会话切换器用。
struct SessionInfo {
    char sid[40] = {0};   // Claude session_id（= cmux resume_binding.checkpoint_id），UUID 36 字符
    bool running = false; // 该会话是否在生成
    char label[40] = {0}; // cmux auto-name/prompt（可读名）；空 = 列表 fallback 到 sid 前缀
};

// AskUserQuestion 的一个选项（payload question.options[]）。
struct QuestionOption {
    char id[8]     = {0};  // "opt0".."optN"（稳定 id，回送用，省字节 + 避中文编码）
    char label[40] = {0};  // 选项可读文本
};

// 待应答的 AskUserQuestion 快照（来自 cmux feed，经 cc-bridge）。
struct QuestionState {
    char rid[100]  = {0};  // cmux feed requestId（~90 字符），回送 answerQuestion 用
    char header[24] = {0}; // 问题 header（短标签）
    char text[92]  = {0};  // 问题文本
    bool multi = false;    // multiSelect
    QuestionOption options[6];  // 上限 6（AskUserQuestion 常 2-4）
    uint8_t nOptions = 0;
};

struct BuddyState {
    int  total = 0, running = 0, waiting = 0;
    bool completed = false;
    char msg[64] = {0};            // 当前状态串，如 "running: Bash"
    char entries[8][92] = {{0}};   // 最近活动行（≤8，每行 ≤91）
    uint8_t nEntries = 0;
    // 审批 prompt：空 promptId = 无待审批
    char promptId[40] = {0};
    char promptTool[40] = {0};     // 工具名，如 "Bash"
    char promptHint[92] = {0};     // 参数/提示，如 "terraform apply"
    // per-session 列表（来自 payload sessions[]，供物理 session 切换器选中用）。
    // 上限 16 对齐 bridge to_payload 的封顶；sid 用于选中后回送 selectSession。
    SessionInfo sessions[16];
    uint8_t nSessions = 0;
    // 待应答的 AskUserQuestion（来自 cmux feed，经 cc-bridge）；hasQuestion=false 即无。
    QuestionState question;
    bool hasQuestion = false;
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
