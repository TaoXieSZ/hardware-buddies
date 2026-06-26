## Why

一次 AskUserQuestion 可以带**多个问题**（工具支持最多 4 个）。但 cardputer 的问答面板只弹/答**第一个**就退，其余问题永远不弹 —— 于是发起方（Claude/Cursor）一直等不到完整答复、卡住。

根因在 daemon 侧：`parse_pending_questions` 的 MVP 只取 cmux feed item 的 `questions[0]`，多问题不拆。设备拿到的 `payload.question` 永远只有第一个子问题。

本 change 让设备**顺序答完一次 AskUserQuestion 的全部子问题**（用户已选方案 A）。

## What Changes

- **daemon 顺序驱动多问题**：cmux 一个 question feed item 含 `questions[]`（q0/q1/…）与一个真 `request_id`。daemon 改成**逐个子问题**地放进 `payload.question`：给第 i 个子问题派一个**合成 rid** `<real_rid>#<i>`，设备当成一个普通问题答。
- **累积答案、全答完一次回送**：设备回送 `answerQuestion(rid="<real_rid>#<i>", …)` 时，daemon 解析出真 rid + 子问题序号 i，存下第 i 个答案；只要还有未答子问题，就把下一个子问题（`#<i+1>`）放进 payload；全部答完后，调一次 `feed.question.reply(real_rid, [a0, a1, …])` 回送全部答案。
- **固件零改动**：设备只看到「一连串普通问题」（合成 rid 各不相同，绕过它的 rid 去重），无需改固件、无需烧录。
- **进度提示（可选）**：子问题 header 前缀 `[i/N]` 让用户知道答到第几个。

## Non-goals

- 不在固件里维护多问题状态（全部由 daemon 状态机驱动，固件不变）。
- 不改 AskUserQuestion 单问题的现有行为（多 N=1 时与现状完全一致）。
- 不做「一屏同时显示多个问题」（小屏放不下，仍逐个）。

## Capabilities

### Modified Capabilities
- `question-responder`：在「显示单个 pending question + 回送」之上，支持**一次 AskUserQuestion 的多个子问题顺序作答**（daemon 用合成 rid 逐个驱动、累积答案、全答完一次回送）。

## 评审说明（先批 spec 再动手）

本 change 是行为变更，按 OpenSpec 流程：先把行为写进 spec（下方 `specs/question-responder/spec.md` 的 GIVEN-WHEN-THEN），你审阅批准后再改 daemon 代码。预计**只改 daemon（cc-bridge + cmux_control），不改固件、不用烧录**。
