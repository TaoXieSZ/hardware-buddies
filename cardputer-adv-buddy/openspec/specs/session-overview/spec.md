# session-overview Specification

## Purpose
TBD - created by archiving change cardputer-claude-buddy. Update Purpose after archive.
## Requirements
### Requirement: 显示会话总数与运行数

固件 SHALL 在状态层持续显示当前会话的总数与运行中数量（来自状态的 `total` / `running`），以一个不遮挡 clawd 的角标呈现，让用户一眼知道有几个 Claude 会话在跑。

#### Scenario: 角标反映计数

- **WHEN** 收到 `total=3, running=2` 的状态
- **THEN** 屏上 SHALL 显示形如 `3 sess · 2 running` 的角标
- **AND** 计数变化时 SHALL 在下一次状态更新后刷新

### Requirement: 只读可滚动会话列表（MVP）

固件 SHALL 提供一个可由键盘打开的只读会话列表视图，逐条显示 `_sessions` 中各会话及其状态（运行/等批/空闲），并 SHALL 支持键盘 `↑↓` 在条目多于一屏时滚动。本 MVP SHALL NOT 提供切换或操作具体会话的能力（仅查看）。

#### Scenario: 打开列表查看各会话

- **WHEN** 用户按下打开会话列表的键
- **THEN** 屏上 SHALL 列出各会话及其运行/等批状态
- **AND** 条目超过一屏时 `↑↓` SHALL 滚动列表

#### Scenario: 仅只读

- **WHEN** 会话列表打开
- **THEN** 固件 SHALL NOT 提供切换/向某会话发指令的操作（留作后续）

