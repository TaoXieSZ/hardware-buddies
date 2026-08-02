# session-focus

## ADDED Requirements

### Requirement: selectSession 聚焦匹配链

daemon 收到固件 `{"cmd":"selectSession","sid":...}` 时 SHALL 按以下顺序尝试匹配 cmux surface，首个命中者聚焦：Claude pane（sid == `resume_binding.checkpoint_id`）→ Cursor pane（标题含 `cursor-<sid>`）→ OpenCode pane → Codex pane（`requested_working_directory` == sid 或以其结尾）→ Kimi pane（见「Kimi 聚焦回退」）。全部未命中 MUST 记日志 `no matching cmux surface (ignored)` 并正常返回，不得抛异常。

#### Scenario: Claude 会话命中

- **WHEN** 设备发送的 sid 等于某 pane 的 checkpoint_id
- **THEN** 该 pane 被聚焦，不再尝试后续匹配

#### Scenario: 全部未命中

- **WHEN** sid 不匹配任何已知规则的 pane
- **THEN** daemon 记日志并忽略，BLE 链路和其他会话不受影响

### Requirement: Kimi 聚焦回退

对未命中前四路规则的 sid，daemon SHALL 在 `~/.kimi-code/sessions/` 下查找目录名包含该 sid 的会话目录，从其 `wd_<dir>_<hash>` 前缀解析工作目录，聚焦 `requested_working_directory` 等于该目录的 cmux pane。多个 pane 同目录时聚焦最近活动的 Kimi pane（标题不匹配 Claude/Codex 已知名模式者）；目录解析失败或无匹配 pane 时按未命中处理。

#### Scenario: 单 Kimi pane 命中

- **WHEN** sid 对应 `~/.kimi-code/sessions/wd_hardware-buddies_<hash>/session_<sid>/`，且 cmux 中有 `requested_working_directory` 为 `/Users/taoxie/hardware-buddies` 的 pane
- **THEN** 该 pane 被聚焦

#### Scenario: 同目录多 pane

- **WHEN** 解析出的工作目录下有多个候选 pane
- **THEN** 聚焦标题不符合 Claude/Codex 模式的最近一个；无法判定时按未命中处理并记日志
