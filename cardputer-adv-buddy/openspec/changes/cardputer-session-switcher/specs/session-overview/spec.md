## MODIFIED Requirements

### Requirement: 只读可滚动会话列表（MVP）

> 本能力由「只读」升级为「可选中」：原 MVP 的「SHALL NOT 切换/操作会话」限制被本次 change 推翻——选中后的切换动作由新能力 `session-switch` 定义。

固件 SHALL 提供一个可由键盘打开的会话列表视图，逐条显示 `_sessions` 中各会话及其状态（运行/等批/空闲），SHALL 维护一个「当前选中项」并以可见高亮呈现，SHALL 支持键盘 `↑↓` 移动选中项（条目多于一屏时随之滚动）。列表本身仍 SHALL NOT 在 cardputer 上展示某会话的 prompt 详情或审批面板（详情回真终端看）；对选中会话的「切换到前台」动作 SHALL 由 `session-switch` 能力提供，不在本能力内定义其语义。

#### Scenario: 打开列表查看各会话

- **WHEN** 用户按下打开会话列表的键
- **THEN** 屏上 SHALL 列出各会话及其运行/等批状态
- **AND** 条目超过一屏时 `↑↓` SHALL 滚动列表

#### Scenario: 选中项高亮与移动

- **WHEN** 会话列表打开
- **THEN** 屏上 SHALL 高亮当前选中的会话
- **AND** `↑↓` SHALL 在条目间移动选中项（必要时滚动以保持选中项可见）

#### Scenario: 列表不显示详情

- **WHEN** 会话列表打开
- **THEN** 固件 SHALL NOT 在 cardputer 上显示选中会话的 prompt 详情或审批面板
- **AND** 「切到前台」的具体行为 SHALL 由 `session-switch` 能力定义
