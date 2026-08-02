# tasks.md — kimi-session-identity

## 1. hook 来源标记

- [x] 1.1 `tools/cc-bridge/hook.py`：支持 `--agent <name>` argv（env `CC_BRIDGE_AGENT` 兜底），转发前注入 `"agent"` 字段；无参数时行为逐字节不变
- [x] 1.2 单测：hook.py 注入/不注入两条路径（mock socket）
- [x] 1.3 `apply_event` 把 `ev["agent"]` 记入 `_sessions[sid]["agent"]`（缺失时不动已有值）+ 单测

## 2. payload 透出 agent

- [x] 2.1 `to_payload` 无标签条目携带 `agent`（有值时）；Claude/未知省略 + 单测（对应 spec 三个场景）
- [x] 2.1a `to_payload` Kimi 条目以 state.json `title` 为 `label`（sid8 回退无法区分 Kimi 会话）+ 单测
- [x] 2.2 `~/.kimi-code/config.toml` 的 cc-bridge hooks 块命令加 `--agent kimi`（先备份，`kimi doctor config` 验证）

## 3. Kimi 聚焦

- [x] 3.1 `CmuxClient.focus_by_kimi_sid(sid)`：`state.json` 的 `title` 精确匹配 pane 标题，`cwd` basename 唯一匹配兜底（纯函数 `kimi_session_meta`；单测：标题命中/无标题 cwd 兜底/同 cwd 歧义/目录缺失）
- [x] 3.2 `bridge.py` `_select_session` 匹配链末尾追加 `focus_by_kimi_sid` + 单测
- [x] 3.3 `make test-py` 全绿，kickstart daemon

## 4. 固件标记 + 真机

- [x] 4.1 cardputer-adv-buddy `clawd_player.cpp` agent 映射加 `kimi` → `ki`（青色系），`pio run -e cardputer-adv` 编译
- [x] 4.2 ROM 下载模式重刷固件（只烧 bootloader/partitions/firmware 三镜像，littlefs 不变）
- [x] 4.3 真机验证：列表里 Kimi 会话显示 `ki` 标；点选 Kimi 会话 → cmux 对应 pane 聚焦（daemon 日志 `selectSession ... → focused surface`）
