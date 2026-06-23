# Design: cardputer 物理选择器（AskUserQuestion 应答）

> 探索阶段文档（2026-06-23）。记录方案 B 决策、已 spike 验证的 cmux feed 数据结构、固件 UI、待实现 spike。

## 核心想法

Claude 弹 AskUserQuestion 时，cardputer 显示选项，用户按 `1/2/3` 直接答，cmux 收到答案回灌 Claude。cardputer 只做「显示选项 + 按键应答」，不碰 Claude 的 hook 协议。

## 决策：借道 cmux feed（方案 B）

### Warp/A 路径为何不行：hook 共存冲突

session 跑在 cmux 里，cmux 的 `cmux hooks feed` 已在 PermissionRequest 上接管 AskUserQuestion（显示在 Feed → `feed.question.reply` 回灌）。若 cc-bridge 也注册 PermissionRequest hook 回 `updatedInput.answers`：

```
Claude AskUserQuestion → PermissionRequest
   ├─ cmux feed hook   (updatedInput) ─┐ 并行
   └─ cc-bridge hook   (updatedInput) ─┘
            ▼
   ⚠️ 最后完成者赢，顺序非确定（hooks 并行）
   官方："Avoid having more than one hook modify the same tool's input."
```

### B：cc-bridge 不注册 hook，借 cmux 的 reply

```
Claude AskUserQuestion
   │  PermissionRequest
   ▼
 cmux feed hook ── blocks on semaphore (≤120s) ───────────┐
   │  feed 事件流                                          │
   ▼                                                       │
 cc-bridge: cmux events --category feed (或轮询 feed.list)  │
   │  拿 kind:"question" → requestId + options + sid        │
   ▼  [BLE] payload.question 推 cardputer                   │
 cardputer 显示选项 → 用户按 1/2/3 → ok                     │
   │  [BLE] {cmd:"answerQuestion", rid, ids:[...]}          │
   ▼                                                        │
 cc-bridge: cmux rpc feed.question.reply {request_id, ...}  │
   │                                                        │
   ▼  唤醒 cmux feed hook ───────────────────────────────►─┘
      hook 在 stdout 吐 Claude 期望的 decision → Claude 继续
```

零 hook 冲突，复用 cmux 已验证的 reply，与 session-switcher「cc-bridge 调 cmux」同构。

## 已 spike 验证的 cmux feed 数据结构（2026-06-23）

`~/.cmuxterm/workstream.jsonl` 的 `kind:"question"` 事件（实测 7 条 AskUserQuestion）：

```json
{
  "kind": "question",
  "title": "AskUserQuestion",
  "workstreamId": "claude-41af42bb-fb94-42d9-88bd-f03446d71f25",
  "payload": {
    "question": {
      "requestId": "claude-<sid>-PermissionRequest-AskUserQuestion-<epoch_ms>",
      "questions": [
        { "options": [
            { "id": "opt0", "label": "...", "description": "..." },
            { "id": "opt1", "label": "...", "description": "..." } ] } ]
    }
  }
}
```

- `requestId` → `feed.question.reply` 的 `request_id`（已确认该参数必填）。
- `options[].id`（`opt0`/`opt1`…）稳定，**固件回送 id 而非 label**：省字节、避中文 label 的 BLE/编码问题。
- `workstreamId = claude-<session_id>` → cc-bridge 知道是哪个会话（可与 session_id 对齐）。

## 固件 UI 草图（question 覆盖层）

```
┌─ 240×135 ─────────────────────┐
│ ❓ Format            (header)  │   header
│ How should I format output?   │   question（截断/换行）
│  ▶ 1 Summary                  │   选项 label，▶=当前选中
│    2 Detailed                 │
│    3 Custom...                │
│ 1-3 选 · ,/. 移 · ok 提交     │   操作提示
└───────────────────────────────┘

multiSelect:  ▶[x]1 …  [ ]2 …  [x]3 …   数字 toggle，ok 提交全部勾选
```

- 单选：按数字即选中；可「数字=即选即交」省一步，或「数字选 + ok 交」（待定）。
- 复用 APPROVAL 覆盖层骨架；新增动态 N 选项渲染 + 选中态（沿用 sessions 列表的高亮思路）。

## 开放问题 / 待实现 spike

1. **`feed.question.reply` 完整参数**（实现前必验）：除 `request_id` 外，answer 字段名是什么？传 option `id`（opt0）还是 `label`？multiSelect 怎么传（数组？）？
   —— 需一个 **live pending** question 实测（已 answered 的 requestId 无效），或翻 cmux skill/源。
2. **cc-bridge 订阅形态**：常驻 `cmux events --category feed --reconnect` 子进程读流（实时，feed.md 提到 `feed.item.received`/`feed.item.completed`）vs 轮询 `feed.list` 找 pending question。倾向 events 流。
3. **覆盖层优先级**：question vs approval vs sessions vs help 的显示优先级（question 触发于 PermissionRequest，但语义是问询非审批；倾向 question 高于普通 approval）。
4. **payload 膨胀**：`question` 选项数组 + 既有 `sessions[]`/`entries[]` 叠加逼近固件行缓冲（cardputer `g_line[2048]`、StickC `_LineBuf<1024>`）。question 在场时是否临时省略 sessions/entries？
5. **超时一致性**：cmux feed hook 阻塞 ≤120s；cardputer 侧也需兜底超时（用户不答 → 关面板，让 cmux/终端兜底，类似 approval 的 `APPROVAL_SAFETY_MS`）。
6. **answeredByOther**：用户若在终端/cmux Feed 先答了，cardputer 的 question 面板要能收到「已被回答」信号并关闭（feed event 的 status 变化？）。

## 与既有能力的关系

- 复用 `cardputer-session-switcher` 的 cmux 集成（CmuxClient）+ 覆盖层/选中渲染思路。
- 与 baseline `tool-approval` 同为「覆盖层 + 键盘拍板 + 回送」，但选项动态、走 cmux feed 而非 permission 回送。
