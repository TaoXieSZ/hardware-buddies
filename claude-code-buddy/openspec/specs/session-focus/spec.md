# session-focus

## Purpose

设备点选会话（固件发送 `{"cmd":"selectSession","sid":...}`）后，daemon 把对应的
cmux pane 聚焦到前台。匹配是多路回退链，覆盖 Claude / Cursor / OpenCode /
Codex / Kimi 五类 pane；全部未命中时记日志并忽略，绝不影响 BLE 链路。

## Requirements

### Requirement: selectSession 聚焦匹配链

daemon 收到固件 `{"cmd":"selectSession","sid":...}` 时 SHALL 按以下顺序尝试匹配 cmux surface，首个命中者聚焦：Claude pane（sid == `resume_binding.checkpoint_id`）→ Cursor pane（标题含 `cursor-<sid>`）→ OpenCode pane → Codex pane（`requested_working_directory` == sid 或以其结尾）→ Kimi pane（见「Kimi 聚焦回退」）。全部未命中 MUST 记日志 `no matching cmux surface (ignored)` 并正常返回，不得抛异常。

#### Scenario: Claude 会话命中

- **WHEN** 设备发送的 sid 等于某 pane 的 checkpoint_id
- **THEN** 该 pane 被聚焦，不再尝试后续匹配

#### Scenario: 全部未命中

- **WHEN** sid 不匹配任何已知规则的 pane
- **THEN** daemon 记日志并忽略，BLE 链路和其他会话不受影响

### Requirement: Kimi 聚焦回退

对未命中前四路规则的 sid，daemon SHALL 在 `~/.kimi-code/sessions/` 下查找该 sid 的 `state.json`，以其 `title` 字段（Kimi 会把 pane 标题设为会话首条用户消息）精确匹配 cmux pane 标题并聚焦；`title` 缺失或未匹配时退化为 `cwd` basename 唯一匹配（候选唯一才聚焦）；仍未命中时读候选 pane 屏幕内容，以 `lastPrompt`/`title` 命中唯一候选（pane 被重命名后标题匹配失效的兜底）。Claude pane（有 checkpoint_id）和 Cursor pane（标题含 `cursor-`）MUST 排除在候选之外。目录/文件读取失败或无匹配时按未命中处理。

#### Scenario: 标题精确命中

- **WHEN** sid 的 state.json `title` 为「修好这个hook」，cmux 中存在同标题的非 Claude/Cursor pane
- **THEN** 该 pane 被聚焦（即使同 cwd 下还有其他 pane）

#### Scenario: 无标题时的 cwd 回退

- **WHEN** state.json 无 `title`，`cwd` basename 在候选 pane 中唯一
- **THEN** 该 pane 被聚焦；候选不唯一时按未命中处理并记日志

#### Scenario: pane 重命名后的屏幕内容兜底

- **WHEN** pane 标题被用户改过（≠ state.json `title`），但某唯一候选 pane 的屏幕内容包含 `lastPrompt` 或 `title`
- **THEN** 该 pane 被聚焦；多个或零个命中时按未命中处理
