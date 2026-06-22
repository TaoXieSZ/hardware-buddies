# Tasks

> 注：本 change 为「事后正式化」——下列实现任务已在今天的会话中完成并真机验证、提交（hardware-buddies main：动画 `8111bee`、buffer 修复 `2e491e6`、deny `7fe347c`、HELP/会话 `6e1db6c`、hook wav + 音量 `5643490`；cc-bridge main `e16ef07`）。spec 在此补记已落地的现实。剩 5.4 待办。

## 1. 状态动画扩展（agent-state-animation）
- [x] 1.1 `agent_state.h` 加 `Notification` 枚举
- [x] 1.2 `link_state.h` `deriveAgentState` 加 thinking/notification 派生（thinking 必须在 running 之前判）
- [x] 1.3 `clawd_player` `fileForState` 加 notification 映射 + `reactError` reaction
- [x] 1.4 `main.cpp` error reaction 边沿触发（msg "failed" 新出现）
- [x] 1.5 GIF 资源 thinking/notification/error/dizzy 转 120px 宽 + 纯黑背景

## 2. hook 事件音效（audio-feedback）
- [x] 2.1 `sound_player::playEvent`：LittleFS 读 `/sounds/<name>.wav` → RAM → `playWav`
- [x] 2.2 `cclink` 读 bridge `play` 字段 → `playEvent`
- [x] 2.3 4 个关键事件 wav（shanraisshan 转 16kHz mono ~48KB）放 `data/sounds/`
- [x] 2.4 清理与 wav 重复的 hook tone（approval/done/tool/stop_fail），保留本地 tone

## 3. 音量控制（audio-feedback）
- [x] 3.1 `sound_player` `volumeUp`/`volumeDown` + `volume()`
- [x] 3.2 `main.cpp` `-`/`=` 音量键 + 屏底 toast
- [x] 3.3 HELP 覆盖层显示 `-/=vol`

## 4. 关联修复（cardputer-claude-buddy 真机验证盲区）
- [x] 4.1 `cclink` 接收 buffer 640→2048（实时审批面板不弹的真根因）
- [x] 4.2 deny 键 quick-tap 漏检修复（去掉 `&& isPressed()`）
- [x] 4.3 会话列表过滤 subagent 条目 + 标题用真实 total

## 5. 验证与收尾
- [x] 5.1 真机：thinking / dizzy / error 动画（含 dizzy 背景纯黑修正）
- [x] 5.2 真机：posttoolusefailure wav 播放 + 音量键
- [x] 5.3 真机：审批闭环 space/n/a 决定回送（cc-bridge `e16ef07` 失败清审批配合）
- [x] 5.4 将 `cardputer-coding-pet` 标记 superseded 并归档（`--skip-specs`，归档为 `2026-06-22-cardputer-coding-pet`，未污染 baseline）
