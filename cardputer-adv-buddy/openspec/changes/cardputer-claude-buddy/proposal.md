## Why

clawd 现在只是被动镜子——开机循环播 idle，状态还得靠键盘自测假装。它没接真实的 Claude Code 会话，也没用上这台机器相对全家桶唯一的两张牌：**键盘**和**够大的屏**。

把它变成一个**真正连着 Claude Code 的桌搭**：clawd 随真实会话状态动起来，工具要审批时屏上**看得见在批什么**、键盘 `ok`/`esc` 当场拍板，还能一眼看到**有几个会话在跑/在等批**。审批和多会话导航正是 2 键的 StickC 干不了、Cardputer 键盘+屏天生能干的事——这是这台机器在 buddy 家族里存在的理由。

## What Changes

- **接上现成的 cc-bridge**：固件广播 `Claude-XXXX`，被 macOS 上常驻的 cc-bridge 守护进程按前缀连上（BLE NUS，复用全家桶现成协议），收 `{total,running,_sessions,prompt,waiting,...}` 状态 JSON。
- **状态层**：把收到的会话状态映射到 clawd GIF（取代 Phase 1 的键盘自测），屏角显示 `N sess · M running` 角标。
- **审批层**：bridge 置 `waiting` 时，屏上弹出待审批的工具+参数（来自 `prompt`），键盘 `ok`=approve(once) / `esc`=deny / `a`=always；不按则超时 → ask（回落到终端原生提示）。决定经 NUS 送回 bridge → hook 返回 Claude Code 的 allow/deny/ask。
- **会话层（MVP 只读）**：一屏可滚动列表，显示各 session 在跑/等批，键盘 `↑↓` 翻看。

## Non-goals

- **不做 Cursor**（本次）：固件按品牌编译（`BUDDY_BRAND_PREFIX`），先出 `Claude-` 变体；Cursor 复用同源码换前缀，作后续变体，不在本次范围。
- **不做 session 切换/操作**：MVP 只读看，切换具体 session、给它发指令需要 bridge 侧 `target` 路由配合，留后续。
- **不做"打理由"审批**：审批只到 approve/deny/always 三档（够到协议已有的 once/always/deny），自由文本理由留后续。
- **不重做 clawd 渲染**：clawd 显示层已在 `cardputer-coding-pet` 实现，本次直接复用，只换驱动源（BLE 取代键盘）。
- **不接 WiFi/TLS/OpenClaw**：本桌搭走 BLE→Mac bridge，固件不碰网络栈（512KB SRAM 无 PSRAM，把网络留给 Mac）。

## Capabilities

### New Capabilities
- `ble-claude-link`: 固件以 `Claude-XXXX` 广播并被 cc-bridge 连上，解析其 BLE NUS 状态 JSON，驱动 clawd 状态与会话计数。
- `tool-approval`: `waiting` 时渲染待审批工具+参数，键盘 ok/esc/a 给出 approve/deny/always 决定并经 NUS 回送，超时回落 ask。
- `session-overview`: 显示会话总数/运行数与一屏只读可滚动会话列表（哪个在跑/等批）。

### Modified Capabilities
<!-- 本 change 的 specs 是新建项目能力；clawd 显示层在 cardputer-coding-pet，未在本仓 openspec/specs/ 固化，故此处不列 delta。状态来源从键盘自测换成 BLE 在 ble-claude-link 中描述。 -->

## Impact

- **代码**：新增 `src/ble_link.{h,cpp}`（NUS 客户端 + 广播 + JSON 解析，参照 `../claude-code-buddy/src/ble_bridge.*` 与 `_applyJson`）、`src/approval.{h,cpp}`（审批 UI + 键盘决定）、`src/sessions.{h,cpp}`（会话列表视图）。`state_source.h` 增 `BleStateSource` 实现替换 `KeyboardStateSource`，main 组合根改接 BLE。clawd_player 复用，增"角标/审批不遮挡主区"的布局。
- **依赖**：BLE 用 ESP32 内置 NimBLE/Arduino BLE（M5/Arduino 自带），可能加 `ArduinoJson`（解析状态 JSON）。
- **协议对端**：macOS 上的 `cc-bridge`（已存在）。注意它也是占用 Cardputer 串口的那个守护——见 `cardputer-coding-pet/design.md` 记录的「串口所有权」约束：本桌搭走 BLE 不碰串口，烧录时只需对付 S3 USB-JTAG（下载模式）。
- **内存**：无 PSRAM。审批/会话文本走小缓冲；clawd sprite 64KB 与 BLE 栈共存需实测堆余量，必要时审批态临时让出大 GIF。
- **验证**：`pio run` 编译；真机端到端：起一个 Claude Code 会话 → clawd 动 → 触发一个需审批工具 → Cardputer 弹审批 → 按 ok → 终端放行。
