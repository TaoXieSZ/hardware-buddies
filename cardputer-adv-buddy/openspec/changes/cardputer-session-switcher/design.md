# Design: cardputer 物理 session 切换器

> 探索阶段文档（2026-06-22 explore session）。记录方向决策与切到 cmux 后要解决的开放问题。

## 核心想法

cardputer 当**物理 session 切换器**：点选会话列表里的某个 session → Mac 上对应终端 pane/tab 跳到前台。详情与操作回真终端做，cardputer 只负责「快速选择 + 跳转」。

## 决策：绑定 cmux

### 调研：Warp vs cmux 的程序化 session 切换

**Warp — 做不到**
- 无 AppleScript 支持（warpdotdev/Warp Issue #3364，长期开着未实现）
- 无 macOS Shortcuts 支持
- URI scheme（`warp://action/new_tab`、`new_window`、`launch/<config>`）只能**开新东西**
- **没有任何 CLI / URI / IPC 接口聚焦「已存在的特定 tab」**（Issue #9083 请求中未实现）
- 唯一歪招：AppleScript 模拟键盘循环切 tab——极脆、无法精确定位，放弃

**cmux — 能做到**
- manaflow-ai/cmux：开源 Ghostty-based macOS 终端，vertical tabs + split panes + 内嵌浏览器
- 明确定位「built for AI coding agents and programmability」
- **Unix socket API / CLI**：程序化 create/switch workspace、管理 split pane、发送 text/key-press
- 已有官方 Claude Code skill（`cmux-terminal-multiplexer-control`）通过 socket 切 session
- 子 agent/teammate 渲染成 native pane（多 session 天然可视化、可控）

### 来源
- https://github.com/warpdotdev/Warp/issues/3364 — Warp AppleScript 支持请求（未实现）
- https://github.com/warpdotdev/Warp/issues/9083 — Warp 暴露 tab config / 切换的请求（未实现）
- https://docs.warp.dev/terminal/more-features/uri-scheme/ — Warp URI scheme（只能开新）
- https://github.com/manaflow-ai/cmux — cmux 仓库
- https://cmux.com/ — cmux 官网

## 架构（已验证 2026-06-22，实测见下）

```
cardputer 选中 session
    │  固件 sendSelectSession(sid)   [BLE NUS]   sid = Claude session_id
    ▼
cc-bridge 收到 selectSession
    │  ① cmux surface.list → 找 resume_binding.checkpoint_id == sid 的 surface
    │  ② 拿该 surface 的 ref（surface:N）+ 所属 workspace/window ref
    ▼
cc-bridge 调 cmux CLI/socket：
    cmux focus-panel --panel surface:N [--workspace workspace:M --window window:K]
    ▼
cmux 把对应 surface（含其 workspace tab + window）切到前台
```

实测命令：cmux 二进制 `/Applications/cmux.app/Contents/Resources/bin/cmux`，
socket `~/Library/Application Support/cmux/cmux.sock`（亦 `~/.local/state/cmux/cmux.sock`）。
外部进程直接调 CLI 即可（`cmux trigger-flash --surface surface:1` → `OK`，无需密码，
当前 `socketControlMode` 默认放行本机调用）。`cmux rpc <method> [json]` 走原始 v2 socket。

## 验证结论（2026-06-22 实测，4 个开放问题已全部回答）

> 用户已迁到 cmux（本 Claude session 即跑在 cmux 内，hooks 全是 `cmux hooks claude ...`）。
> 以下结论来自直接调 cmux 二进制 + 读 upstream `docs/cli-contract.md`、`skills/cmux/SKILL.md`，非凭记忆。

1. **能否按 `session_id` 精确定位 pane？→ ✅ 能，且 cmux 原生暴露，无需推断。**
   `cmux rpc surface.list` 每个 surface 返回 `resume_binding.checkpoint_id`，其值**就是 Claude
   的 `--session-id`**（实测本 session = `41af42bb-...`，与 `--session-id`/`--resume` 一致），
   并带 `resume_binding.kind:"claude"`、`pane_ref`、`ref`、`workspace_id`、`window_id`。
   → 映射键 = `checkpoint_id`。bridge 不必从 `cwd`/`tty` 推断（开放问题 #3 也随之解决）。

2. **socket 协议 → 已确定。**
   - 二进制：`/Applications/cmux.app/Contents/Resources/bin/cmux`
   - socket：`~/Library/Application Support/cmux/cmux.sock`（`last-socket-path` 写明；
     `cmux capabilities` 另报 `~/.local/state/cmux/cmux.sock`）。可用 `--socket` / `CMUX_SOCKET_PATH` 覆盖。
   - 查询：`cmux rpc surface.list`（JSON）/ `cmux list-pane-surfaces` / `cmux tree --all`
   - 聚焦：`cmux focus-panel --panel surface:N [--workspace workspace:M --window window:K]`
     （`focus-panel` = surface focus 别名；另有 `focus-pane`、`select-workspace`、`focus-window`、
     rpc 层 `surface.focus`/`workspace.select`/`window.focus`）
   - 发文本/键（如需）：`cmux send` / `send-key` / rpc `surface.send_text`/`surface.send_key`
   - 鉴权：`--password` / `CMUX_SOCKET_PASSWORD` / Settings 里存的密码；本机默认放行——
     实测外部调用 `cmux trigger-flash --surface surface:1` → `OK surface:1 workspace:1`，无需密码。

3. **session_id ↔ pane 映射怎么建？→ 直接查，不必维护。**
   bridge 收到 `selectSession(sid)` 时**实时** `surface.list`，线性找 `checkpoint_id == sid`，
   取其 `ref` 调 `focus-panel`。无需 bridge 侧维护映射表（避免状态漂移）。
   注意：`checkpoint_id` 是 cmux 用来 `--resume` 的 Claude session id；只要 cmux 经 agent-hook
   记录了该 surface（`source:"agent-hook"`），映射就在。冷启动/手动起的 claude 若无 hook 记录则查不到——降级为忽略该次选择（见 tasks 4.x）。

4. **cc-bridge 与 cmux 分工 → 确认并存。**
   cc-bridge 仍是 cardputer 的 BLE 对端（状态镜像 + 审批），**新增唯一能力**：收 `selectSession`
   → 调 cmux `focus-panel`。cmux 不替代 cc-bridge；cmux 只被当作「终端 pane 切换器」调用。

## 数据需求（payload）

- **实测修正（2026-06-22）**：cc-bridge `to_payload` 此前**只有聚合** `total/running/waiting`，
  **没有** per-session 数组（探索期的「已有 sessions[]」假设不成立）。本 change 在 `to_payload` 新增
  `sessions:[{sid,running}]`：`sid` = `_sessions` 的 key = Claude `session_id` = cmux `checkpoint_id`。
- 字段从简：只 `sid + running`，**不带** label/tokens/ctx（详情回真终端看）。label 可留作后续。
- 固件只需解析 `sessions[]` 做可选中列表。
- buffer：`sid` 是完整 UUID（36 字符）≈ 45 B/条，输出**上限 16 条** ≈ 720 B；
  固件行缓冲 StickC `_LineBuf<1024>`、cardputer `g_line[2048]` 均够（避免「buffer 640 覆辙」）。
  不解析 `sessions` 的旧 buddy（StickC 等）由 ArduinoJson 自动忽略未知字段，不受影响。

## 与既有能力的关系

- 复用 `cardputer-feedback-channels` 的会话列表 UI（已有只读列表，加选中态）。
- 复用 baseline `session-overview` 能力（会话总数/运行数 + 列表）。
- 审批闭环（baseline `tool-approval`）保留——但有了终端切换后，复杂审批可以「跳到终端处理」，cardputer 审批退化为快速 once/deny。
