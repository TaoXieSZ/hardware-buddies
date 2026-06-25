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

## 3. 固件（可选 · 非核心）
- [ ] 3.1 `cclink` 解析 `sessions[].agent`；`SessionInfo` 加 agent 字段
- [ ] 3.2 `drawSessionTag` 标识前加 agent 标记（`▸`/`C·`）—— 默认延后

## 4. 验证
- [x] 4.1 单测：`test_ext_sessions_merge_into_payload` + `test_ext_sessions_stale_dropped`（pytest 190）
- [x] 4.2 真机（2026-06-25）：cmux 同时跑 Claude + Cursor → cardputer **会话列表三个 session，其中一个是 Cursor** ✅。聚合管线端到端打通
- [ ] 4.3 真机：Cursor 会话待输入 → 钉（同 FIFO 队列）—— 未单独验（同代码路径）

## 5. 跨层同步  ✅ 两 live 仓已同步
- [x] 5.1 同步：cc-bridge→`claude-desktop-buddy`（ext_sessions 合并，pytest 184）；cursor-bridge→`claude-desktop-buddy-cursor`（老 fork：自包含 back-port `set_session_state`+`_wait_seq` + per-session 6 分支 + pusher 防御式，语法+逻辑冒烟过）。两 daemon 已重载、真机验证
- [ ] 5.2 BLE 单 owner 迁移文档 + install（cursor-bridge 不再需要自己的 Cursor stick）—— 待补

## 状态小结
- 显示（轮播/列表）：真机通过 ✅。切 Cursor pane（2.3）：已实现 + 单测，待同步线上 + 真机验。
- 剩：5.2 单 owner 迁移文档；Codex（后续）。

## 后续（不在本轮）
- Codex 状态源（先查 Codex CLI 有无 hook；没有→cmux pane 活性给粗 busy/idle）
