# kimi-session-identity

## Why

Kimi Code 会话自 2026-08-02 接入 cc-bridge 后有两个真机可见的缺陷：设备列表里被缺省标成 `cc`（Claude 黄标）——因为本地 hook 通道不区分事件来源；`selectSession` 点选后无法聚焦——`CmuxClient` 的聚焦匹配只覆盖 Claude（checkpoint_id）/Cursor/Codex/OpenCode pane。用户无法区分 Kimi 会话，也不能从设备跳进 Kimi 终端。

## What Changes

- **hook 事件携带来源**：`tools/cc-bridge/hook.py` 支持 `--agent <name>` 参数（或 `CC_BRIDGE_AGENT` env），转发前把 `agent` 字段注入事件 JSON；`~/.kimi-code/config.toml` 的 cc-bridge hooks 块改为传 `--agent kimi`（Claude 的 `~/.claude/settings.json` 不动，缺省 = claude）。
- **daemon 记录并按会话透出 agent**：`apply_event` 把事件的 `agent` 存进 `_sessions[sid]`；`to_payload` 的无标签会话条目携带 `agent` 字段（有标签的 Claude 会话保持缺省空，不占字节）。
- **Kimi pane 聚焦**：`CmuxClient` 新增 `focus_by_kimi_sid(sid)`：读 `~/.kimi-code/sessions/*/<sid>/state.json`，用其 `title`（Kimi 会把 pane 标题设为会话首条消息）精确匹配 cmux pane，`cwd` basename 唯一匹配兜底；`bridge.py` 的 `_select_session` 在现有四路匹配后追加该回退。
- **固件标记**：cardputer-adv-buddy `clawd_player.cpp` 的 agent 映射表加 `kimi` → `ki` 标（新颜色），固件需重刷一次。

## Capabilities

### New Capabilities

- `session-focus`: `selectSession` 的 sid → cmux surface 匹配规则——Claude checkpoint_id、Cursor sid-in-title、OpenCode、Codex cwd、Kimi 会话目录回退，及无匹配时的 logged no-op 语义。

### Modified Capabilities

- `session-list-payload`: 无标签会话条目在来源非 Claude 时 SHALL 携带 `agent` 字段；事件来源标记的注入与存储规则。

## Impact

- 代码：`tools/cc-bridge/hook.py`、`tools/cc-bridge/bridge.py`（`_select_session`）、`tools/buddy_core/core.py`（`apply_event`/`to_payload`）、`tools/control_plane/cmux_control.py`（`focus_by_kimi_sid`）；cardputer-adv-buddy `src/clawd_player.cpp`（一行映射）+ 重刷固件。
- 配置：`~/.kimi-code/config.toml` hooks 命令加 `--agent kimi`。
- 协议：`sessions[]` 无标签条目新增可选 `agent` 字段——固件已按可选字段解析，旧固件兼容（显示回退 `cc`）。
- 跨项目：固件改动在 cardputer-adv-buddy（不受 OpenSpec 管辖，作为本 change 的实施任务跟踪）。
