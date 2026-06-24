# Tasks

> 探索阶段记录（2026-06-22）。方向已定（绑定 cmux）。前置批 0.x 已于 2026-06-22 全部完成
> （cmux API 实测，结论写入 design.md「验证结论」）。下一步从 §1 bridge 实现起。

## 0. 前置（切到 cmux 后第一批）—— ✅ 已完成 2026-06-22
- [x] 0.1 把日常 Claude 工作从 Warp 迁到 cmux（用户已迁；本 session 即在 cmux 内跑）
- [x] 0.2 调研 cmux socket API：二进制 `/Applications/cmux.app/Contents/Resources/bin/cmux`，
      socket `~/Library/Application Support/cmux/cmux.sock`；查 `rpc surface.list`、切 `focus-panel`（详见 design.md）
- [x] 0.3 开放问题 #1 = ✅ 能：`surface.list` 的 `resume_binding.checkpoint_id` 即 Claude session_id
- [x] 0.4 映射方案 = 实时查 `surface.list` 按 `checkpoint_id` 匹配，bridge 不维护映射表
- [x] 0.5 分工 = 并存：cc-bridge 管 BLE+状态+审批，新增「调 cmux focus-panel」单一能力

## 1. bridge（claude-code-buddy/tools，cc-bridge）—— ✅ 已实现 2026-06-22
- [x] 1.1 接收 `selectSession(sid)`：`buddy_core/core.py` `on_stick_line` 加 `cmd=="selectSession"` 分支，
      经注入的 `on_select_session` 回调分发；因 cmux 查询会阻塞，丢到 daemon 线程，不卡 BLE TX 回调
- [x] 1.2 收 sid → 实时查 cmux 匹配 `resume_binding.checkpoint_id == sid`：
      `control_plane/cmux_control.py` `CmuxClient.focus_by_checkpoint(sid)`（复用 `list_sessions()` 跨 window 枚举，
      `Session` 新增 `checkpoint_id` 字段）。**不维护**映射表（按 0.4 结论）
- [x] 1.3 聚焦目标 surface：复用 `CmuxClient._focus_argv` → `cmux rpc surface.focus`
      （= design 里 `focus-panel` 的等价 rpc，沿用既有 voice 路由已验证路径）；查无/rc≠0 → 返回 None，
      `cc-bridge/bridge.py` `_select_session` 记录日志忽略，不崩
- [x] 1.4 payload 带稳定 `sid`：`BuddyState.to_payload` 新增 `sessions:[{sid,running}]`
      （`sid` = `_sessions` 的 key = Claude `session_id` = cmux `checkpoint_id`）；空则省略，上限 16 防固件行缓冲溢出；
      wire 契约写入 `REFERENCE.md`（字段表 + 「Session switch」回送区）
- 测试：`tests/test_cmux_control.py`（checkpoint 提取 + focus 命中/查无/空）、`tests/test_buddy_core.py`
      （selectSession 分发 + sessions payload）；`pytest` 全套 **165 passed**

## 2. 固件（hardware-buddies/cardputer-adv-buddy）—— ✅ 已实现 2026-06-22（`pio run -e cardputer-adv` 通过）
- [x] 2.1 `cclink` 解析 payload `sessions[]`：`link_state.h` 加 `SessionInfo{sid,running}` + `BuddyState.sessions[16]/nSessions`；
      `cclink.cpp applyJson` 解析数组（`sid`+`running`；**无 label**——bridge to_payload 未给，对齐 §1.4 决定）；
      字段缺失 → 清零（bridge 无会话时省略该 key）
- [x] 2.2 会话列表 per-session + 选中态：`clawd_player` SESSIONS 视图从「显示 transcript entries」重构为
      per-session 列表（sid 前 8 字符 + run/idle 标），加选中高亮 + `sessionsMove(delta)`（viewport 跟随）+
      `sessionsSelectedSid()`；`showSessions(const BuddyState&)`
- [x] 2.3 `sendSelectSession(sid)`：`cclink.{h,cpp}` 新增，回送 `{"cmd":"selectSession","sid":...}`（仿 `sendDecision`）
- [x] 2.4 选中触发：`main.cpp` SESSIONS 键处理——`,/.` 移动选中、`enter`/`space` 确认 → `sendSelectSession` +
      toast `switch <sid8>` + nudge 音；`esc`/backtick 关

## 3. specs（cmux API 定了再写）
- [x] 3.1 `session-switch` capability spec（含选中 → 切 pane 的 Scenario）——已写
      `specs/session-switch/spec.md`（ADDED）+ `specs/session-overview/spec.md`（MODIFIED：只读→可选中）；
      `openspec validate cardputer-session-switcher --strict` 通过（2026-06-22）

## 4. 验证 —— ✅ 真机通过 2026-06-22
- [x] 4.1 真机端到端：cardputer 选中 + enter → cmux 对应 pane 跳前台。daemon 日志佐证
      `selectSession <sid> → focused surface <uuid>`，多次稳定。
- [x] 4.2 多 session：3 个 cmux claude 会话全部正确显示并可准确切换（用户真机确认）。
      验证用临时 daemon（cdb bridge.py，`CC_BRIDGE_DEVICE_PREFIX=Claude-` 通配，因 launchd plist
      前缀不含 cardputer）；BLE 首连有 bleak service-discovery 竞态，重连后自愈。

## 5. 会话列表 label（真机暴露的 UX 缺口）
> 调研结论：Claude Code 的 LLM 会话名（~/.claude/sessions `name`）**只给后台 agent（kind=bg）**，
> 交互会话（cmux 里跑的 inter）永远 None；唯一可用的可读名是 **cmux auto-name**（surface title）。
- [x] 5.1 bridge 数据侧（已做 2026-06-22，两仓同步）：
      `cmux_control.label_from_title` + `CmuxClient.session_labels()`（title→label，取中段/纯 auto-name）；
      `BuddyState.session_labels` + `to_payload` sessions[] 带 `label`（空则省略）；
      `cc-bridge cmux_label_loop` 每 15s 刷新（off-loop）。实测三会话 label 可读（hardware-buddies-setup 等）；
      `pytest` 171 passed。
- [x] 5.2 固件显示（已做 + 真机验证 2026-06-22）：`clawd_player drawSessions` 用 label 替代 sid8
      （无 label fallback sid8，`%.24s` 截断）。**关键修复**：`showSessions` 复制循环原先漏拷 label 字段
      （只拷 sid+running），导致 label 永远空 → 已补 `strncpy(sess_[i].label, ...)`。烧 firmware.bin 后真机
      三会话名字正确显示。
- [x] 5.5 sessions[] 数据源改 cmux（已做）：`to_payload` 列表优先用 `session_labels`（cmux 全部可切换会话），
      running 从 hook `_sessions` 补；无 cmux 源时 fallback `_sessions`。修复「只显示 hook 见过的会话」。
- [x] 5.3 daemon 持久化：launchd plist `CC_BRIDGE_DEVICE_PREFIX` 加 cardputer 前缀（已配 `Claude-7AFD`）。
- [x] 5.4 monorepo ↔ claude-desktop-buddy subtree 同步（§1 + §5 改动双份已同步并 push）。
- [x] 5.2 daemon 持久化：launchd plist 的 `CC_BRIDGE_DEVICE_PREFIX` 加 cardputer 前缀（已配 `Claude-7AFD`）。
- [x] 5.3 monorepo ↔ claude-desktop-buddy subtree 同步（§1 改动双份已同步并 push）。
