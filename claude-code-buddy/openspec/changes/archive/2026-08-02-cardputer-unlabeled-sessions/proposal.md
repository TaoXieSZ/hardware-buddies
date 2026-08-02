# cardputer-unlabeled-sessions

## Why

cc-bridge 心跳里的 `sessions[]` 在 `session_labels` 非空时**只**从 cmux 标签快照构建，而 cmux 标签只覆盖 Claude pane（有 `checkpoint_id` 的 surface）。凡是 hook 正常跟踪、但没有 cmux 标签的会话——Kimi Code 会话、不在 cmux 里跑的 Claude 会话——在 `to_payload` 阶段被整个丢弃，cardputer 的 `tab` 会话列表完全看不到它们，即便 daemon 的 total/running 计数是对的。2026-08-02 真机验证：daemon 日志跟踪到 2 个 Kimi 会话 + 1 个 Claude 会话，设备上只显示 1 个。

## What Changes

- `buddy_core/core.py` 的 `to_payload`：labels 路径构建列表后，把 `_sessions` 中**不在 session_labels 里**的会话追加进 `sessions[]`，总行数仍 cap 16（先标签会话、后无标签会话，保证可聚焦的 Claude 会话优先）。
- 无标签会话不带 `label` 字段——固件现有的 sid 前缀回退显示逻辑不变，**固件零改动**。
- `selectSession` 对无标签会话本来就是 logged no-op（找不到匹配 cmux surface），行为不变。
- 非破坏变更：纯 Claude + cmux 的现有部署，labels 覆盖所有会话时输出与今天完全一致。

## Capabilities

### New Capabilities

- `session-list-payload`: cc-bridge 心跳 `sessions[]` 的构建规则——cmux 标签会话优先、hook 跟踪的无标签会话追加、16 条上限、ext_sessions 合并顺序。

### Modified Capabilities

（无——`daemon-event-mapping` 管的是 hook 事件 → 状态归约，不涉及 sessions[] 负载构建。）

## Impact

- 代码：`tools/buddy_core/core.py`（`to_payload` 一处）；`tests/` 新增/更新 payload 构建的单测。
- 协议：`sessions[]` 可能出现无 `label` 的条目——cardputer-adv-buddy 固件已按可选字段处理（sid 前缀回退），StackChan/StickC 固件忽略 `sessions` 键，均无需同步修改。
- 消费者：cardputer-adv-buddy（显示侧，零改动）；cc-bridge daemon（本变更）。
