// clawd 显示层（合成器）。四种模式合成到一块 240x135 sprite 后 push：
//   NORMAL   ：clawd GIF + 右上角会话计数角标
//   APPROVAL ：审批面板（工具+参数+按键提示），覆盖 GIF
//   SESSIONS ：只读会话列表
//   HELP     ：键位说明覆盖层
// 优先级 APPROVAL > SESSIONS > HELP > NORMAL。GIF 渲染逐字复用 buddy 家族 AnimatedGIF 思路。
#pragma once
#include "agent_state.h"
#include "link_state.h"   // BuddyState / SessionInfo（会话切换器用）
#include <stdint.h>

namespace clawd {
void begin();
bool ok();

// NORMAL 模式
void setState(AgentState s);              // 会话状态 → clawd GIF
void setBadge(int total, int running);    // 右上角 "T·R" 角标
void setBattery(int pct);                  // 顶栏电量 %（<0=unknown 不显示）。openspec cardputer-battery-indicator
// 多会话轮播：顶栏左显示当前会话标识 + [idx/total]；pinned=true 时底部钉态横幅。
// total<=0 或 tag 空 = 不显示（单聚合态）。openspec change cardputer-session-rotation。
void setSessionTag(const char* tag, int idx, int total, bool pinned);
void setToast(const char* text);          // 底部短暂提示(~1.5s,nudge 发送反馈)
void setSleeping(bool sleep);
void reactHeart();
void reactDizzy();
void reactError();                        // 工具出错临时动画(error-120.gif, ~2.5s)

// APPROVAL 覆盖层
void showApproval(const char* tool, const char* hint);
void hideApproval();
bool approvalVisible();

// SESSIONS 覆盖层（per-session 可选中列表 → 物理 session 切换器）
void showSessions(const BuddyState& bs);  // 用 bs.sessions[] 渲染可选中列表
void hideSessions();
void sessionsMove(int delta);             // 移动选中项（viewport 跟随）
const char* sessionsSelectedSid();        // 当前选中会话的 sid（""=无）
bool sessionsVisible();

// QUESTION 覆盖层（AskUserQuestion 应答器）
void showQuestion(const BuddyState& bs);  // 用 bs.question 渲染选项面板（快照 rid+options+multi）
void hideQuestion();
void questionMove(int delta);             // 移动光标（viewport 跟随）
void questionToggle();                    // multiSelect: toggle 当前项勾选；单选: no-op
void questionJumpTo(int idx);             // 数字键直跳到第 idx 项（0-based）
bool questionMulti();                     // 是否 multiSelect
const char* questionRid();                // 当前问题 rid（""=无）
uint8_t questionSelectedIds(const char** out, uint8_t maxN);  // 收集提交 id（单选=光标项；多选=勾选项）
bool questionVisible();

// HELP 覆盖层（键位说明，h 键开关）
void showHelp();
void hideHelp();
bool helpVisible();

void tick(uint32_t dtMs);                 // 合成当前模式 → push
}
