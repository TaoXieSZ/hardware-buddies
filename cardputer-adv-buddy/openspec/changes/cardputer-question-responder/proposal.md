> **🔬 探索阶段记录（2026-06-23）** — 方向已定（借道 cmux feed，方案 B），关键机制已 spike 验证
> （feed `kind:"question"` 事件含 requestId + options，`feed.question.reply` 存在）。实现待续。
> 延续 `cardputer-session-switcher`：cardputer 从「物理 session 切换器」再进一步成为「物理选择器」。

## Why

cardputer 已能切换 session（`cardputer-session-switcher`）。下一个 terminal 难做、cardputer 擅长的事：
**在 Claude 弹出选择题（AskUserQuestion）时，直接在 cardputer 上按 `1/2/3` 选，不用回终端敲键盘**。

这把 cardputer 从「看 + 切」扩展到「**就地拍板**」——选择题是 agent 交互里最频繁的打断点，物理键一按即答，省下「切回终端 → 找光标 → 选 → 回来」的全程。和已有的工具审批面板（approval）同类，只是选项是动态多项。

## What Changes

- cardputer 新增 **question 覆盖层**：显示 AskUserQuestion 的 header / 问题 / N 个选项，键盘 `1-9` 直选、`,/.` 移动、`ok` 提交；multiSelect 时数字 toggle 勾选。
- 选中 → 固件回送 `{cmd:"answerQuestion", rid, ids:[...]}`。
- cc-bridge **借道 cmux feed**：订阅 cmux feed 事件流拿到 pending question（requestId + 选项 + 所属 session），推给 cardputer；收到固件回送 → 调 `cmux rpc feed.question.reply` 回灌，由 cmux 唤醒它阻塞的 hook 答复 Claude。

## 关键决策：借道 cmux feed（方案 B），不自己回 PermissionRequest hook

| | A：cc-bridge 自己回 hook | **B：借道 cmux feed（选定）** |
|---|---|---|
| AskUserQuestion 回复 | 自己拼 PermissionRequest 的 `updatedInput.answers`（agent 标⚠️待实测） | 调 cmux 已验证的 `feed.question.reply` |
| 与 cmux 共存 | ⚠️ 两个 PermissionRequest hook 并行改同一 input，**最后完成者赢、顺序非确定**（官方明确警告） | ✓ 不注册 hook，零冲突 |
| 架构 | 新路径 | ✓ 与 session-switcher「cc-bridge 调 cmux」同构 |

**决策依据（已 spike 验证 2026-06-23）**：cmux 的 feed audit（`~/.cmuxterm/workstream.jsonl`）里 `kind:"question"` 事件携带 `payload.question.requestId`、`questions[].options[]{id,label,description}`、`workstreamId="claude-<session_id>"`；`feed.question.reply` 存在且要求 `request_id`。数据链完整。详见 design.md。

## Non-goals

- 不自己改写 Claude Code 的 PermissionRequest `updatedInput`（避免与 cmux feed hook 的非确定性冲突）。
- 不支持非 cmux 环境（本功能与 session-switcher 一样绑定 cmux；无 cmux 时静默不显示 question）。
- 不在 cardputer 显示选项的长 description（小屏只显示 header + label；详情回真终端看）。
- MVP 以**单选**为主，multiSelect 作为次级（toggle + 提交）。

## Capabilities

### New Capabilities
- `question-responder`: cardputer 显示 AskUserQuestion 选项并按键应答；cc-bridge 经 cmux feed 桥接（订阅 pending question + `feed.question.reply` 回灌）。

## Impact

- **bridge**（claude-desktop-buddy + monorepo 镜像）：新增 cmux feed 订阅（`cmux events --category feed` 或轮询 `feed.list`）；payload 新增 `question` 字段（rid + 选项）；收 `answerQuestion` → `feed.question.reply`。不碰 `hook_permission.py`（不注册 PermissionRequest 应答）。
- **固件**（cardputer-adv-buddy）：新增 question 覆盖层 UI + 键盘选择 + `sendAnswerQuestion`；覆盖层优先级需排（与 approval / sessions 并存）。
- **payload**：`question` 含选项数组（用 option `id` 而非 label 回送，省字节 + 避中文编码），来时考虑临时不发 `sessions[]`/`entries[]` 防 buffer 膨胀。
- **依赖**：cmux feed API（`feed.question.reply`、feed events）。
- **开放问题 / spike**：见 design.md。
