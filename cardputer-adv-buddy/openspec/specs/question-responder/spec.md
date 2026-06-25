# question-responder Specification

## Purpose
TBD - created by archiving change cardputer-question-responder. Update Purpose after archive.
## Requirements
### Requirement: 显示待应答的 AskUserQuestion 选项

固件 SHALL 在收到状态 payload 的 `question` 字段（来自一个 pending AskUserQuestion）时弹出 question 覆盖层，显示问题的 header、问题文本与各选项的 label，并 SHALL 维护一个可见的当前选中项。覆盖层 SHALL NOT 显示选项的长 description（小屏只显示 label；详情回真终端看）。

#### Scenario: 弹出选项面板

- **WHEN** payload 带 `question`（含 `rid` 与 ≥1 个选项）
- **THEN** 固件 SHALL 显示 question 覆盖层，列出各选项 label 并高亮当前选中项
- **AND** SHALL NOT 显示选项 description

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

### Requirement: bridge 经 cmux feed 桥接，不注册 PermissionRequest 应答

`cc-bridge` SHALL 通过 cmux feed（订阅 feed 事件流或轮询）获取 pending AskUserQuestion（`kind:"question"`，取 `requestId` / `options[].id,label` / `workstreamId`）并放入 payload `question`；收到固件 `answerQuestion` 时 SHALL 调 `cmux rpc feed.question.reply`（用 `request_id` + 选中 id）回灌，由 cmux 唤醒其阻塞的 hook 答复 Claude。cc-bridge SHALL NOT 自行注册/应答 Claude 的 PermissionRequest hook 来回写 `updatedInput`（避免与 cmux feed hook 并行修改同一 tool input 的非确定性冲突）。

#### Scenario: 选中经 cmux 回灌

- **WHEN** cc-bridge 收到 `answerQuestion(rid, ids)` 且 cmux 中存在该 `rid` 的 pending question
- **THEN** cc-bridge SHALL 调 `feed.question.reply` 提交该答案
- **AND** SHALL NOT 通过 PermissionRequest hook 回写 updatedInput

#### Scenario: 回灌失败安全降级

- **WHEN** `rid` 已失效（他处已答/超时）或 `feed.question.reply` 失败
- **THEN** cc-bridge SHALL 记录日志并忽略，SHALL NOT 崩溃

### Requirement: 他处应答或超时撤下面板

固件的 question 覆盖层 SHALL 在该问题被他处应答（终端 / cmux Feed）或本地兜底超时后自动撤下，不悬挂。bridge SHALL 在问题不再 pending 时（cmux feed 完成信号）从 payload 清除 `question`。

#### Scenario: 他处已答

- **WHEN** 同一问题在终端或 cmux Feed 被先应答
- **THEN** bridge SHALL 清除 payload `question`，固件 SHALL 撤下覆盖层

#### Scenario: 本地超时兜底

- **WHEN** 用户在固定时长内未应答
- **THEN** 固件 SHALL 撤下 question 覆盖层（让 cmux/终端兜底），SHALL NOT 永久悬挂

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

