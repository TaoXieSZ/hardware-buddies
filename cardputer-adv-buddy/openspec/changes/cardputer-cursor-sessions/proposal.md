## Why

cardputer 现在只监听 **Claude** 会话（cc-bridge 的 Claude-* BLE 链路）。但用户常在 cmux 里同时跑 **Cursor CLI**（甚至 Codex）。一块 cardputer 应当能同时反映 Claude **和** Cursor 的多会话状态——轮播/钉/列表跨 agent。

好消息（spike 已摸）：
- **线协议 + 固件已经 agent-无关**（`sessions[]{sid,label,st,ws}` 不关心背后是什么 agent，由 change `cardputer-session-rotation` 奠定）——固件基本不用改。
- **Cursor 有 hook**（`cursor-bridge/cursor_hook.js` + `cursor_hook_permission.js`），cursor-bridge 的 `apply_event` 与 cc-bridge 同构——Cursor 能拿**丰富状态**（thinking/tool/waiting），不必退化成粗状态。

所以真正要解决的是**聚合**：一个 BLE 设备只能被一个 central 占用，现在 cc-bridge / cursor-bridge 是两个平行 daemon、两个 BLE 前缀，各推各的。要让一块 cardputer 看到两边，得有一个**单一 BLE owner 的聚合层**。

详见同目录 `architecture-notes.md`（完整架构图 + 复杂度逐项）。

## What Changes

- **Cursor 侧 per-session 状态**：把 `cardputer-session-rotation` 给 cc-bridge 加的同款 `set_session_state(sid, st)` 落到 `cursor-bridge/bridge.py` 的 `apply_event` 各分支（Cursor hooks → 丰富状态 + FIFO waiting seq）。
- **聚合层（核心）**：让一个 BLE owner 的 payload 同时包含 Claude 与 Cursor 的会话。`sessions[]` 每条按需带 `agent`（`claude`/`cursor`）以便固件区分/标注；按 **cmux 会话（surface）** 归一，避免两 agent 的 sid 命名空间冲突。
- **（可选 · 固件小改）**：会话标识前加 agent 标记（如 `▸`/`C·`），让用户一眼区分这条是 Claude 还是 Cursor 会话。核心不依赖它。

## Non-goals

- **不做 Codex**（本轮明确推迟；它是否有 hook 仍是未知，单列后续）。
- **不重构固件渲染**：轮播/钉/列表照用 `cardputer-session-rotation` 的 per-session 机制。
- **不改 cmux**：只消费 cmux 已暴露的会话登记。

## Capabilities

### New Capabilities
- `cursor-session-monitoring`：cardputer 经聚合层同时反映 Claude 与 Cursor 的多会话状态（轮播/钉/列表跨 agent），Cursor per-session 状态来自其 hook。

### 依赖
- 叠在 `cardputer-session-rotation`（per-session `st`/`ws` 线协议 + 固件轮播/钉）之上——那个 change 应先 archive。

## ⚠️ Gating spike（开工前必须验）

聚合的可行性押在两点：
1. **会话归一**：cmux 能否给 Cursor pane 一个稳定的 surface/binding（类似 Claude 的 `resume_binding.checkpoint_id`），让聚合层把 Cursor 会话也按 cmux 会话归一、并支持 `selectSession` 聚焦其 pane？
2. **单 BLE owner 的形态**：聚合走哪条——(a) 新 aggregator daemon 同时听 Claude+Cursor 两个 hook socket、独占 BLE；(b) cc-bridge 读 cursor-bridge 状态合流；(c) 两 hook 路由进同一 socket。先验哪条最小可行。
