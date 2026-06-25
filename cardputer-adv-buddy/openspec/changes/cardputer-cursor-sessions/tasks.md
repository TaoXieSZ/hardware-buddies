# Tasks

> 状态：proposal 草拟（探索产物），**未实现**。Codex 不在本轮（推迟）。

## 0. Gating spike
- [x] 0.1 cmux Cursor pane：`resume_binding=null`（无 checkpoint），但有稳定 surface `id`/`ref` + title 嵌 `cursor-xxx` → **按 surface UUID 归一、按 surface id 聚焦**可行（见 design D2）。cmux feed 只有 claude events → Cursor 状态须自 cursor-bridge hook。
- [x] 0.3 cursor_hook.js ✅ 带 `session_id`(=conversation_id) 且把 Cursor 事件翻译成 Claude-shaped events（复用 apply_event）→ 给 cursor-bridge 加 per-session 状态 = 套 cc-bridge 同款，几乎零额外逻辑
- [x] 0.2b ID join ✅ **免费**：cursor hook `session_id` == cmux title `cursor-<UUID>`（同一 UUID 去前缀，实测 66099139/889f542f 在 cursor-bridge 日志各 35/122 次）。状态↔聚焦可干净 join，精确 selectSession 可行（早前「硬问题」判断作废，小样本误判）
- [ ] 0.2 聚合形态选型：(a) 新 aggregator / (b) cc-bridge 合流（cursor-bridge 不连自己 BLE，把 sessions[] 喂给 cc-bridge）/ (c) 两 hook 同 socket。倾向 (b) 极简：单 BLE owner，绕开双 bridge 抢链路
- [x] 0.4 Cursor hooks ✅ 实时 fire 丰富事件：实测发「继续」→ 日志 22:39 `approve: shell` + 22:40 `PostToolUse session=e121d286-ed97-4209-ae5d-bff47b95163c`（完整 UUID）。前置盲区清除，spike 全绿

## 1. Cursor per-session 状态（cursor-bridge）  ✅ monorepo + pytest 9
- [x] 1.1 `set_session_state` 贴到 cursor-bridge apply_event 6 分支（SessionStart=idle / UserPromptSubmit=thinking / Pre+PostToolUse=tool / Stop=idle / PermissionRequest=waiting）
- [x] 1.2 复用 buddy_core 共享 `set_session_state`（setdefault 不冲掉 cursor 的 last_seen；reaper pop 整桶 st/ws 随之消失）
- [x] 1.3 单测：`test_per_session_state_transitions`（thinking→tool→waiting+FIFO→idle）+ `test_per_session_state_in_payload`（多 session payload st）。⚠️ 仅 monorepo；线上 `claude-desktop-buddy-cursor` 同步见 task 5.1

## 2. 聚合层（方案 b-refined）  ✅ monorepo + pytest 190
- [x] 2.1 cursor-bridge `push_ext_sessions_loop`（extra_task，每 2s）→ 写 `{action:"ext_sessions",agent:"cursor",sessions:[...]}` 到 cc-bridge socket（best-effort，cc-bridge 没起就跳过）。`_build_cursor_sessions` 从 _sessions+labels 直建，避开 to_payload 副作用
- [x] 2.2 cc-bridge `handle_client` 收 `ext_sessions` action → `state.ext_sessions[agent]`；`to_payload` 合并（本机=claude 不带 agent，ext 标自己 agent；EXT_STALE_SEC=30s 丢幽灵；全表 16 上限）。测试：`test_ext_sessions_merge_into_payload` + `test_ext_sessions_stale_dropped`
- [x] 2.3 selectSession 对 Cursor pane 聚焦：`cmux_control.focus_by_cursor_sid(sid)`（按 title 里 cursor-<UUID>/前缀匹配非 Claude surface → focus surface id）；`_select_session` 回退链 `focus_by_checkpoint or focus_by_cursor_sid`。测试：`test_focus_by_cursor_sid_matches_title_and_focuses` + no-match。pytest 192。⚠️ 仅 monorepo，未同步线上 cc-bridge、未真机
- ⚠️ 三仓同步见 task 5.1（cc-bridge→claude-desktop-buddy / cursor-bridge→claude-desktop-buddy-cursor）

## 3. 固件 agent 标记  ✅ 真机验证（2026-06-25）
- [x] 3.1 `SessionInfo` 加 `char agent[8]`；`cclink` 解析 `sessions[].agent`（空=claude）
- [x] 3.2 `drawSessions` 会话列表每行 agent 标记：claude=黄+"cc"，cursor=灰蓝+"cu"（颜色+文字双重区分）
- [x] 3.3 修复 `showSessions` 漏拷 agent 字段（曾导致全显 cc）。真机验证：Cursor 行灰蓝 cu / Claude 行黄 cc，用户确认「可以了」✅
- [ ] 3.4 轮播顶栏 tag 的 agent 标记（按需扩，未做）

## 7. 多问题 AskUserQuestion（真机暴露，待设计）
- 现象：一次 AskUserQuestion 含多个问题时，设备只弹/答第一个就退，其余不弹。
- 根因：daemon `parse_pending_questions` MVP 只取 `questions[0]`，多问题不拆。
- 方案待定：A 顺序弹（答完弹下一个）/ B 多问题整体放给终端。先写 spec 批准再做（daemon 侧，不用烧固件）。

## 4. 验证
- [x] 4.1 单测：`test_ext_sessions_merge_into_payload` + `test_ext_sessions_stale_dropped`（pytest 190）
- [x] 4.2 真机（2026-06-25）：cmux 同时跑 Claude + Cursor → cardputer **会话列表三个 session，其中一个是 Cursor** ✅。聚合管线端到端打通
- [ ] 4.3 真机：Cursor 会话待输入 → 钉（同 FIFO 队列）—— 未单独验（同代码路径）

## 5. 跨层同步  ✅ 两 live 仓已同步
- [x] 5.1 同步：cc-bridge→`claude-desktop-buddy`（ext_sessions 合并，pytest 184）；cursor-bridge→`claude-desktop-buddy-cursor`（老 fork：自包含 back-port `set_session_state`+`_wait_seq` + per-session 6 分支 + pusher 防御式，语法+逻辑冒烟过）。两 daemon 已重载、真机验证
- [ ] 5.2 BLE 单 owner 迁移文档 + install（cursor-bridge 不再需要自己的 Cursor stick）—— 待补

## 状态小结
- 显示（轮播/列表）：真机通过 ✅。切 Cursor pane 管道全通（设备发 selectSession → daemon focus_by_cursor_sid），真机验证 selectSession 到达 + focus 尝试。
- 剩：5.2 单 owner 迁移文档；Codex（后续）。

## ⚠️ 真机暴露的真问题：列表来源 = hook 历史 ≠ cmux 活 pane（2026-06-25）
现象：设备列出 12b56ff4/646a554a/e121d286（cursor-bridge 按 hook 历史追踪，pane 已关/不在本 cmux），而当前活的 cmux Cursor pane `cursor-66099139` 没进列表 → 点哪个都 `no matching cmux surface`，且无 label（显 sid 前缀）。
- 根因：cursor-bridge 会话列表来自 hook 历史（凡触发过 hook 都记），cmux 聚焦却要求是活 pane。两集合发散。
- 修法（task 6）：给 cursor-bridge 加 **cmux 对账**——像 cc-bridge 的 cmux_label_loop 那样，会话列表来自 cmux 活 Cursor surface（title `cursor-<UUID>` → sid + label），device 列表 == 可聚焦集合，顺带拿到 label。老 cursor fork 当前无此环。

## 6. cursor-bridge cmux 对账（真机暴露）  ✅ monorepo + pytest 195
- [x] 6.1 `cmux_control.cursor_session_labels()`：列活 Cursor surface（title `cursor-<UUID>` 正则提 sid → label）。测试 `test_cursor_session_labels_lists_live_cursor_panes`
- [x] 6.2 cursor-bridge `cmux_cursor_label_loop`（15s 轮询）填 `state.session_labels`={cmux_sid:label}；`_build_cursor_sessions` 改成**以 cmux 活 pane 为准**、按 UUID 首段 join hook st/ws，僵尸会话排除；cmux 不可用回退 hook 列表。测试 `test_build_cursor_sessions_uses_live_cmux_panes` + fallback
- [x] 6.3 真机（2026-06-25）：cursor-bridge cmux 对账 loop 跑通（日志 `cmux cursor labels refreshed: 1 pane(s)`）→ 设备 Cursor 列表只剩活 cmux pane + label，**enter 能切到 Cursor pane**。用户确认「现在可以了」✅
- [x] 用户约束「只看 cmux 上的 Cursor session」：`_build_cursor_sessions` 只列 cmux 活 pane，无 cmux pane 不列、cmux 不可达则空（去掉 hook 兜底）
- [x] 同步：cursor-bridge→`claude-desktop-buddy-cursor`（自包含 `_cmux_cursor_panes`，无 control_plane 依赖，实测真 cmux 拿到活 pane）；cc-bridge focus_by_cursor_sid→`claude-desktop-buddy`

## ✅ 全功能真机通关（2026-06-25）
一块 cardputer 同时显示 Claude + Cursor 会话；Cursor 列表只剩 cmux 活 pane（带 label）；enter 可切到 Cursor pane。剩 5.2 单 owner 迁移文档、Codex（后续）。

## 后续（不在本轮）
- Codex 状态源（先查 Codex CLI 有无 hook；没有→cmux pane 活性给粗 busy/idle）
