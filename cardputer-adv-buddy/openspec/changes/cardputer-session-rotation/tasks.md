# Tasks

> 状态：proposal 草拟（探索产物），**未实现**。任务 0（spike）已完成；下列为三层实现 + 验证。

## 0. Spike（已完成，结论：可行）
- [x] 0.1 问答归属 — `parse_pending_questions` 每条带 `sid=workstream_id`（cmux_control.py:372），现成 ✅
- [x] 0.2 审批归属 — hook 有 `session_id`（hook_permission.py:95），未下发；daemon 设聚合 waiting（core.py:1225），plumb 可得 🟡
- [x] 0.3 per-session 状态 — 每个 hook 事件带 session_id（core.py:1186），collapse 成聚合，按桶存可得 🟡
- [x] 0.4 结论：无 blocker，三层 plumbing

## 1. daemon 按 session 分桶（cc-bridge ×2 仓）  ✅ monorepo 实现 + pytest 186
- [x] 1.1 `_sessions[sid]` 扩到含 `st`/`ws`；`BuddyState.set_session_state(sid, st)`；apply_event 各分支按 session_id 设状态（SessionStart=idle / UserPromptSubmit=thinking / PreToolUse·PostToolUse=tool / Stop=idle / PermissionRequest·Notification(waiting-for-input)=waiting / PreCompact=thinking）
- [x] 1.2 每桶 `st ∈ {idle,thinking,tool,waiting}`（core 派生顺序由各事件分支落点保证）
- [x] 1.3 `ws`：`_wait_seq` 单调递增，进入 waiting 时分配一次、离开清零（FIFO）
- [x] 1.4 聚合字段保留；`to_payload` 两个 sess 分支每条加 `st`/`ws`（缺省不发）
- 测试：`test_per_session_state_in_payload` + `test_waiting_assigns_fifo_seq`（core 层）。⚠️ 仅 monorepo；线上仓同步见 task 7.1

## 2. 审批 session_id plumb  ✅ monorepo + pytest 186
- [x] 2.1 `hook_permission.py`：`req` 加完整 `session_id`（非 [:8] rid 前缀）
- [x] 2.2 `_handle_wait_permission`：读 `req.session_id` → 进等待时 `set_session_state(sid,"waiting")`（含 FIFO seq）、finally 清回 idle

## 3. wire / firmware 数据  ✅ 编译过
- [x] 3.1 `SessionInfo` 加 `state`(int=AgentState) + `waitSeq`；`agentStateFromWire` 映射；`cclink` 解析 `sessions[].st/.ws`
- [x] 3.2 兼容：缺 `st` → Idle、缺 `ws` → 0（老 daemon / 兄弟无碍）

## 4. firmware 轮播控制器  ✅ 编译过（main.cpp + clawd_player）
- [x] 4.1 main.cpp 轮播控制器（每帧，timer 驱动）→ `setState(sessions[cur].state)`（仅目标态变化时调，避免 GIF 重载）+ `clawd::setSessionTag`；clawd_player `drawSessionTag` 顶栏左 `label [i/N]`（label 缺→sid）
- [x] 4.2 成员=全部 session；轮播位置 [i/N] 顶栏显示
- [x] 4.3 稀释旋钮：idle dwell 1000ms / active 3000ms
- 无 session → 回退 `deriveAgentState`（聚合）+ 清 tag

## 5. firmware FIFO 钉输入  ✅ 编译过（与 task 4 同一控制器）
- [x] 5.1 每帧扫 `waitSeq>0` 求最小（最早等待）
- [x] 5.2 有等待 → 不轮播，钉最小 seq 者，`setState`=其 state（waiting→notification 动画）+ 底部橙横幅 `input: <label>`
- [x] 5.3 每帧重算 → 处理完（waitSeq 清零）自动钉次小；无等待恢复轮播
- [x] 5.4 覆盖层避让：`setState` 在 mode≠NORMAL 时只存 baseState_、不 applyTarget；`drawSessionTag` 仅 NORMAL tick 渲染

## 6. 验证
- [ ] 6.1 单测（daemon）：多 session 事件 → 各桶 state 正确；waiting seq FIFO
- [ ] 6.2 单测：to_payload 的 sessions[] 带 st/ws；聚合字段不变（兄弟兼容）
- [ ] 6.3 真机：A thinking / B tool / C idle → 主形象轮播 + 显示对应 label
- [ ] 6.4 真机：A、B 先后等输入 → 钉 A（先），A 答完自动钉 B（FIFO）
- [ ] 6.5 真机：payload 体积复核（多 session 下不超 cardputer 行缓冲 2048）

## 7. 跨层同步
- [ ] 7.1 daemon 改动同步线上 `claude-desktop-buddy`（core.py + bridge + hook_permission.py），live pytest
- [ ] 7.2 复核 claude-code-buddy 行缓冲 1024 在多 session payload 下的体积风险
