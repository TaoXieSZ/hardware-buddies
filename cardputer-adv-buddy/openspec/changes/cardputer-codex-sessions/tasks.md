# Tasks

> 状态：proposal 草拟（探索产物），**未实现**。等 spec 审批后开工。
> 复用 `cardputer-cursor-sessions` 的聚合层（cc-bridge `ext_sessions[agent]`）与固件 agent 标记机制——多数活在 codex-bridge 本体 + cmux cwd 对账。

## 0. Gating spike（开工前必须验）✅ 全绿（2026-06-25，隔离 CODEX_HOME + `codex exec` headless 真抓）
- [x] 0.1 ✅ 真抓 codex 各事件 stdin JSON（隔离 `CODEX_HOME` + debug logger + `codex exec -s read-only`，不碰用户真 hooks.json）。确认：
  - **每个事件都带 `session_id`（完整 UUIDv7，如 `019f0287-b7a0-70e2-...`）+ `cwd`**；`hook_event_name` 就是精确 Claude 名
  - 实测 fire 顺序：SessionStart(`source`) → UserPromptSubmit(`prompt`) → PreToolUse(`tool_name`/`tool_input`/`tool_use_id`) → PostToolUse(+`tool_response`) → Stop(`stop_hook_active`/`last_assistant_message`)
  - 公共字段还含 `turn_id`/`transcript_path`/`model`/`permission_mode`；`tool_name` 走 Claude 风格（实测 `Bash`），`tool_input={command,...}`
  - **未 fire `SessionEnd`**（同 Cursor）→ 桶移除靠 reaper TTL
- [x] 0.2 ✅ cwd join 坐实：codex hook `cwd` == cmux codex pane `requested_working_directory`，**字节级相等**（实测均为 `/Users/txie/OpenSourceProjects/hardware-buddies`）→ 可作 join key
- [x] 0.3 ✅ `PermissionRequest` 真 fire（只读沙箱里跑写命令触发）：payload 带 `session_id`+`cwd`+`tool_name:"Bash"`+`tool_input:{command,description}`。**回送契约 = Claude Code 同款 stdout JSON**：deny=`{"decision":"block","reason":...,"systemMessage":...}`，allow=返回空（默认放行）。实证来源：codex 的 hook 消费者 `oh-my-codex/dist/scripts/codex-native-pre-post.js` 用 `decision:"block"`+`hookSpecificOutput.hookEventName` block 工具 → `codex_hook_permission.js` 可照搬 `cursor_hook_permission.js`
- [x] 0.4 ✅ Codex hooks.json 事件名原生 Claude 形状 → apply_event 近照搬，免事件名翻译层（比 Cursor 更省：`codex_hook.js` 近 identity 转发）
- [x] 0.5 ✅ cmux codex pane title 恒为 `codex`、无 UUID，但带 `requested_working_directory` → 必须 cwd join（非 UUID join）
- 留档：`scratchpad/codex-hook-capture.jsonl`（5 事件）+ `codex-hook-capture-perm.jsonl`（含 PermissionRequest 全 payload）。隔离 home 已删（含 auth.json 拷贝，安全清理）

## 1. codex-bridge 本体（monorepo `claude-code-buddy/tools/codex-bridge/`）✅ pytest 12 绿 + 全量 211 无回归
- [x] 1.1 `tools/codex-bridge/bridge.py`：镜像 cursor-bridge 骨架（listen socket → `apply_event` → 状态桶 → reaper TTL）。apply_event 复用 buddy_core，派生分支对齐 D3（SessionStart=idle〔Codex 原生 fire，cursor 没有〕/ UserPromptSubmit=thinking / Pre+PostToolUse=tool / PermissionRequest=waiting+FIFO+command hint / Stop=idle+last_assistant_message）。每事件把 `cwd` 存进会话桶供 cwd join
- [x] 1.2 `tools/codex-bridge/codex_hook.js`：near-identity shim（Codex 已 Claude 形状，白名单 `session_id`/`cwd`/`tool_name`/`tool_input`/`prompt`/`last_assistant_message` → 写 `/tmp/codex-bridge.sock`）。实测无 socket 也 exit 0（fire-and-forget）；`node --check` 过
- [x] 1.3 单测 `tests/test_codex_bridge.py`（+ conftest `codex` fixture）：`test_per_session_state_transitions`（idle→thinking→tool→waiting+FIFO→idle）+ `test_per_session_state_in_payload` + SessionStart/Stop/last_assistant_message

## 2. 聚合（基本免费——复用 cc-bridge ext_sessions）✅ 推送侧落地（daemon code 同在 bridge.py）
- [x] 2.1 codex-bridge `push_ext_sessions_loop`（每 2s）→ 写 `{action:"ext_sessions",agent:"codex",sessions:[...]}` 到 cc-bridge socket（best-effort，cc-bridge 没起就跳过）。`_build_codex_sessions` 从状态桶 + cmux labels 直建
- [ ] 2.2 验证 cc-bridge 现有 `ext_sessions[agent]` merge 对 `agent="codex"` 即插即用（理论无需改 cc-bridge；若有 agent 白名单/上限假设则补测 `test_ext_sessions_codex_merge_into_payload` + stale 丢弃）—— 留到 task 4/cc-bridge 侧一起验

## 3. cmux cwd 对账 ✅ daemon 侧落地（自包含 `_cmux_codex_panes`，无 control_plane 依赖）
- [x] 3.1 `_cmux_codex_panes()`（bridge.py 内自包含，仿 cursor 的 `_cmux_cursor_panes`）：列活 codex surface（title 含 `codex` 且非 Claude〔无 claude resume_binding〕非 Cursor〔title 无 `cursor-`〕→ key=`requested_working_directory`，label 取 cwd 末段）。测试 `test_cmux_codex_panes_parses_live_panes`（跳 Claude/Cursor pane）
- [x] 3.2 codex-bridge `cmux_codex_label_loop`（15s 轮询）填 `state.session_labels`={cwd:label}；`_build_codex_sessions` **以 cmux 活 pane 为准**、按 cwd join hook st/ws，僵尸（无活 pane 的 cwd）排除；同 cwd 多桶取最近活跃。测试 `test_build_codex_sessions_joins_by_cwd` + `test_build_codex_sessions_same_cwd_merges_latest`
- [x] 3.3 用户约束「只看 cmux 活会话」：无 cmux pane 不列、cmux 不可达则空（不 hook 兜底）。测试 `test_build_codex_sessions_only_cmux_panes`（labels 空 → []）

## 4. cc-bridge focus（cwd）✅ pytest + ext merge 验证
- [x] 4.1 `cmux_control.focus_by_codex_cwd(cwd)`：自包含扫 surface.list，按 `requested_working_directory==cwd` 匹配 codex pane（跳 Claude〔checkpoint〕/ Cursor〔cursor-〕）→ focus surface id。测试 `test_focus_by_codex_cwd_matches_dir_and_focuses` + no-match/空-短路
- [x] 4.2 `_select_session` 回退链追加 codex 分支：checkpoint → cursor_sid → 失败则查 `state.ext_sessions["codex"]` 行拿 sid→cwd → `focus_by_codex_cwd`（sid 用 UUID 不进 cmux title，靠 cwd focus）
- [x] 4.3 验 cc-bridge `ext_sessions` merge 对 `agent="codex"` 即插即用 + `cwd` 字段穿透：`test_ext_sessions_codex_merges_alongside_cursor`（含 cursor 并存、cwd 保留）。**无需改 cc-bridge merge**

## 5. 固件 agent 标记 ✅ 编译 + 真机烧录（2026-06-26）
- [x] 5.1 `drawSessions`：3-way `agent` 标记 claude=黄`cc`(0xFD20) / cursor=灰蓝`cu`(0xCE59) / codex=绿`cx`(0x07E5)。`SessionInfo.agent[8]` 够（"codex"=5）
- [x] 5.2 核对：`cclink` 解析任意 `agent` 字符串（cursor 那轮已通用）+ `sid[40]` 容得下 codex UUID(36)——故 bridge 侧 sid 用 UUID 不用 cwd（避免 40 截断），cwd 走独立字段
- [x] 编译 SUCCESS（Flash 43.6% / RAM 25.2%）→ 烧 `/dev/cu.usbmodem21401`，Hash verified + hard reset

## 6. 验证（单测全绿 + 真机通关 2026-06-26）
- [x] 6.1 单测全绿：codex-bridge + cmux_control codex focus(含 suffix) + cc-bridge ext codex → 无回归
- [x] 6.2 ✅ 真机：cmux 跑 codex pane → cardputer SESSIONS 列表出现**绿色 cx 行**（label=hardware-buddies）。用户确认「看到绿色 cx 行」。注意：ext 会话进 `sessions[]` 列表但不改 `total` 计数（与 cursor 同行为）
- [x] 6.3 ✅ 真机：注入 codex PermissionRequest（经真实 `codex_hook.js → socket → daemon → BLE`）→ codex-bridge `waiting=1 msg=approve: Bash`，设备 **clawd 举灯泡（attention/waiting）**，用户确认；Stop 后回落 idle。设备行为验证（waiting→attention + 钉）。⚠️ codex **自动** fire hook 需先在 codex 里 approve hook-trust（codex 侧门，非本代码 bug；bridge 管线手动探针已证完整）。codex trust 哈希算法不可逆推，留交互批准 / `--dangerously-bypass-hook-trust`
- 注：codex-bridge 顶层 `state.waiting/prompt` 在 Stop 后不自清，但 no_ble push-only 不发顶层字段、只发 `sessions[]`（行 st 正确随 Stop 转 idle），故对设备无影响——不修
- [x] 6.4 ✅ 真机：选中 cx 行 → cmux focus 到 `codex resume…--yolo` pane。日志 `selectSession sid=xie/OpenSourceProjects/hardware-buddies → focused surface D8E25330`。用户确认「跳了」

## 8. 真机 bring-up 暴露并修复（2026-06-26）
- [x] 8.1 **BLE 三扫描器争用**：codex-bridge 作为第 3 个 bleak 扫描器（扫不存在的 `Codex-*`）挤掉 cc-bridge 的 cardputer 连接 → connect-then-drop flapping（串口 `[ble] connected`→`mtu=`→`conn=0`）。修：`buddy_core.run()` 加 `no_ble` 参数 + `_NullBleWriter`（push-only bridge 不建 BleWriter、不跑 reconnect_loop）；codex-bridge 传 `no_ble=True`。停掉 codex-bridge 即 `conn=1` 稳，加 no_ble 后三 daemon 共存稳定。**根因=环境/总线争用，非 firmware**
- [x] 8.2 **focus 崩溃 `NameError: state`**：`_select_session` 闭包够不着 `state`（state 在 buddy_core.run() 内建）。原 codex 分支查 `state.ext_sessions` 拿 cwd → 崩。修：改无状态——codex sid **本身就是 cwd**（超 39 字符取尾段，适配固件 `sid[40]`），`focus_by_codex_cwd` 按 `cwd==sid or endswith(sid)` 匹配。`_select_session` 链：checkpoint → cursor_sid → codex_cwd(sid)
- [x] 8.3 **dashboard import 崩**：codex-bridge 独立运行时自己目录无 dashboard.py（测试时靠 cursor-bridge 先加载留在 sys.path 才没暴露）。修：dashboard import 改 try/except 可选 + `_on_loop_start` guard
- [x] 8.4 部署：codex-bridge 跑在 `claude-desktop-buddy/tools/codex-bridge/`（与 cc-bridge 同 checkout，socket pair）；install.sh 已合 `~/.codex/hooks.json`（7 事件，其它 17 hook 完好）。cc-bridge focus + cmux_control + core.py(no_ble) 已同步线上 claude-desktop-buddy 并 reload

## 7. 部署产物 + 跨层同步
- [x] 7.1 部署产物（monorepo）：`com.codex-bridge.plist.template` + `install.sh`（venv+plist+`~/.codex/hooks.json` 嵌套 schema 合并）+ `README.md`。install.sh 的 jq 合并已在**真实 hooks.json 副本**上验证：输出合法、7 事件各加 1、其它工具 17 hook 完好、幂等。**未碰线上文件**
- [ ] 7.2 明天 bring-up：跑 install.sh（装 codex-bridge daemon + 合 ~/.codex/hooks.json，需在 codex 里 approve hook trust）；把 cc-bridge focus_by_codex_cwd + cmux_control 改动同步 `claude-desktop-buddy` 并 reload cc-bridge（reload 有 BLE half-open 风险，故留到有人在场时做）

## 明天验收 runbook
1. **同步 cc-bridge 改动到线上**：把 monorepo 的 `tools/cc-bridge/bridge.py`(_select_session codex 分支) + `tools/control_plane/cmux_control.py`(focus_by_codex_cwd) 同步到 `claude-desktop-buddy/`，`launchctl kickstart -k gui/$(id -u)/com.cc-bridge`（若设备变砖 = BLE half-open，断电重开设备 / 切 Mac 蓝牙）
2. **装 codex-bridge**：`claude-code-buddy/tools/codex-bridge/install.sh`（或先同步到运行 checkout 再装）。在 codex 里跑一次、**approve codex_hook.js 的 trust 提示**
3. **开 codex pane**：cmux 里起一个 Codex 会话，~15s 后设备会话列表出现绿 `cx` 行
4. **验三项**：三 agent 列表(cc/cu/cx) / Codex waiting 钉 / 选中 Codex → focus 对应 cmux pane

## 后续（不在本轮，按需）
- 权限门真机闭环 `codex_hook_permission.js`（依赖 spike 0.3 已确认的 `{"decision":"block"}` 回送协议）
- 同 cwd 多 codex 区分（cmux 暂不提供 id，长期需 cmux 侧支持）
