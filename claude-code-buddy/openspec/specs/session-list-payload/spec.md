# session-list-payload

## Purpose

cc-bridge 心跳里 `sessions[]` 的构建规则。该数组驱动 cardputer 的 `tab` 会话列表
（物理 session 切换器），由 `BuddyState.to_payload`（`tools/buddy_core/core.py`）
生成。数据源有两个：cmux 标签快照（`session_labels`，每个可聚焦 Claude pane 的
`checkpoint_id` → 自动命名）和 hook 跟踪的 `_sessions`。外部 agent bridge
（cursor/codex/opencode）推来的 `ext_sessions` 在末尾合并。

## Requirements

### Requirement: 标签会话优先构建

当 `state.session_labels` 非空时，cc-bridge 心跳的 `sessions[]` SHALL 首先包含所有 cmux 标签会话（每个 cmux surface 的 `checkpoint_id`），每条携带 `sid`、`running`（来自 hook 跟踪的 `_sessions`，未知则为 false），有自动命名时携带 `label`。

#### Scenario: 纯 Claude + cmux 部署

- **WHEN** 所有 hook 跟踪的会话都有对应的 cmux 标签
- **THEN** `sessions[]` 与变更前输出完全一致（标签会话、带 label，按 labels 顺序）

#### Scenario: 标签会话带上运行状态

- **WHEN** 某标签会话的 sid 在 `_sessions` 中标记 running
- **THEN** 该条目 `running` 为 true，且 `st`/`ws` 字段在存在时一并携带

### Requirement: 无标签会话追加

在标签会话之后，`sessions[]` SHALL 追加 `_sessions` 中 sid 不在 `session_labels` 里的会话。这些条目 MUST NOT 携带 `label` 字段（固件以 sid 前缀回退显示），并携带 `running` 及存在时的 `st`/`ws`。

#### Scenario: Kimi 会话进入设备列表

- **WHEN** daemon 跟踪到 1 个有 cmux 标签的 Claude 会话和 2 个无标签的 Kimi 会话
- **THEN** `sessions[]` 共 3 条：第 1 条带 label 的 Claude 会话，后 2 条为无 label 的 Kimi 会话

#### Scenario: 无 cmux 标签源时的回退

- **WHEN** `session_labels` 为空（cmux 未安装或无标签会话）
- **THEN** `sessions[]` 按现有回退路径从 `_sessions` 构建，行为不变

### Requirement: 16 条上限与优先级

`sessions[]` 总条数 MUST 不超过 16。标签会话 SHALL 优先占位，无标签会话按 `_sessions` 插入顺序占用剩余槽位，超出部分丢弃。

#### Scenario: 槽位满时丢弃无标签会话

- **WHEN** 标签会话已达 16 条
- **THEN** 无标签会话全部被丢弃，`sessions[]` 恰好 16 条

#### Scenario: 槽位不足时截断

- **WHEN** 标签会话 14 条、无标签会话 5 条
- **THEN** `sessions[]` 为 14 条标签会话 + 前 2 条无标签会话

### Requirement: ext_sessions 合并不变

外部 agent（cursor/codex/opencode bridge）推来的 `ext_sessions` SHALL 维持现有合并语义：ext 条目先占槽（上限 16），剩余槽位给本地 `sessions[]`，超过 `EXT_STALE_SEC` 的快照丢弃。

#### Scenario: ext 与本地混合

- **WHEN** codex-bridge 推来 3 条 ext_sessions，本地有 2 条标签会话 + 1 条无标签会话
- **THEN** `sessions[]` 前 3 条为带 `agent` 字段的 codex 条目，随后是 2 条标签会话和 1 条无标签会话
