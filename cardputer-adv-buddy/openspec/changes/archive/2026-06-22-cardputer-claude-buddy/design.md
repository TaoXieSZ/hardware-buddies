## Context

`cardputer-coding-pet` 已让 clawd GIF 跑在 Cardputer-ADV 上（LittleFS + AnimatedGIF），但状态靠键盘自测、没接真实 Claude。本 change 把它接上 macOS 上常驻的 `cc-bridge`，成为一个真正连着 Claude Code 的桌搭：clawd 随真实会话动、屏上审批、键盘拍板、看多会话。

复用对象（都在 monorepo 内）：
- `../claude-code-buddy/src/ble_bridge.{h,cpp}` —— BLE NUS 服务端实现（service `6e400001-…`，RX `…0002` write，TX `…0003` notify），行分隔 JSON。
- `../claude-code-buddy/src/main.cpp` 的 `_applyJson` —— 状态 JSON 字段语义与审批 UI 范式。
- `../claude-code-buddy/tools/buddy_core/core.py` `BuddyState` —— `total/running/_sessions/prompt/waiting/session_ms` 字段。
- 本仓 `clawd_player`（显示层）、`state_source.h`（状态源抽象）。

硬约束：ESP32-S3FN8，**512KB SRAM，无 PSRAM**；BLE 走 BLE→Mac bridge，固件不碰 WiFi/TLS。

## Goals / Non-Goals

**Goals**：固件作 `Claude-` NUS 设备被 cc-bridge 驱动；clawd 由真实状态驱动；审批看得见+键盘拍板（ok/esc/a，超时 ask）；多会话只读总览。

**Non-Goals**：Cursor 变体、session 切换/操作、自由文本理由、WiFi/OpenClaw、重做 clawd 渲染（见 proposal Non-goals）。

## Decisions

### D1：走 BLE NUS，且暴露 cc-bridge 实际连接的「debug」服务
- **选择**：固件以 `Claude-XXXX` 广播，提供 NUS。**关键**：`cc-bridge/bridge.py` 注释明确「talks to the firmware's debug service (unencrypted) instead of the encrypted NUS that Claude Desktop uses；firmware mirrors notifies to both characteristics」——即固件需同时提供加密 NUS（给 Claude Desktop/Web）与未加密 debug 通道（给 cc-bridge），notify 镜像到两者。
- **理由**：要被现成 cc-bridge 零改动连上，就得匹配它连的那一路。apply 时对照 `ble_bridge.cpp` 逐字确认两个 characteristic 的 UUID 与加密属性。
- **备选**：只做加密 NUS → cc-bridge（默认走 debug）连不上，被否。

### D2：状态来源换成 BleStateSource，组合根不动
- 新增 `BleStateSource : StateSource`，内部持有 BLE 链路，`state()`/`consumeChanged()` 由收到的状态 JSON 驱动。main 把 `KeyboardStateSource` 换成它即可（state_source.h 抽象当初就是为这一步留的）。
- 键盘从「切状态」改作「审批/导航输入」（见 D5）。

### D3：JSON 解析复用字段语义
- 用 `ArduinoJson` 解析行 JSON。读取 `total/running/_sessions/prompt/waiting`。会话状态→clawd GIF 的映射沿用 clawd_player 现有 `fileForState`，把「busy/attention/celebrate…」语义对齐 bridge 状态。具体状态字符串以 `_applyJson` + `BuddyState.to_dict()` 为准（apply 时核对）。

### D4：审批 UI 作「覆盖层」，不与 clawd 绘制线程抢屏
- clawd_player 用独立 sprite + push。审批/会话视图作为**模式覆盖**：进入审批态时暂停 clawd 全屏 push，改画审批面板（clawd 缩角标或暂隐）。退出后恢复。避免两个绘制源争屏（教训同 cardputer-coding-pet 的 avatar 线程问题）。

### D5：键盘语义（审批/导航）
- 状态态：`↑↓` 或某键 → 打开会话列表；任意非保留键无副作用。
- 审批态：`ok`=approve once / `esc`=deny / `a`=always；超时窗口内无键 → 不发（回落 ask）。
- 会话列表态：`↑↓` 滚动，`esc` 返回。

### D6：决定回送格式对齐 bridge 解析器
- 审批决定经 NUS RX 写回 bridge，格式 MUST 与 bridge 期望一致（`once/always/deny`，参 `hook_permission.py` 的 `DECISION_MAP` 与固件回送行）。**apply 时逐字核对** `ble_bridge` 回写 + `core.py` 解析，不凭记忆造字段。

## Risks / Trade-offs

- **R1：512KB 无 PSRAM 下 BLE 栈 + clawd sprite(64KB) + JSON 堆是否够** → apply 第一步起最小 BLE 回显，串口打印 `ESP.getFreeHeap()`；紧则审批态临时释放大 sprite、会话列表用流式行渲染不缓存全量。
- **R2：debug vs 加密 NUS 连错路** → D1，对照 ble_bridge.cpp 的 characteristic 定义；先只保证 cc-bridge（debug）能连，加密路作兼容项。
- **R3：决定回送字段不匹配 → bridge 收不到/误判** → R2 同源核对；端到端测「按 ok → 终端真放行」作为验收。
- **R4：clawd 绘制线程 vs 审批覆盖层争屏** → D4 模式互斥，进审批暂停 clawd push。
- **Trade-off**：走 BLE→Mac bridge 而非直连，牺牲 untethered，换零网络栈固件 + 复用全家桶基建 + RAM 宽裕。untethered（WiFi/OpenClaw）是另一条独立后续。

## Open Questions

- bridge 状态 JSON 里「会话状态」的确切字符串/字段（busy/idle/waiting 如何编码）→ apply 读 `_applyJson` + `BuddyState.to_dict()` 定。
- 审批决定回送的确切行格式（是 `{"decision":"once"}` 还是别的）→ apply 读 `ble_bridge` 回写路径定。
- `_sessions` 条目结构（除 `running` 外有无可显示的 label/nickname）→ 决定会话列表每行显示什么。
