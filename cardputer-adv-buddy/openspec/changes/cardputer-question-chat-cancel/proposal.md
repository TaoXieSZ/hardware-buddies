## Why

设备上的 AskUserQuestion 问答面板现在只能做一件事：从问题自带的选项里挑一个（或多个）`id` 回送（`{"cmd":"answerQuestion","rid":..,"ids":[..]}`）。但 Claude Code 自己的 AskUserQuestion 永远还带一条**逃生通道**——「Other」自由文本，让用户不挑现成选项、改用自己的话回答。设备上够不着这条通道，于是出现两个缺口：

1. **没法「先聊聊」**：用户想说「这几个选项都不对 / 我想先讨论一下再决定」时，只能干等或跑回终端打字。小屏问答的价值（当场拍板）在这里断了。
2. **「取消」是假的**：当前 `` ` ``/esc 只是**本机静默撤面板**——把 `rid` 记进 `g_dismissedQRid`、隐藏覆盖层，**什么都不回送**。Claude 那头毫不知情，继续阻塞到 cmux 的 120s 超时。用户以为「取消了」，实际只是把自己这块屏的提示关掉了。

本 change 给问答面板补上「chat about it」与「cancel」两个 meta 选项——就像 Claude Code 问用户时那样，永远留一条「不挑现成项、说自己的话」和一条「明确放过」的路。

## What Changes

- **统一的使能原语：自由文本应答**。两个新选项本质是同一件事——**回送一段自由文本（走 AskUserQuestion 的 Other 通道）**，而不是选项 `id`。为此 `answerQuestion` 回送格式新增可选 `text` 字段；`ids` 与 `text` 二选一。
- **「chat about it」选项**（MVP：canned）：问答面板新增一个键，按下回送一段固定文本（如「我想先聊聊这个，先别急着选」），Claude 收到后会展开解释/讨论而非被当成干净的选项选择。**（可选 secondary）** 设备端自由输入：用 56 键键盘当场打一段自定义回复作为 Other 答案。
- **「cancel」选项**：把现有 `` ` ``/esc 从「静默本机撤」升级为**回送一段「skip — 你来定」文本**，让 Claude 优雅解阻（拿它的最佳默认继续），而不是挂到 120s。本机超时隐藏仍作为最终兜底保留。
- **cc-bridge 桥接**：收到带 `text` 的 `answerQuestion` 时，经 `cmux rpc feed.question.reply` 以**自由文本 / Other 答案**形式提交。

## Non-goals

- **不动审批面板**。本 change 只改 AskUserQuestion 问答面板；权限审批（once/deny/always）的「想讨论一下这个工具调用」是后续话题。
- **不做多轮对话 UI**。「chat about it」只负责把球踢回 Claude（让它继续在终端/会话里对话），不在设备上做来回聊天的渲染。
- **typed 自由输入非 MVP 必需**。核心先上 canned 文本；设备端打字作为 secondary，复用同一 `text` 字段，不需要二次改协议。

## Capabilities

### Modified Capabilities
- `question-responder`：在既有「显示选项 + 选 id 回送」之上，新增自由文本应答通道（chat / cancel 两个 meta 选项），并把 cancel 从静默本机撤升级为回送信号。

## ✅ Gating spike（已验证 2026-06-24）

押在「`cmux feed.question.reply` 是否接受自由文本」上的前提——**已确认接受**，走主路、fallback 不需要。证据（cmux app `/Applications/cmux.app/Contents/Resources/opencode-plugin.js`）：

```js
// :226-228
const questionAnswers = (selections) =>
  (!Array.isArray(selections) || selections.length === 0)
    ? [[]] : selections.map((s) => [String(s)]);   // 每个 selection 原样 String()，零校验
// :119-130  replyQuestion → POST /question/{id}/reply  body:{answers}  透传
```

`selections` 里任意字符串被原样当答案提交，不与问题选项集校验。故 `selections:["<chat/skip 文本>"]` 即可走 Other 自由文本通道。cmux 本体 localizable 另有 `feed.question.typeSomething`（原生自定义答案）佐证自由文本是一等公民。

**附带发现**：opencode 有 `POST /question/{id}/reject` 原生「拒绝」端点（plugin `rejectQuestion`），语义上比「cancel 发 skip 文本」更干净；但 cmux **未**把它暴露成 `feed.question.reject` RPC（只暴露了 `feed.question.reply`），bridge 当前够不着。故 cancel 仍走「reply + skip 文本」，原生 reject 列为后续（待 cmux 暴露该 rpc，或 bridge 直连 opencode HTTP API）。
