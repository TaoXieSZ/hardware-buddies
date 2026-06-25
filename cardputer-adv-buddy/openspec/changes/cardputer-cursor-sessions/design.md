# Design — cardputer-cursor-sessions

> 完整架构图 + 复杂度逐项见同目录 `architecture-notes.md`。本文件只记 Cursor-specific 决策与 spike。

## 关键洞察（来自架构分析）

- **「有哪些会话」= cmux 免费给，跨 agent**；**「每个会话在干嘛」= 各 agent 自己的 hook**。
- 线协议/固件已 agent-无关（`sessions[]{sid,label,st,ws}`）→ **固件基本不动**。
- 工作全在 daemon：把单 agent 演进成「按 cmux 会话聚合多 agent 状态、单 BLE owner」。

## Cursor 利好

- Cursor **有 hook**（`cursor-bridge/cursor_hook.js`），`cursor-bridge/bridge.py` 的 `apply_event` 与 cc-bridge **同构**（SessionStart/UserPromptSubmit/PreToolUse/Stop/PermissionRequest…）。
- 所以给 Cursor 加 per-session 状态 = 把 `cardputer-session-rotation` 给 cc-bridge 加的 `set_session_state` **同款贴到 cursor-bridge 的 apply_event**。`BuddyState.set_session_state` 已在 `buddy_core/core.py`（共享）——cursor-bridge 直接可用。

## 决策（draft 默认，review 可翻）

### D1. 聚合形态：候选三选一（spike 定）
- **(a) 新 aggregator daemon**：同时听 Claude + Cursor 两个 hook socket，维护一个 `BuddyState`（sessions[] 跨 agent），独占 BLE。最干净、最贴「单 owner」，但是新进程 + 收敛两 bridge。
- **(b) cc-bridge 合流 cursor 状态**：cc-bridge 额外读 cursor 的事件/状态，合进自己的 payload。改动集中在 cc-bridge，但职责变重。
- **(c) 两 hook 路由进同一 socket**：cursor_hook.js 也投到 cc-bridge 的 socket，apply_event 按来源/agent 分流。最小代码，但 agent 区分要在事件里带标记。
- **倾向 (c) 作 MVP**（最小改动先打通），(a) 作长期形态。spike 后定。

### D2. 会话归一（避免 sid 冲突 + 支持聚焦）
- Claude sid = `checkpoint_id`；Cursor sid = Cursor session_id。两者都是 UUID，碰撞概率极低，但**聚焦**（selectSession→cmux 聚焦对应 pane）需要把 Cursor 会话也映射到 cmux surface。
- spike：cmux 的 Cursor pane 有没有类似 `resume_binding.checkpoint_id` 的稳定绑定？有 → 按 surface 归一、selectSession 通吃；没有 → 退而用 title/nickname 软匹配（弱，列为风险）。

### D3. agent 标注
- `sessions[]` 每条按需带 `agent`（`claude`/`cursor`）。固件**可选**在标识前加标记（`▸cursor` / `C·`），核心功能（轮播/钉）不依赖。
- 默认：daemon 发 `agent` 字段；固件先不渲染标记（MVP），后续加。

### D4. 固件
- **不改核心**。`sessions[]` 多出 Cursor 条目照样轮播/钉/列表。
- 可选：解析 `agent` 字段 + 标识前缀（小改 `SessionInfo` + `drawSessionTag`）。

## 跨层同步
- Cursor per-session 改动同步 monorepo + 线上 `claude-desktop-buddy` 的 `cursor-bridge/bridge.py`（见 [[cc-bridge-daemon-source-of-truth]]）。
- 聚合层落地后，注意 BLE 单 owner 与现有 cc/cursor 双 bridge 部署的迁移。

## 风险
- **单 central 约束**：BLE 设备一次只能一个 central；聚合必须收敛到一个 owner，否则两 bridge 抢链路。
- **Cursor 会话→surface 绑定**若 cmux 不给，selectSession 对 Cursor 会话会不准（D2 风险）。
- payload 体积：多 agent → sessions[] 更长，复核固件行缓冲（cardputer 2048 够；claude-code-buddy 1024 紧）。
