# Tasks

> 探索阶段记录（2026-06-23）。方案 B（借道 cmux feed）已 spike 验证 feed 数据结构 + reply 存在。
> 实现前先做 §0 的两个 spike 把参数定死。

## 0. 前置 spike — 大部分已定 2026-06-23（adv 关机，无 live 实测，标⚠️待复核）
- [x] 0.1 `feed.question.reply` 参数（报错探测 + 翻 cmux opencode-plugin.js）：
      `{request_id, selections:[string]}`（字段名实测确认）；`selections` = **label**（强推断，⚠️ id 可能性待 live 复核）；
      fire-and-forget，`delivered:true` 不校验 rid（失效 rid 不崩）。multiSelect → selections 多元素。
- [x] 0.2 订阅形态 = **轮询 `feed.list`**（feed item 有 `status`，实测 `expired`；pending 取非终态）。
      复用 cmux_label_loop 模式，比常驻 `cmux events` 子进程简单。⚠️ pending status 确切值待 live 复核（保守取非 expired/completed）。
- [x] 0.3 覆盖层优先级 = APPROVAL > QUESTION > SESSIONS > HELP > NORMAL（question 高于 sessions，低于工具审批）；
      payload：question 在场时仍发 sessions（cardputer g_line[2048] 够），question 字段紧凑（rid + 选项 label）。

## 1. bridge（两仓同步）—— ✅ 实现 2026-06-23（179 passed；⚠️ cmux 侧未 live 验证）
- [x] 1.1 轮询 cmux feed：`cmux_control.parse_pending_questions`（纯函数，可测）+ `CmuxClient.pending_questions`
      解析 `feed.list` 的 `kind:"question"` → `{rid, header, prompt, multi, options:[{id,label}], sid}`（MVP 取 questions[0]）；
      `cc-bridge cmux_question_loop` 每 2s 轮询 off-loop → `state.pending_question`。⚠️ pending status 判定（非终态 string）未 live 验证。
- [x] 1.2 payload `question`：`to_payload` 输出 `{rid, header, text, multi, options:[{id,label}]}`（截断防溢出）；
      仅 pending 时带。（暂未省略 sessions/entries——cardputer 2048 缓冲够。）
- [x] 1.3 接收 `answerQuestion(rid, ids)`：`on_stick_line` 分支 → `_answer_question` 回调，id→label（查 pending）
      → `CmuxClient.answer_question` → `feed.question.reply{request_id, selections}`。无匹配/失败 → 日志忽略。
      ⚠️ selections=label（强推断，id 可能性待 live 复核）。
- [x] 1.4 question 撤面板：`cmux_question_loop` 发现不再 pending → `pending_question=None` → payload 无 question → 固件撤面板。
      （走轮询而非 `feed.item.completed` 信号；~2s 延迟。）
- [x] 1.5 `SAFE_TOOLS` 不变；不注册 PermissionRequest 应答（借道 cmux feed，零 hook 冲突）。

## 2. 固件（cardputer-adv-buddy）—— ✅ 实现 + 编译通过 2026-06-23（pio run SUCCESS，Flash 36.6%）
- [x] 2.1 `link_state` 加 `QuestionOption`/`QuestionState`；`cclink` 解析 payload `question`。
- [x] 2.2 `clawd_player` QUESTION 覆盖层：header + 问题 + N 选项 + 选中高亮；multiSelect 勾选态（`[x]`）。
- [x] 2.3 键盘（main.cpp）：`1-9` 直选（单选即选即交 / 多选 toggle）、`,/.` 移动、`ok`/space 提交、`esc` 取消。
- [x] 2.4 `sendAnswerQuestion(rid, ids)`（回送 option id 数组）。
- [x] 2.5 优先级 APPROVAL>QUESTION>SESSIONS>HELP；兜底超时复用 `APPROVAL_SAFETY_MS`。

## 3. specs
- [x] 3.1 `question-responder` capability spec（explore 时写，4 条 ADDED requirement；strict 校验通过）。

## 4. 验证 —— ✅ 真机验证 2026-06-23（4.2/4.3 deferred，见 RETROSPECTIVE.md）
- [x] 4.1 真机端到端：烧 firmware.bin → Claude 弹 AskUserQuestion → cardputer 显示选项 → 按数字选 → Claude 收到答案继续。（2026-06-23：daemon 日志 reply=True，真机按数字答成功）
- [ ] 4.2 multiSelect：多选 toggle + 提交准确。（deferred — 未测，见 RETROSPECTIVE.md）
- [ ] 4.3 并发：他处先答 → 面板撤下；超时兜底；覆盖层优先级不打架。（部分：超时 125s / 覆盖层优先级已做+验证（自动放行）；"他处先答撤面板"未专门测 — deferred）
- [x] 4.4 复核 cmux 猜测点：① `selections` 用 label 还是 id ② pending question 的 status 确切值
      （2026-06-23 验证：cmux_question_loop 捕获 live 问题、用 label 回灌 reply=True 成功解除）。
