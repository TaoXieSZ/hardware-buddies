# Design — cardputer-codex-sessions

## D1. 架构：第三个 ext 源插进已有聚合层

```
                         ┌─────────────── 单一 BLE owner ───────────────┐
  Claude hooks ─► cc-bridge (本机会话, agent=claude)  ──► BLE ──► cardputer
                         ▲          ▲
   ext_sessions(cursor)  │          │  ext_sessions(codex)   ← 本 change 新增
                         │          │
  Cursor hooks ─► cursor-bridge   codex-bridge ◄─ Codex hooks
                  (推 sessions[])  (推 sessions[])
                                        │
                                        └─ cmux 对账(cwd) ◄─ cmux surface.list
```

- cc-bridge 不变地担任**唯一 BLE central**；`state.ext_sessions["codex"]` 是第三个分桶，`to_payload` 现有 merge 逻辑（`EXT_STALE_SEC=30s` 丢幽灵、全表 16 上限、ext 条目标自己 `agent`）**无需改动**即可纳入 codex。
- codex-bridge 与 cursor-bridge 同构：自己不连 BLE，best-effort 把 `sessions[]` 写到 cc-bridge socket（cc-bridge 没起就跳过）。

## D2. cwd join——与 Cursor 的唯一实质差异（已知局限）

| | Cursor | Codex |
|---|---|---|
| cmux pane title | `…· cursor-<UUID>` | 纯 `codex` |
| join key | UUID（hook `session_id` 去前缀 == title UUID），**精确** | **cwd**（hook `cwd` == pane `requested_working_directory`） |
| 聚焦 | `focus_by_cursor_sid(sid)` 按 title UUID | `focus_by_codex_cwd(cwd)` 按 pane cwd |
| 同 key 撞车 | 几乎不可能（UUID 唯一） | **可能**：同目录两个 codex → 合并成一条，无法分别聚焦 |

**为什么接受 cwd join**：cmux 根本不在 codex pane 上暴露任何 session-id（title 永远是 `codex`），UUID join 无从谈起。cwd 是 hook 与 cmux pane **唯一共有**的稳定标识。用户已确认走 cwd join、接受同目录撞车的局限（绝大多数实际用法是一目录一 codex）。

**撞车时的行为**：`_build_codex_sessions` 按 cwd 聚合，同 cwd 的多个 hook 桶取「最活跃/最近」一条的 st/ws（具体规则见 spec scenario）；列表只显示一条该 cwd 的 codex 会话。

## D3. codex-bridge 的事件派生（复用 cc-bridge 顺序）

Codex hook 已是 Claude 形状，`apply_event` 分支直接对齐 cc-bridge / cursor-bridge：

| Codex hook event | per-session st | 备注 |
|---|---|---|
| `SessionStart` | idle | 建桶 |
| `UserPromptSubmit` | thinking | 兜底建桶（若没收到 SessionStart） |
| `PreToolUse` / `PostToolUse` | tool | |
| `PermissionRequest` | waiting | **分配 FIFO seq**；权限门用 |
| `Stop` | idle | 回落 |
| `SessionEnd`（若 Codex 不 fire，靠 reaper TTL） | 桶移除 | 同 Cursor 的处理 |

钉（FIFO）跨三 agent 共用同一单调 `_wait_seq` 空间——Claude/Cursor/Codex 谁先进 waiting 谁先钉。

## D4. 权限门（PermissionRequest）

Codex 原生 `PermissionRequest` 事件优于 Cursor（Cursor 要拦 `beforeShellExecution`/`beforeMCPExecution` 两个具体门）。`codex_hook_permission.js`：
- 收到 PermissionRequest → 置该 cwd 会话 waiting + 推 ext_sessions → 阻塞等 cc-bridge 经 BLE 拿到设备按键决定 → 按 codex 权限协议回送 allow/deny。
- **gating spike #3 必须先确认 codex 权限 hook 的回送形态**（stdout JSON / exit code），再定 shim 写法。未确认前权限门标记为「待 spike」，不阻塞「显示/列表」主线。

## D5. 单 BLE owner 与部署

- 沿用 Cursor task 0.2 的「方案 b」：codex-bridge 不拥有 BLE，cc-bridge 单 owner。三 agent 状态全经 cc-bridge 一条 BLE 链路出去。**无新增 BLE 争用**。
- 部署位置：monorepo `claude-code-buddy/tools/codex-bridge/`（CI 测试）；线上运行实例的 checkout 位置在实现阶段定（沿用 cursor-bridge 的「第三 checkout」模式或直接跑 monorepo，见 `cursor-bridge-third-checkout` 记忆）。launchd `com.codex-bridge`，socket `/tmp/codex-bridge.sock`，日志 `~/Library/Logs/codex-bridge.log`。

## D6. 固件改动（最小）

`drawSessions`（`clawd_player.cpp`）现支持 `agent` ∈ {空/claude→`cc` 黄, cursor→`cu` 灰蓝}。新增 `codex`→`cx` + 区分色（建议绿/青系，与黄/灰蓝拉开）。`SessionInfo.agent[8]` 容量已够（"codex" 5 字符 < 7）。无线协议变更（`agent` 字段已存在）。其余轮播/钉/列表零改。
