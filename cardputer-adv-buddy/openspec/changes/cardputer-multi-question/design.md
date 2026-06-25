# Design — cardputer-multi-question

## 数据结构（已查实）

cmux 一个 question feed item（`feed.list`）：
- `request_id`：真 rid（整个 AskUserQuestion 一个）。
- `questions[]`：q0/q1/…，每个含 `header` / `prompt` / `options[]` / `multi_select`。
- 回送：`feed.question.reply(request_id, answers)`，`answers` 是**每个问题一组答案**（opencode-plugin `selections.map(s => [String(s)])` → `[[a0], [a1], …]`）。

现状：`parse_pending_questions` 只取 `questions[0]` → 设备只见第一个，回送一个答案 → 其余子问题永远不弹、AskUserQuestion 不完整。

## 核心设计：daemon 合成 rid 顺序驱动（固件零改）

```
真 rid = R，questions = [q0, q1, q2]
─────────────────────────────────────────────────────
daemon 维护：当前子问题序号 cur、已收答案 answers[]
heartbeat 时把第 cur 个子问题放进 payload.question，rid 用合成 "R#cur"
设备：看到 "R#0" → 答 → 回 answerQuestion(rid="R#0", …)
daemon：解析 "R#cur" → 真 R + 序号 0；存 answers[0]；cur=1
        把 q1 以 "R#1" 放进 payload
设备：看到 "R#1"(新 rid，不被去重) → 答 → 回 "R#1"
…
全部答完(cur==N)：feed.question.reply(R, [answers[0], answers[1], answers[2]])
                  清空多问题状态
```

**为什么固件零改**：设备的 rid 去重（`g_shownQRid`/`g_dismissedQRid`）按 rid 区分。每个子问题用不同的合成 rid（`R#0`/`R#1`），设备就当成「一串不同的问题」逐个弹、逐个答，完全复用现有单问题路径。

## 决策（draft 默认，review 可翻）

### D1. 合成 rid 格式 `<real_rid>#<i>`
- `#` 不会出现在 cmux 的 request_id 里（UUID 风格），可安全分隔。
- 设备回送的 `rid` 原样带回 `R#i`，daemon `rsplit('#',1)` 还原真 rid + 序号。

### D2. 状态机存哪
- 存在 daemon 的 cmux 问答处理处（cc-bridge cmux_question_loop / 对应 state）。一次只跟一个多问题 AskUserQuestion（设备一次也只答一个）。
- 真 rid 变化（新 AskUserQuestion）或被他处答掉（feed item 不再 pending）→ 重置状态机。

### D3. 答案类型
- 子问题可能是单选 / 多选 / chat / cancel（自由文本，见 [[cmux-question-reply-accepts-freetext]]）。每个子问题的答案按现有规则取（ids→label，或 text）。`answers[i]` 是该子问题的 selections 列表。
- cancel 某个子问题：作为该子问题的「skip」文本答案，仍推进到下一个（不整体放弃）；或定义为放弃整组 —— **review 定**。draft：cancel 当该问题的 skip 文本，继续下一个。

### D4. 进度提示
- 子问题 header 前加 `[i/N]`（如 `[2/3] 选数据库`），让用户知道进度。N=1 时不加（与现状一致）。

## 兼容 & 风险
- N=1（绝大多数 AskUserQuestion）：cur 从 0 到 1，行为与现状完全一致（合成 rid `R#0`，答完直接 reply）。可接受：rid 变成 `R#0`，对设备透明。
- 僵尸多问题（feed item 卡 pending）：沿用现有 `QUESTION_MAX_AGE_S` age-gate；状态机在 item 不再 pending 时重置。
- 跨层同步：daemon 改动同步线上 `claude-desktop-buddy`（[[cc-bridge-daemon-source-of-truth]]）。

## 实现范围
- **只改 daemon**：`cmux_control.parse_pending_questions`（暴露全部 questions[]）+ cc-bridge 的问答驱动（合成 rid 状态机 + 累积 + 全答完 reply）。
- **固件不改、不烧录**。
- 单测覆盖：多问题顺序驱动、合成 rid 解析、全答完一次 reply、N=1 退化。
