## ADDED Requirements

### Requirement: 显示待应答的 AskUserQuestion 选项

固件 SHALL 在收到状态 payload 的 `question` 字段（来自一个 pending AskUserQuestion）时弹出 question 覆盖层，显示问题的 header、问题文本与各选项的 label，并 SHALL 维护一个可见的当前选中项。覆盖层 SHALL NOT 显示选项的长 description（小屏只显示 label；详情回真终端看）。

#### Scenario: 弹出选项面板

- **WHEN** payload 带 `question`（含 `rid` 与 ≥1 个选项）
- **THEN** 固件 SHALL 显示 question 覆盖层，列出各选项 label 并高亮当前选中项
- **AND** SHALL NOT 显示选项 description

### Requirement: 键盘选择并回送应答

固件 SHALL 支持键盘选择：数字键 `1-9` 直选对应选项、`,/.` 移动选中、`ok`/space 提交、`esc` 取消；`multiSelect` 为真时数字键 toggle 勾选、`ok` 提交全部勾选项。提交时固件 SHALL 经 NUS 回送 `{"cmd":"answerQuestion","rid":"<requestId>","ids":["<optionId>",...]}`，回送选项的稳定 `id`（非 label），命令格式与既有回送（`selectSession`/`permission`）同构。

#### Scenario: 单选直选提交

- **WHEN** question 覆盖层打开且 `multiSelect` 为假，用户按数字键选中某项并提交
- **THEN** 固件 SHALL 回送 `{"cmd":"answerQuestion","rid":...,"ids":["<该选项 id>"]}`
- **AND** 回送的是 option `id` 而非 label

#### Scenario: 多选勾选提交

- **WHEN** `multiSelect` 为真，用户数字键 toggle 勾选多项后 `ok`
- **THEN** 固件 SHALL 回送 `ids` 含全部勾选项的 id

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
