// clawd 显示层（合成器）。四种模式合成到一块 240x135 sprite 后 push：
//   NORMAL   ：clawd GIF + 右上角会话计数角标
//   APPROVAL ：审批面板（工具+参数+按键提示），覆盖 GIF
//   SESSIONS ：只读会话列表
//   HELP     ：键位说明覆盖层
// 优先级 APPROVAL > SESSIONS > HELP > NORMAL。GIF 渲染逐字复用 buddy 家族 AnimatedGIF 思路。
#pragma once
#include "agent_state.h"
#include <stdint.h>

namespace clawd {
void begin();
bool ok();

// NORMAL 模式
void setState(AgentState s);              // 会话状态 → clawd GIF
void setBadge(int total, int running);    // 右上角 "T·R" 角标
void setToast(const char* text);          // 底部短暂提示(~1.5s,nudge 发送反馈)
void setSleeping(bool sleep);
void reactHeart();
void reactDizzy();
void reactError();                        // 工具出错临时动画(error-120.gif, ~2.5s)

// APPROVAL 覆盖层
void showApproval(const char* tool, const char* hint);
void hideApproval();
bool approvalVisible();

// SESSIONS 覆盖层（只读）
void showSessions(const char lines[][92], uint8_t n, int total);
void hideSessions();
void sessionsScroll(int delta);
bool sessionsVisible();

// HELP 覆盖层（键位说明，h 键开关）
void showHelp();
void hideHelp();
bool helpVisible();

void tick(uint32_t dtMs);                 // 合成当前模式 → push
}
