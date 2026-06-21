## ADDED Requirements

### Requirement: 以 Claude-XXXX 广播并被 cc-bridge 连上

固件 SHALL 通过 BLE 广播一个以 `Claude-` 为前缀的设备名，并暴露与 buddy 家族一致的 Nordic UART Service（NUS：service `6e400001-…`，RX write `…0002`，TX notify `…0003`），使 macOS 上常驻的 `cc-bridge` 守护进程能按前缀发现并连接它，无需 bridge 侧改动。

#### Scenario: bridge 发现并连接

- **WHEN** 固件运行且 cc-bridge 在扫描 `Claude-` 前缀
- **THEN** 固件 SHALL 处于可被发现的广播态并接受连接
- **AND** 连接建立后 SHALL 能通过 NUS RX 接收行分隔的 JSON

### Requirement: 解析状态 JSON 驱动 clawd 与会话计数

固件 SHALL 解析 bridge 经 NUS 推送的状态 JSON（含 `total` / `running` / `_sessions` / `prompt` / `waiting` 等字段），并据此驱动 clawd 状态与屏上会话计数。会话状态到 clawd GIF 的映射 SHALL 复用 `cardputer-coding-pet` 已实现的显示层。

#### Scenario: 真实状态驱动 clawd（取代键盘自测）

- **WHEN** 收到一条状态 JSON，其会话状态表示「忙/思考中」
- **THEN** clawd SHALL 切到对应的 busy 类 GIF
- **AND** 屏角 SHALL 显示当前 `running`/`total` 计数

#### Scenario: 不再依赖键盘自测

- **WHEN** 固件已与 bridge 建立连接
- **THEN** clawd 的状态 SHALL 由 BLE 状态 JSON 驱动，而非键盘按键

### Requirement: 断连与重连

固件 SHALL 在与 bridge 断连时进入一个明确的「离线」表现（如 clawd sleep 或离线角标），并 SHALL 自动恢复广播以便 bridge 重连，无需重启设备。

#### Scenario: 掉线后自动可重连

- **WHEN** 与 bridge 的 BLE 连接断开
- **THEN** 固件 SHALL 显示离线表现并重新进入可发现广播态
- **AND** bridge 再次出现时 SHALL 能重新连接并恢复状态驱动
