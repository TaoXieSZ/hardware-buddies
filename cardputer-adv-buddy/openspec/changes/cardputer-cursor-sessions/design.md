# Design — cardputer-cursor-sessions

> 完整架构图 + 复杂度逐项见同目录 `architecture-notes.md`。本文件只记 Cursor-specific 决策与 spike。

## 关键洞察（来自架构分析）

- **「有哪些会话」= cmux 免费给，跨 agent**；**「每个会话在干嘛」= 各 agent 自己的 hook**。
- 线协议/固件已 agent-无关（`sessions[]{sid,label,st,ws}`）→ **固件基本不动**。
- 工作全在 daemon：把单 agent 演进成「按 cmux 会话聚合多 agent 状态、单 BLE owner」。

## Cursor 利好

- Cursor **有 hook**（`cursor-bridge/cursor_hook.js`），`cursor-bridge/bridge.py` 的 `apply_event` 与 cc-bridge **同构**（SessionStart/UserPromptSubmit/PreToolUse/Stop/PermissionRequest…）。
- 所以给 Cursor 加 per-session 状态 = 把 `cardputer-session-rotation` 给 cc-bridge 加的 `set_session_state` **同款贴到 cursor-bridge 的 apply_event**。`BuddyState.set_session_state` 已在 `buddy_core/core.py`（共享）——cursor-bridge 直接可用。

## 决策（draft 默认，review 可翻）

### D1. 聚合形态 —— spike 定（2026-06-25）

**实测现状**：cc-bridge 独占 `Claude-7AFD`（cardputer，活跃）；cursor-bridge 扫 `Cursor-*` **找不到设备**（无 Cursor stick），状态正确追踪但**没有 BLE 出口**。所以不是「两活 owner 抢链路」，而是「cursor 状态没出口 + cc-bridge 独占唯一设备」。cc-bridge 已有 `MultiBleWriter` / `write_to` / `connected_prefixes` 基础设施。

**推荐方案（b-refined）：cursor-bridge 把它的 sessions[] 快照推给 cc-bridge，cc-bridge 合并后从唯一 BLE 发出。**
- cursor-bridge 每个 heartbeat 往 cc-bridge socket 投一条 `{cmd:"ext_sessions", agent:"cursor", sessions:[{sid,label,running,st,ws}...]}`（sid = cursor session UUID）。
- cc-bridge 把这批「外部会话」存在**独立字段**（不混进自己的 `_sessions`，避免 total/running 计数 + reaper 逻辑互相污染），`to_payload` 时 append 到 `sessions[]`、每条标 `agent:"cursor"`。
- cursor-bridge 不再连自己的 BLE（或保留作「独立 Cursor stick」可选部署）。
- 为什么不选 (c) 路由 hook 进同一 socket：会把两 agent 的 apply_event 计数器/reaper 逻辑混在一个 BuddyState，污染面大。(b-refined) 让两 agent 状态**各自算、只在 payload 层合并**，隔离干净。
- 为什么不选 (a) 新 aggregator：现在 cc-bridge 已是唯一 owner，新进程是过度工程；(b-refined) 复用现有 owner。
- **FIFO 钉跨 agent**：ws 序号要跨两 agent 单调——cc-bridge 合并时对 cursor 的 ws 做偏移/重排，或两边共用一个 seq 源（实现期细化）。

### D2. 会话归一（避免 sid 冲突 + 支持聚焦）— ✅ spike 已解（2026-06-25）
- **Claude pane**：`resume_binding.kind=claude` + `checkpoint_id`；按 checkpoint 归一、聚焦。
- **Cursor pane**：`resume_binding=null`（cmux **不给** checkpoint 绑定）。但 surface 有稳定 `id`（surface UUID，如 `E0663C56-…`）+ `ref`（`surface:7`），且 **cursor session id 嵌在 title**（`…· cursor-889f542f-…`）。
- 结论：**Cursor 会话用 surface UUID 归一**（sid=surface id，避免与 Claude checkpoint 冲突）；**label** 从 title 提（含 `cursor-xxx`）；**聚焦走 surface id 直接 focus**（cmux 支持按 surface 聚焦，无需 checkpoint）。
- 检测「这是 Cursor agent pane」：`resume_binding==null` 且 `type!=markdown` 且 title 含 `cursor-`（注意排除 markdown/browser surface 与混入 claude-resume 命令的壳）。
- **状态来源确认**：cmux **feed 只有 `source:claude` events**，Cursor 状态 cmux 一点不给 → 必须由 cursor-bridge 的 hook 提供（见 Cursor 利好）。聚合层 = Cursor 状态(来自 cursor hook) + Cursor 登记/聚焦(来自 cmux surface)。

### D3. agent 标注
- `sessions[]` 每条按需带 `agent`（`claude`/`cursor`）。固件**可选**在标识前加标记（`▸cursor` / `C·`），核心功能（轮播/钉）不依赖。
- 默认：daemon 发 `agent` 字段；固件先不渲染标记（MVP），后续加。

### D4. 固件
- **不改核心**。`sessions[]` 多出 Cursor 条目照样轮播/钉/列表。
- 可选：解析 `agent` 字段 + 标识前缀（小改 `SessionInfo` + `drawSessionTag`）。

## ✅ ID join —— 免费（2026-06-25 实测确认）

> 早前一版基于 tail -5 的小样本误判「id 对不上、是硬问题」。**作废**。完整日志证明它俩同一 UUID：

```
cursor-bridge:  event: Stop session=66099139-1550-4241-bd6a-a177bfb0d21c
cmux title:     ...· cursor-66099139-1550-...
                          └ 同一个 UUID，title 仅多 cursor- 前缀
```

**cursor hook 的 `session_id` == cmux title 的 `cursor-<UUID>`（去前缀）。** 直接按 UUID join：
- **状态** 来自 cursor-bridge（按该 UUID 建桶）。
- **登记/label/聚焦** 来自 cmux surface（title 提 `cursor-<UUID>` → 匹配同一 surface → 按 surface id focus）。

对称性：Claude 走 `resume_binding.checkpoint_id`，Cursor 走 title 的 `cursor-<id>`——**两个 agent 都能干净地和 cmux join**（见 [[cursor-session-id-join-problem]] 已更正、[[cardputer-session-switcher-cmux]]）。所以**精确 selectSession 聚焦 Cursor pane 也可行**，不必降级。

**仍需确认（盲区）**：cursor-bridge 日志最近 stale → 开工前确认当前 Cursor 实例 hooks 真在 fire（没装/不发则整个方案无源）。这是唯一未消的前置。

## 跨层同步
- ⚠️ cursor-bridge 实际跑在**第三个 checkout** `claude-desktop-buddy-cursor/`（不是 monorepo、也不是 claude-desktop-buddy）。Cursor 侧改动要同步到那个仓（见 [[cursor-bridge-third-checkout]]）。
- 聚合层落地后，注意 BLE 单 owner 与现有 cc/cursor 双 bridge 部署的迁移。

## 风险
- **单 central 约束**：BLE 设备一次只能一个 central；聚合必须收敛到一个 owner，否则两 bridge 抢链路。
- **Cursor 会话→surface 绑定**若 cmux 不给，selectSession 对 Cursor 会话会不准（D2 风险）。
- payload 体积：多 agent → sessions[] 更长，复核固件行缓冲（cardputer 2048 够；claude-code-buddy 1024 紧）。
