## Why

cardputer 现在能同时监听 **Claude**（cc-bridge 本机）与 **Cursor**（cursor-bridge 推 `ext_sessions`，change `cardputer-cursor-sessions`）。用户也常在 cmux 里跑 **Codex CLI**。一块 cardputer 应当再把 Codex 的多会话状态纳进来——轮播/钉/列表跨三 agent。

接 Codex 比当初接 Cursor **更省**，因为关键基础设施已就位、且 Codex 的 hook 比 Cursor 更对齐：

- **聚合层已 agent-keyed**：cc-bridge 的 `ext_sessions[agent]` 合并 + `EXT_STALE_SEC` 丢幽灵 + 全表 16 上限（由 `cardputer-cursor-sessions` task 2.2 奠定）。加 `agent:"codex"` 基本「插上就用」。
- **固件已 agent-无关**：`sessions[]{sid,label,st,ws,agent}` 不关心背后是什么 agent（`cardputer-session-rotation`）。固件仅需为第三种 `agent` 值加一个列表标记/颜色。
- **Codex hook 原生 Claude 形状**（实测 `~/.codex/hooks.json`）：事件名就是 `SessionStart`/`UserPromptSubmit`/`PreToolUse`/`PostToolUse`/`Stop`，外加原生 **`PermissionRequest`** 权限事件——比 Cursor（需 `cursor_hook.js` 把 `beforeSubmitPrompt`→`UserPromptSubmit` 翻译）更省，`apply_event` 几乎照搬。
- **Codex hook payload 自带 `session_id` + `cwd`**（实测 oh-my-codex native hook 读这俩字段；codex rollout `session_meta` 也有 `session_id`/`cwd`）。

所以真正要新增的只有两块：一个 **codex-bridge** daemon（镜像 cursor-bridge 的 hook→状态→推 `ext_sessions`），加一套 **cmux 对账**——但这里有唯一一处与 Cursor 的实质差异（见下）。

## 关键设计差异：cmux 无 Codex session-id，只能按 cwd join

Cursor 当初能干净 join 是因为 cmux 把 Cursor pane 标成 `cursor-<UUID>`，与 hook 的 `session_id` 同源（去前缀即相等），所以「只列 cmux 活 pane」+「点设备聚焦某会话」靠 UUID 精确匹配。

**Codex 的 cmux pane title 只是纯 `"codex"`，不带 UUID**（实测 cmux `surface.list`）。但 pane 带 `requested_working_directory`，codex hook 带 `cwd`。故本 change 采用 **cwd join**（用户已拍板）：

- 设备列表来源 = cmux 活 codex pane（满足用户「只看 cmux 活会话」的既有约束）；
- 每条 codex pane 的丰富状态（thinking/tool/waiting）按 **cwd** 从 codex-bridge 的 hook 桶里 join；
- 选中聚焦 = focus cwd 匹配的 cmux codex pane。
- **已知局限**：同一目录跑两个 codex 会话会在 cwd 上撞车 → 合并显示为一条（无法区分/分别聚焦）。这是 cmux 不暴露 codex id 导致的固有限制，本 change 不绕过，仅文档化（design D2）。

## What Changes

- **新 `codex-bridge` daemon**（镜像 `cursor-bridge`，monorepo `claude-code-buddy/tools/codex-bridge/`）：listen codex hook socket → `apply_event`（复用 buddy_core，Codex 事件已是 Claude 形状，近零翻译）→ per-session 状态（idle/thinking/tool/waiting + FIFO waiting seq）→ `push_ext_sessions_loop` 推 `{action:"ext_sessions",agent:"codex",sessions:[...]}` 到 cc-bridge socket。**不自占 BLE**（沿用 Cursor「方案 b」单 owner：cc-bridge 是唯一 BLE central）。
- **codex hook shim**：`codex_hook.js`（near-passthrough：Codex 已 Claude 形状，仅补 `session_id`/`cwd` 归一 + 写 socket，fire-and-forget）+ `codex_hook_permission.js`（绑到 `PermissionRequest`，回送 allow/deny）。
- **cmux 对账（cwd join）**：`cmux_control.codex_session_labels()` 列活 codex surface（title==`codex` 的非 Claude/非 Cursor surface → key=cwd，label 取 cwd 末段/cmux 元信息）；codex-bridge `cmux_codex_label_loop` 填 `state.session_labels`，`_build_codex_sessions` 以 cmux 活 pane 为准、按 cwd join hook 的 st/ws。
- **cc-bridge focus**：`focus_by_codex_cwd(cwd)`（按 cwd 匹配 cmux codex surface → focus）；`_select_session` 回退链追加。`agent:"codex"` 的 `ext_sessions` 合并已被现有逻辑覆盖（无需改 merge）。
- **固件 agent 标记（小改）**：`drawSessions` 为第三种 `agent`=`codex` 加标记 `cx` + 一个区分色（建议绿/青，spec 审批时定）。沿用 cc/cu 同款双重区分（颜色+文字）。

## Non-goals

- **不改 cmux**：只消费 cmux 已暴露的 codex pane（接受其无 session-id 的现状）。
- **不做 Codex 的精确 UUID join**：cmux 不提供，本轮用 cwd join（含同目录撞车局限）。
- **不重构固件渲染**：轮播/钉/列表照用 `cardputer-session-rotation` 的 per-session 机制。
- **不动 Claude / Cursor 既有链路**：纯叠加第三个 ext 源。

## Capabilities

### New Capabilities
- `codex-session-monitoring`：cardputer 经聚合层再纳入 Codex 的多会话状态（轮播/钉/列表跨 Claude/Cursor/Codex 三 agent），Codex per-session 状态来自其 hook，会话列表/聚焦按 cwd 对账 cmux 活 pane。

### 依赖
- 叠在 `cardputer-cursor-sessions`（`ext_sessions[agent]` 聚合 + 固件 agent 标记 cc/cu）与 `cardputer-session-rotation`（per-session `st`/`ws` + 轮播/钉）之上——那两个 change 的机制本 change 直接复用。

## ⚠️ Gating spike（开工前必须验）

1. **codex hook payload 形状**：实抓一份 codex 各事件的 stdin JSON，确认 `session_id` + `cwd` 字段名与值（已从 oh-my-codex native hook 反推：读 `session_id`/`sessionId` + `cwd`；需真抓确认 `hook_event_name` 大小写与 PermissionRequest 形状）。
2. **cwd join 可行性**：确认 codex hook `cwd` 与 cmux codex pane `requested_working_directory` 字面一致（已见同为 `/Users/txie/OpenSourceProjects/hardware-buddies`），可作 join key。
3. **PermissionRequest 回送形态**：确认 codex 权限 hook 的 allow/deny 回送协议（stdout JSON？exit code？）——决定 `codex_hook_permission.js` 怎么写。
