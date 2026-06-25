# Design — cardputer-session-rotation

## 三层数据流（现状 → 目标）

```
                现状（聚合）                          目标（per-session）
hook 事件       每条带 session_id                     不变（数据本来就在）
  │             core.py:1186
  ▼
daemon          collapse 成聚合：                     按 session_id 分桶：
  _sessions     {sid: {running}}                      {sid: {running, state, msg, waiting_since}}
  state         total/running/waiting/msg（聚合）      聚合保留 + per-session state 派生
  │
  ▼
wire payload    sessions[]={sid,label,running}        sessions[]={sid,label,running,st,ws}
                + 聚合字段                             + 聚合字段（兄弟 buddy 仍读）
  │
  ▼
firmware        deriveAgentState(聚合)→ 一个动画       轮播控制器：在 sessions[] 间轮，
                                                       播放 sessions[i].st 的动画 + label；
                                                       有 ws 的 → FIFO 钉
```

## 决策（draft 默认值，review 可翻）

### D1. daemon 按 session_id 分桶
- apply_event 已逐事件拿到 `session_id`；把状态变更路由进 `_sessions[sid]` 桶（而非只改聚合）。
- 每桶派生 `state ∈ {idle, thinking, tool, waiting, done}`——复用 firmware 同款派生顺序（thinking 在 tool 前）。
- `waiting_since`：桶进入 waiting（permission/question/notification）时打 `time.time()` 戳；离开清零。
- 聚合字段（total/running/waiting/msg）**保留**，由各桶汇总——兄弟 buddy（StickC 等）不解析 per-session，仍读聚合。

### D2. 审批 session_id plumb（spike 🟡 项）
- `hook_permission.py`：把 `ev["session_id"]` 加进发给 daemon 的 `req`（现在只用来拼 rid）。
- `_handle_wait_permission`：把 session_id 落到对应桶的 `waiting_since` + `state=waiting`，而非只设聚合 `state.waiting`。
- 问答路径无需改 plumb——`parse_pending_questions` 的 `sid` 现成。

### D3. wire 紧凑性
- `sessions[]` 每条加两字段，键名取短：`st`（state 单字符或短串，如 `t`hinking/`u`tool/`w`aiting/`d`one/`i`dle）、`ws`（waiting_since，epoch 秒或相对序号）。
- ≤16 session × ~20B ≈ 320B 增量。cardputer 行缓冲 2048 够；移植到 claude-code-buddy（行缓冲 1024）需复核（见 proposal 风险）。
- 用相对**序号**而非绝对时间戳做 FIFO 排序更省字节、且免去固件做时钟对齐——daemon 给每个 waiting 事件发一个单调递增 seq，firmware 按 seq 升序即 FIFO。**默认走 seq。**

### D4. 轮播控制器（firmware）
- 无覆盖层（非 approval/question/sessions/help）时启动；timer 驱动，每 `dwell` 切到下一个 session。
- 当前选中 session：`clawd::setState(sessions[i].st 派生的 AgentState)` + 屏上常驻显示**会话标识**（`label` 优先，缺则短 `sid` 前 6-8 字符）——即便单会话也标，让用户一眼知道是谁。
- **渲染布局（2026-06-24 定）**：
  - 顶栏左 = 会话标识 `label` + 轮播位置 `[2/5]`；顶栏右 = 既有 badge（T/R 总/运行）。clawd GIF 仍居中，状态=该会话 `st` 动画。
  - 钉态（有会话待输入）= 底部**专属横幅**「⏳ <label> waiting」+ 该会话 notification 动画，和普通轮播明显区分。
- **成员=全部 session**（用户定）。dwell 默认 ~2500ms。
- **稀释旋钮（D6）**：idle 会话 dwell 减半 / active 会话 dwell 加成——默认开一个轻权重，可调。

### D5. FIFO 钉输入（firmware）
- 每 tick 算「需要输入」集合：`st ∈ {waiting}` 或该 session 有 pending approval/question。
- 集合非空 → 不轮播，**钉 `ws`（seq）最小的那个**：显示它的 notification 动画 + label。
- 处理完（daemon 撤掉它的 waiting）→ 自动钉次小的；集合空 → 恢复轮播。
- 与覆盖层关系：审批/问答覆盖层全屏拍板时主形象不显示，钉主要作用于 notification 态与「下一个该看谁」。多覆盖层排队形态留实现期。

### D6. 稀释缓解（用户已知的 tradeoff）
- 「全部都轮」下 6 个 session 只 1 个忙 → 那个忙的每 ~15s 才露 2.5s。
- 缓解（可调，不改用户「全部都轮」的决定）：idle dwell 短（如 1s）、active dwell 长（如 3s）；或 active 会话插队提高频率。默认给 idle=1s / active=3s，user tune。

## Spike 结论（2026-06-24，已验）

| 路径 | 现状 | 归属 |
|---|---|---|
| 问答 question | `parse_pending_questions` 带 `sid=workstream_id`（cmux_control.py:372） | ✅ 现成 |
| 审批 permission | hook 有 session_id（hook_permission.py:95），未下发；daemon 设聚合 waiting | 🟡 plumb 一字段 |
| per-session 状态 | 每个 hook 事件带 session_id（core.py:1186），collapse 成聚合 | 🟡 按桶存 |

无 blocker。

## 跨层同步（root CLAUDE.md sibling 规则）

- daemon 改动同步 monorepo 镜像 + 线上 `claude-desktop-buddy`（见 [[cc-bridge-daemon-source-of-truth]]）。
- wire 加字段对兄弟 buddy 向后兼容（不解析 `st`/`ws` 即忽略）；但 claude-code-buddy 行缓冲 1024 的体积复核要做。
