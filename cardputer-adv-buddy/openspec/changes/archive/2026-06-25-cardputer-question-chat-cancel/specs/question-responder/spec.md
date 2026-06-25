## ADDED Requirements

### Requirement: 自由文本应答通道（chat about it / cancel）

问答覆盖层 SHALL 在选项之外提供两个 meta 选项——「chat about it」与「cancel」——二者均以**自由文本**形式应答（走 AskUserQuestion 的 Other 通道），而非回送选项 `id`。回送格式 SHALL 在既有 `answerQuestion` 上新增可选 `text` 字段：`{"cmd":"answerQuestion","rid":"<rid>","text":"<utf8 文本>"}`；`text` 与 `ids` 互斥，同一次应答只带其一。

#### Scenario: chat about it 回送讨论文本

- **WHEN** 问答覆盖层打开，用户触发「chat about it」
- **THEN** 固件 SHALL 回送 `{"cmd":"answerQuestion","rid":...,"text":"<chat 文案>"}`（MVP 为固定文案）
- **AND** SHALL NOT 在该次应答里带 `ids`

#### Scenario: cancel 回送 skip 文本而非静默撤

- **WHEN** 用户触发「cancel」
- **THEN** 固件 SHALL 回送 `{"cmd":"answerQuestion","rid":...,"text":"<skip 文案>"}` 让 Claude 优雅解阻
- **AND** SHALL 撤下本机覆盖层并标记该 `rid` 已处理（不再 resume）
- **AND** SHALL NOT 沿用旧的「只本机静默撤、什么都不回送」行为

#### Scenario: 设备端自由输入（secondary，可选）

- **WHEN** 「chat about it」配置为 typed 模式且用户进入文本输入态
- **THEN** 固件 SHALL 用键盘逐字累积一段 UTF-8 文本，提交时作为该 `rid` 的 `text` 回送
- **AND** 取消输入 SHALL 返回选项视图、不回送

### Requirement: bridge 桥接自由文本到 Other 答案

`cc-bridge` SHALL 在收到带 `text` 的 `answerQuestion` 时，经 `cmux rpc feed.question.reply` 以**自由文本 / Other 答案**形式提交给对应 `rid` 的 pending question；不把 `text` 当成选项 id 匹配。`rid` 失效或 reply 失败时 SHALL 记日志并忽略，SHALL NOT 崩溃。

#### Scenario: 自由文本经 cmux 回灌

- **WHEN** cc-bridge 收到 `answerQuestion(rid, text)` 且 cmux 中存在该 `rid` 的 pending question
- **THEN** cc-bridge SHALL 调 `feed.question.reply` 以 Other/自由文本答案提交该 `text`

#### Scenario: feed.question.reply 不支持自由文本时降级

- **WHEN** 经 spike 确认 `feed.question.reply` 只接受选项 id、不接受自由文本
- **THEN** 实现 SHALL 改走终端注入路径：把 `text` 经 `cmd:"key"` CGEvent 注入聚焦的 Claude 终端
- **AND** SHALL NOT 静默丢弃用户的 chat/cancel 意图

## MODIFIED Requirements

### Requirement: 键盘选择并回送应答

固件 SHALL 支持键盘选择：数字键 `1-9` 直选对应选项、`,/.` 移动选中、`ok`/space 提交、`c` 触发「chat about it」、`` ` ``/esc 触发「cancel」；`multiSelect` 为真时数字键 toggle 勾选、`ok` 提交全部勾选项。选项提交时固件 SHALL 经 NUS 回送 `{"cmd":"answerQuestion","rid":"<requestId>","ids":["<optionId>",...]}`（回送稳定 `id` 非 label）；chat/cancel 则回送 `text` 字段（见「自由文本应答通道」）。命令格式与既有回送（`selectSession`/`permission`）同构。

#### Scenario: 单选直选提交

- **WHEN** 问答覆盖层打开且 `multiSelect` 为假，用户按数字键选中某项并提交
- **THEN** 固件 SHALL 回送 `{"cmd":"answerQuestion","rid":...,"ids":["<该选项 id>"]}`
- **AND** 回送的是 option `id` 而非 label

#### Scenario: 多选勾选提交

- **WHEN** `multiSelect` 为真，用户数字键 toggle 勾选多项后 `ok`
- **THEN** 固件 SHALL 回送 `ids` 含全部勾选项的 id

#### Scenario: chat / cancel 走自由文本而非 id

- **WHEN** 用户按 `c`（chat about it）或 `` ` ``/esc（cancel）
- **THEN** 固件 SHALL 回送 `text` 字段而非 `ids`
- **AND** SHALL 撤下覆盖层并标记该 `rid` 已处理
