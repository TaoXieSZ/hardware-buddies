# session-list-payload delta — kimi-session-identity

## MODIFIED Requirements

### Requirement: 无标签会话追加

在标签会话之后，`sessions[]` SHALL 追加 `_sessions` 中 sid 不在 `session_labels` 里的会话。这些条目 MUST NOT 携带 `label` 字段（固件以 sid 前缀回退显示），并携带 `running` 及存在时的 `st`/`ws`。会话来源非 Claude 时（见「事件来源标记」），条目 SHALL 携带 `agent` 字段（如 `"kimi"`）；来源为 Claude 或未知时省略该字段（固件缺省按 Claude 显示）。

#### Scenario: Kimi 会话进入设备列表

- **WHEN** daemon 跟踪到 1 个有 cmux 标签的 Claude 会话和 2 个无标签的 Kimi 会话（事件带 `agent: "kimi"`）
- **THEN** `sessions[]` 共 3 条：第 1 条带 label 的 Claude 会话，后 2 条为无 label、带 `agent: "kimi"` 的 Kimi 会话

#### Scenario: 未知来源不标 agent

- **WHEN** 无标签会话的事件未携带 `agent` 字段
- **THEN** 其条目不含 `agent` 键，固件回退显示 `cc` 标

#### Scenario: 无 cmux 标签源时的回退

- **WHEN** `session_labels` 为空（cmux 未安装或无标签会话）
- **THEN** `sessions[]` 按现有回退路径从 `_sessions` 构建，行为不变（`agent` 字段规则同上）

## ADDED Requirements

### Requirement: 事件来源标记

`hook.py` SHALL 支持 `--agent <name>` 参数（`CC_BRIDGE_AGENT` 环境变量作为缺省），在转发事件 JSON 前注入 `"agent": "<name>"`。`apply_event` SHALL 把事件的 `agent` 值记入 `_sessions[sid]["agent"]`（事件未携带时不动已有值）。

#### Scenario: Kimi hook 注入标记

- **WHEN** 以 `--agent kimi` 调用的 hook.py 收到 `SessionStart` 事件
- **THEN** 转发给 daemon 的 JSON 含 `"agent": "kimi"`，且 `_sessions[sid]["agent"] == "kimi"`

#### Scenario: Claude hook 无标记

- **WHEN** 未配置 `--agent` 的 hook.py 转发事件
- **THEN** 事件 JSON 不含 `agent` 键，会话来源保持未知（按 Claude 处理）
