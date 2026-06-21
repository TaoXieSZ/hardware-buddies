> 烧录走 BLE 不碰串口；刷固件仍按 cardputer-coding-pet 的坑：ROM 下载模式 + upload_speed 115200 + 一次烧完（见该 change 的 design.md / 本仓 README）。
> 进度说明：代码 + `pio run` 编译可达的任务已勾；**纯真机门**（需烧录 + cc-bridge 运行 + 真实 Claude 会话）保留未勾：1.3 / 1.4 / 2.5 / 3.5 / 4.3。

## 1. BLE 链路 spike（先解 R1/R2：能连 + RAM 够）

- [x] 1.1 对照 `ble_bridge.{h,cpp}` 逐字核对：NUS `6e40000x` + cc-bridge 连的**未加密 debug 服务** `b0c2dbe6-cc0x`；S3 路径所有 char 开放、不配对（Cardputer 同走）。设备名 `esp_read_mac(ESP_MAC_BT)`+`BUDDY_BRAND_PREFIX"%02X%02X"`
- [x] 1.2 `src/ble_link.{h,cpp}`（逐字复用 ble_bridge + 一行 `#define` 走开放路径）；`pio run` SUCCESS
- [x] 1.3 真机：cc-bridge 按前缀连上 ✅ 串口 `[main] conn=1 ... heap=142560`（142KB 空闲，R1 证伪）
- [x] 1.4 端到端冒烟：bridge 推状态 → 串口 `[main] conn=1 t=1 r=1 w=0` 反映 ✅

## 2. 状态层：BLE 驱动 clawd（ble-claude-link spec）

- [x] 2.1 加 `ArduinoJson @ ^7`；`cclink.cpp` 解析状态 JSON（`total/running/waiting/completed/msg/entries/prompt`），字段逐字对照 `data.h _applyJson`
- [x] 2.2 BLE 状态源落地为 `cclink` + `deriveAgentState`（waiting→Approval/completed→Done/running→ToolUse/else Idle），main 改由 BLE 驱动（取代键盘自测）
- [x] 2.3 右上角 `总数·运行数` 角标（`clawd::setBadge`，NORMAL 模式画在 sprite 角落不遮 GIF）
- [x] 2.4 断连：`ble_link` onDisconnect 自动重启广播；main `!online` → clawd sleep（离线表现）
- [x] 2.5 真机：真实会话状态(`t=1 r=1`)驱动 clawd busy，不再靠键盘 ✅

## 3. 审批层（tool-approval spec）

- [x] 3.1 `clawd_player` 增 APPROVAL 模式 + `showApproval(tool,hint)`：弹审批面板（工具大字+参数+按键提示），覆盖 GIF（模式互斥）
- [x] 3.2 长 `hint` 截断不溢出
- [x] 3.3 键盘：`ok`(enter)=once / `esc`=deny / `a`=always；`cclink::sendDecision` 回送 `{"cmd":"permission","id":..,"decision":..}`（逐字对照 main.cpp:1428）
- [x] 3.4 bridge 撤 prompt 或本地兜底 30s 超时 → 关面板（不发=ask）
- [x] 3.5 真机：BLE 直推 prompt(Bash/terraform apply) → Cardputer 弹审批面板 → 按 ok → 回送 `{"cmd":"permission","id":"test-001","decision":"once"}` ✅（`tools/ble_test_prompt.py`，绕开 bridge 单独验证固件；cc-bridge 路径不弹是其会话 bypass/中继配置问题，非固件）

## 4. 会话层 MVP（session-overview spec）

- [x] 4.1 `clawd_player` 增 SESSIONS 模式 + `showSessions(entries,n)`：只读列出各会话行；main `tab` 开关
- [x] 4.2 `,`/`.` 滚动、`esc` 返回；明确不做切换/操作
- [x] 4.3 真机：`tab` 弹出只读会话列表，用户确认「可以看到」✅

## 5. 收尾

- [x] 5.1 品牌编译开关 `BUDDY_BRAND_PREFIX`（默认 `Claude-`），Cursor 变体覆盖即可；本次只出 Claude
- [x] 5.2 `README.md` 更新为 Claude 桌搭：接 cc-bridge 连法、状态/审批/会话键盘语义、串口诊断、模块结构
- [x] 5.3 全量 `pio run` 绿灯（RAM 23.6% / Flash 35.6%）；`.pio/` 忽略、仅源码+openspec 纳入
- [x] 5.4 `openspec validate cardputer-claude-buddy --strict` 通过
