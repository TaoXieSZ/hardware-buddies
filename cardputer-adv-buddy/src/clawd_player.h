// clawd 显示层（合成器）。六种模式合成到一块 240x135 sprite 后 push：
//   NORMAL   ：clawd GIF + 右上角会话计数角标
//   APPROVAL ：审批面板（工具+参数+按键提示），覆盖 GIF
//   QUESTION ：问答面板（问题+选项），覆盖 GIF
//   SESSIONS ：只读会话列表
//   NOTES    ：笔记浏览+回放（列表可选中）
//   NOTEBOOK ：键盘笔记本（打字+存 SD）
//   HELP     ：键位说明覆盖层
// 优先级 APPROVAL > QUESTION > SESSIONS > NOTES > NOTEBOOK > HELP > NORMAL。GIF 渲染逐字复用 buddy 家族 AnimatedGIF 思路。
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
// 持久 REC 指示：本机录音期间顶栏左显 "REC mm:ss"（红），非录音态清除。
// 与 toast 不同（那是 1.5s 自动消失），录音是持续态。openspec change cardputer-voice-notes。
void setRecording(bool on, uint32_t elapsedMs);
void setSleeping(bool sleep);
void setOnline(bool on);                   // BLE 是否连上 cc-bridge；!on 时顶栏常驻 Connecting...
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

// NOTES 覆盖层（笔记浏览+回放，l 键开关）openspec cardputer-note-playback
void showNotes(const char (*names)[32], uint8_t n, int8_t playingIdx);
void hideNotes();
void notesMove(int delta);
const char* notesSelected();
bool notesVisible();

// NOTEBOOK 覆盖层（键盘笔记本，k 键开关）
void showNotebook();
void hideNotebook(bool save);       // save=true→存 SD
void notebookKey(char c);           // 插入字符
void notebookBackspace();           // 删光标前字符
void notebookNewline();             // 换行
const char* notebookText();         // 取当前文本缓冲（存盘用）
bool notebookVisible();

void tick(uint32_t dtMs);                 // 合成当前模式 → push
}
