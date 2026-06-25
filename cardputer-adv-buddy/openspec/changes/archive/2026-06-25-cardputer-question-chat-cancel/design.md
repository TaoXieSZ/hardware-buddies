# Design — cardputer-question-chat-cancel

## 核心洞察：两个新选项 = 同一个缺失的原语

「chat about it」与「cancel」表面是两个 UI 选项，本质都是**回送一段自由文本**，走 AskUserQuestion 的 Other 逃生通道：

```
                     回送内容                          Claude 那头的效果
  chat about it  →  自由文本 "我想先聊聊…/<用户打的字>"  →  展开讨论，不当成干净选择
  cancel         →  自由文本 "skip — 你来定"            →  拿最佳默认继续，优雅解阻
  （今天的 ` /esc）→  什么都不回送（本机静默撤）          →  Claude 不知情，挂到 cmux 120s
```

所以技术核心只有一处：**让固件能回送自由文本，而不只是选项 id**。两个选项、固定文案都只是它上面的薄薄一层。一旦 `text` 字段就位，typed 自由输入也只是把固定文案换成用户当场打的字，无需二次改协议。

## 决策（draft 默认值，review 可翻）

### D1. 回送格式：`answerQuestion` 加可选 `text`，与 `ids` 二选一
```
选项选择： {"cmd":"answerQuestion","rid":"<rid>","ids":["<id>",...]}   ← 不变
自由文本： {"cmd":"answerQuestion","rid":"<rid>","text":"<utf8 文本>"}  ← 新增
```
- 同时给了 `ids` 和 `text` 时，bridge 以 `text` 为准（自由文本优先；实际上 UI 不会同时产生）。
- 选这个而非新开 `cmd`：与既有 `answerQuestion` 同构，bridge 只多认一个字段，固件回送路径不分叉。

### D2. chat：canned 为 MVP，typed 为 secondary
- **MVP（核心）**：「chat about it」一键回送固定文案。文案待定（建议中性、推动 Claude 解释而非下指令），如「我想先聊聊这个，先别急着选」。零新 UI。
- **Secondary（可选，本 change 内或后续）**：进设备端文本输入态，用 56 键键盘打一段自定义回复，作为 Other 答案回送。复用 D1 的 `text` 字段。需要：文本输入覆盖层 + UTF-8 逐字累积 + 退格/提交/取消键。
- 为何 canned 先行：满足「补上 Other 通道」的核心诉求、风险最低；typed 是体验升级，不阻塞 MVP。

### D3. cancel：回送「skip」信号，而非静默撤
- 「cancel」回送固定文案「skip — 你来定」（经 `feed.question.reply` 的 `selections:["skip…"]`），让 Claude 解阻继续。严格优于今天的静默撤（后者把 Claude 挂到 120s）。
- 本机超时隐藏（125s 兜底）与「他处已答 → bridge 撤面板」逻辑保留不变，作为最终安全网。
- **为何不用原生 reject**：spike 发现 opencode 有 `POST /question/{id}/reject`（语义更准），但 cmux 未暴露成 `feed.question.reject` RPC，bridge 够不着。待 cmux 暴露、或 bridge 直连 opencode HTTP API 再切；现阶段 reply+skip 文本零额外依赖。

### D4. 键位（question 覆盖层内，作用域隔离）
| 键 | 动作 | 备注 |
|---|---|---|
| `1`-`9` / `,` `.` / space·enter | 选项选择/移动/提交 | **不变** |
| `c` | chat about it | 新增；回送 chat 文案（或进 typed 输入态，见 D2） |
| `` ` `` / esc(fn+esc) | cancel | **语义升级**：从静默撤 → 回送 skip 文案 |

- `c` 取「chat」首字母，问答层内不与选项数字键冲突。
- 复用 `` ` ``/esc 当 cancel：保住肌肉记忆，只是让它从「假取消」变「真取消」。
- 确切键帽 review 可调；若要对齐审批面板近期的「三角」布局（spc/ctrl/`），可把 chat 也挪到固定角位。

## Fallback（spike 已通过 → 不启用，留作防御文档）

> 2026-06-24 spike 确认 `feed.question.reply` 接受自由文本，**主路成立，本节不启用**。仅在未来 cmux 改坏该契约时回退。

若 `cmux feed.question.reply` 只收选项 id、够不着 Other：
- **chat about it** → 改走终端注入：往 Mac 聚焦的 Claude 终端打一段文字（复用现有 nudge 的 `cmd:"key"` → CGEvent 注入路径），等价于用户在终端里打了「我想先聊聊」。不经 feed.question.reply。
- **cancel** → 退回本机静默撤（现状），可选附带注入一次 esc。
- 这条路 UI/键位不变，只是 bridge 侧改用注入而非 reply；spike 结果出来后在 tasks 里二选一落地。

## 跨层同步（root CLAUDE.md 的 sibling 规则）

改了 `answerQuestion` 线协议 → 需同步检查：
- `cc-bridge`（monorepo 镜像 + 线上 `claude-desktop-buddy`）——新增 `text` 分支。
- 兄弟 buddy（claude-code-buddy 的 stick/CoreS3）目前**没有** question 面板，不受影响；但若日后移植，协议已就位。
