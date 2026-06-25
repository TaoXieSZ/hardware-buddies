# Tasks

> 状态：proposal 草拟，**待你批 spec 后再动手**。方案 A（设备顺序答）。预计只改 daemon、不烧固件。

## 0. 评审门
- [x] 0.1 你审阅 `proposal.md` + `specs/question-responder/spec.md` 的行为，确认方案 A + 合成 rid 设计
- [x] 0.2 确认 D3 边界：cancel 单个子问题 = 该问题 skip 文本、继续下一个（draft 默认）

## 1. daemon：暴露全部子问题
- [x] 1.1 `cmux_control.parse_pending_questions` 暴露整个 `questions[]`（不只 q0），保留真 rid
- [x] 1.2 单测：多问题 feed item → 解析出 N 个子问题

## 2. daemon：合成 rid 顺序驱动状态机（cc-bridge）
- [x] 2.1 多问题状态：{real_rid, cur, answers[], N}；heartbeat 把第 cur 子问题以 `<real_rid>#<cur>` 放进 payload.question（header 前缀 `[i/N]`）
- [x] 2.2 收 `answerQuestion(rid="<real_rid>#<i>")` → rsplit('#') 还原；存 answers[i]；cur+1
- [x] 2.3 cur==N → `feed.question.reply(real_rid, answers)` 一次回送；清状态
- [x] 2.4 真 rid 变 / item 不再 pending / age-gate → 重置状态机
- [x] 2.5 N=1 退化路径（与现状一致）

## 3. 单测
- [x] 3.1 多问题顺序驱动（q0→q1→…）
- [x] 3.2 合成 rid 解析（`R#2` → R, 2）
- [x] 3.3 全答完一次 reply（answers 顺序正确）
- [x] 3.4 N=1 退化
- [x] 3.5 cancel 子问题 = skip 文本继续

## 4. 跨层同步 + 验证
- [ ] 4.1 同步线上 `claude-desktop-buddy`（cmux_control + cc-bridge），跑测
- [ ] 4.2 真机：发一个含 2-3 问题的 AskUserQuestion → 设备逐个弹、逐个答 → 全答完我收到完整答复（不卡）
