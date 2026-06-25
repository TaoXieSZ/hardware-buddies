# Tasks

> 状态：proposal 草拟（探索产物），**未实现**。Codex 不在本轮（推迟）。

## 0. Gating spike
- [x] 0.1 cmux Cursor pane：`resume_binding=null`（无 checkpoint），但有稳定 surface `id`/`ref` + title 嵌 `cursor-xxx` → **按 surface UUID 归一、按 surface id 聚焦**可行（见 design D2）。cmux feed 只有 claude events → Cursor 状态须自 cursor-bridge hook。
- [x] 0.3 cursor_hook.js ✅ 带 `session_id`(=conversation_id) 且把 Cursor 事件翻译成 Claude-shaped events（复用 apply_event）→ 给 cursor-bridge 加 per-session 状态 = 套 cc-bridge 同款，几乎零额外逻辑
- [x] 0.2b ID join ✅ **免费**：cursor hook `session_id` == cmux title `cursor-<UUID>`（同一 UUID 去前缀，实测 66099139/889f542f 在 cursor-bridge 日志各 35/122 次）。状态↔聚焦可干净 join，精确 selectSession 可行（早前「硬问题」判断作废，小样本误判）
- [ ] 0.2 聚合形态选型：(a) 新 aggregator / (b) cc-bridge 合流（cursor-bridge 不连自己 BLE，把 sessions[] 喂给 cc-bridge）/ (c) 两 hook 同 socket。倾向 (b) 极简：单 BLE owner，绕开双 bridge 抢链路
- [x] 0.4 Cursor hooks ✅ 实时 fire 丰富事件：实测发「继续」→ 日志 22:39 `approve: shell` + 22:40 `PostToolUse session=e121d286-ed97-4209-ae5d-bff47b95163c`（完整 UUID）。前置盲区清除，spike 全绿

## 1. Cursor per-session 状态（cursor-bridge）
- [ ] 1.1 把 `set_session_state(sid, st)` 贴到 `cursor-bridge/bridge.py` 的 apply_event 各分支（对齐 cc-bridge：thinking/tool/waiting/idle）
- [ ] 1.2 `BuddyState.set_session_state` 共享自 buddy_core（已存在），无需重复
- [ ] 1.3 单测：Cursor 事件 → 桶 st 正确 + waiting FIFO seq

## 2. 聚合层（按 0.2 结论落地）
- [ ] 2.1 单 BLE owner 同时承载 Claude + Cursor 会话（MVP 倾向 (c)：cursor_hook 路由进同一 socket，apply_event 按 agent 分流）
- [ ] 2.2 sessions[] 按 cmux 会话归一；每条带 `agent`（claude/cursor）
- [ ] 2.3 selectSession 对 Cursor 会话能聚焦正确 pane（依赖 0.1）

## 3. 固件（可选 · 非核心）
- [ ] 3.1 `cclink` 解析 `sessions[].agent`；`SessionInfo` 加 agent 字段
- [ ] 3.2 `drawSessionTag` 标识前加 agent 标记（`▸`/`C·`）—— 默认延后

## 4. 验证
- [ ] 4.1 单测：聚合 payload 同时含 Claude + Cursor 会话，各自 st/ws 正确；聚合字段不变
- [ ] 4.2 真机：cmux 同时跑 Claude + Cursor → cardputer 轮播/列表两边会话都出现、状态正确
- [ ] 4.3 真机：Cursor 会话待输入 → 正确钉（与 Claude 会话同一 FIFO 队列）

## 5. 跨层同步
- [ ] 5.1 cursor-bridge 改动同步线上 `claude-desktop-buddy`，跑测
- [ ] 5.2 BLE 单 owner 迁移：现有 cc/cursor 双 bridge 部署怎么过渡（文档 + install）

## 后续（不在本轮）
- Codex 状态源（先查 Codex CLI 有无 hook；没有→cmux pane 活性给粗 busy/idle）
