## ADDED Requirements

### Requirement: 多问题 AskUserQuestion 顺序作答

一次 AskUserQuestion 含多个子问题（`questions[]` = q0/q1/…，共享一个真 `request_id`）时，系统 SHALL 让设备**逐个顺序作答全部子问题**，并在全部答完后一次性回送。daemon SHALL 用合成 rid `<real_rid>#<i>` 把第 i 个子问题放进 `payload.question`，使设备复用单问题路径逐个作答；固件 SHALL NOT 需要为此改动。

#### Scenario: 顺序弹出每个子问题

- **WHEN** 一次 AskUserQuestion 含 N(>1) 个子问题
- **THEN** daemon SHALL 先以合成 rid `<real_rid>#0` 把 q0 放进 `payload.question`
- **AND** 设备答完 q0（回送 `answerQuestion(rid="<real_rid>#0", …)`）后，daemon SHALL 以 `<real_rid>#1` 放出 q1
- **AND** 依次直到第 N-1 个子问题

#### Scenario: 全部答完一次性回送

- **WHEN** 第 N 个（最后一个）子问题被答
- **THEN** daemon SHALL 调一次 `feed.question.reply(<real_rid>, [a0, a1, …, a_{N-1}])`，每个子问题一组答案
- **AND** SHALL 清空该 AskUserQuestion 的多问题状态

#### Scenario: 单问题退化（N=1）

- **WHEN** AskUserQuestion 只含 1 个子问题
- **THEN** 行为 SHALL 与现状一致：答完即 `feed.question.reply` 回送，对设备透明（合成 rid `<real_rid>#0`）

#### Scenario: 合成 rid 解析

- **WHEN** daemon 收到 `answerQuestion(rid="<real_rid>#<i>", …)`
- **THEN** daemon SHALL 以最后一个 `#` 分隔还原真 `<real_rid>` 与子问题序号 i，把答案存入 `answers[i]`

#### Scenario: 子问题答案类型沿用

- **WHEN** 某子问题被以选项 id（单/多选）或自由文本（chat/cancel）作答
- **THEN** daemon SHALL 按现有规则（id→label 或 text）生成该子问题的 `answers[i]`
- **AND** cancel 单个子问题 SHALL 作为该子问题的 skip 文本答案、继续下一个（不整体放弃）

#### Scenario: 多问题被他处答或失效则重置

- **WHEN** 该 AskUserQuestion 在终端/cmux 被先答，或 feed item 不再 pending（含 age-gate 超时）
- **THEN** daemon SHALL 重置多问题状态机，不再向设备推该组子问题
