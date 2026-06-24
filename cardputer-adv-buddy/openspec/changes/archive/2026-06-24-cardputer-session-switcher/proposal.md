> **🔬 探索 → specs 已就绪（2026-06-22）** — 方向已定（绑定 cmux）。cmux socket API 已实测，
> 4 个开放问题全部回答（见 design.md「验证结论」），`session-switch` capability spec 已写并通过
> `openspec validate --strict`。**实现待续**：bridge §1 + 固件 §2（见 tasks.md）。

## Why

cardputer 到目前为止是个「越来越好的镜子」——被动反映 Claude 会话状态。但它最该发挥的，是 terminal 难做的事：**物理快速切换 session**。

理想交互：在 cardputer 上点选某个 session → Mac 上对应的终端 tab/pane 立刻跳到前台。这把 cardputer 从「小屏镜子」变成「**物理 session 切换器**」，真正用上它「键盘 + 屏」相对 buddy 全家桶的独特牌；同时**避免在 240×135 小屏上重复 terminal 已经做得很好的事**——看 prompt 详情、审批操作，这些选中后回到真终端去做。

关键 reframe（来自本次 explore）：先前设想过让 cardputer 显示 per-session prompt 详情 + 审批面板，但用户指出「我最终要在 terminal 里看（当前 session 就跑在 Warp 里）」——所以 cardputer 不该显示详情，而该当**选择器**：点中就跳过去。

## What Changes

- cardputer 会话列表升级为**可选中**：解析 bridge payload 里已有的 `sessions[]`（per-session `sid`/`label`/`running`，固件目前没解析），上下键选中。
- 点选 session → 固件发 `selectSession(sid)` → cc-bridge 调可编程终端的 API，把对应 pane/tab 切到前台。
- cardputer **不再需要** prompt 详情 / 审批面板——选中即跳转到真终端操作。

## 调研结论：绑定 cmux（Warp 做不到）

| 终端 | 能否程序化聚焦特定 session 的 tab/pane | 依据 |
|---|---|---|
| **Warp** | ✗ 做不到 | 无 AppleScript（Issue #3364 长期未实现）、无 Shortcuts；URI scheme 只能【开新 tab/窗口】，没有任何 CLI/URI/IPC 聚焦【已存在的特定 tab】（Issue #9083 请求中未实现）|
| **cmux** | ✓ 能做到 | manaflow-ai/cmux，Ghostty-based macOS 终端，专为 AI agents + 可编程性而造；Unix socket API/CLI 可 switch workspace/pane、发送 text/keys；已有官方 Claude Code skill |

**决策：本功能绑定 cmux**（manaflow-ai/cmux）。用户将把日常 Claude 工作从 Warp 迁到 cmux 后继续实现。

## Non-goals

- 不在 cardputer 显示 prompt 详情 / 审批面板（选中后回真终端做）。
- 不支持 Warp（缺程序化 tab 聚焦 API，社区催了 4 年未给）。
- 不做 nudge 全局键盘注入的「定向到某 session」路由（先前探到的硬卡点；用「终端切换」从根本上绕开）。

## Capabilities（待 cmux API 调研后细化）

### New Capabilities
- `session-switch`: cardputer 会话列表可选中，点选 → cc-bridge 调 cmux socket API 把对应 pane 切到前台。

## Impact

- **固件**（hardware-buddies/cardputer-adv-buddy）：`cclink` 解析 `sessions[]`；`clawd_player` 会话列表加选中态；新增 `sendSelectSession(sid)` BLE 命令。
- **bridge**（claude-desktop-buddy）：收 `selectSession` → 调 cmux socket API 聚焦对应 pane；需建立 Claude `session_id` ↔ cmux pane 的映射。
- **依赖**：cmux（manaflow-ai/cmux）socket API。
- **payload**：`sessions[]` 已有 `sid/label/running`，session-switch 不需要 prompt 详情进 payload（回真终端看），所以**不会撑大 payload**（避免重蹈 buffer 640 覆辙）。
- **开放问题**（切 cmux 后调研）：见 `design.md`。
