# agent-state-animation Specification

## Purpose
TBD - created by archiving change cardputer-feedback-channels. Update Purpose after archive.
## Requirements
### Requirement: 会话状态到 clawd 动画的完整映射

固件 SHALL 将 cc-bridge 推送的会话状态派生为 clawd 动画状态，覆盖 idle / thinking / tool-use / approval / done / notification 七态（approval 由审批覆盖层处理，主形象用其余六态）。派生 SHALL 在判定 running 之前先判定 thinking——因为 bridge 在 `UserPromptSubmit` 时同时置 `running=1` 与 `msg="thinking…"`，若先判 running 会把 thinking 误落到 tool-use。

#### Scenario: 模型推理中显示 thinking

- **WHEN** 收到状态 `msg` 含 "thinking" 且无待审批（`waiting=0`）
- **THEN** clawd SHALL 显示 thinking 动画
- **AND** 即使此时 `running=1` 也 SHALL 优先判为 thinking 而非 tool-use

#### Scenario: 等待用户输入显示 notification

- **WHEN** 收到状态 `msg` 含 "waiting for your input"
- **THEN** clawd SHALL 显示 notification 动画

#### Scenario: 工具执行中显示 tool-use

- **WHEN** `running >= 1` 且 `msg` 非 thinking
- **THEN** clawd SHALL 显示 tool-use（busy）动画

### Requirement: reaction 临时覆盖机制

固件 SHALL 提供 reaction 机制，由体感事件与 hook 事件共用：短暂覆盖主状态动画一段固定时长后自动恢复到主状态。reaction SHALL 仅在 NORMAL 显示模式生效（审批/会话/帮助覆盖层优先）。

#### Scenario: 工具失败闪现 error

- **WHEN** 状态 `msg` 以 "failed" 开头且为新出现（边沿触发，上一帧非 failed）
- **THEN** clawd SHALL 以 reaction 形式显示 error 动画约 2.5s
- **AND** SHALL 不被随后紧跟的 done/ready 状态打断（reaction 时长内保持）

#### Scenario: 体感触发 dizzy / heart

- **WHEN** IMU（BMI270，经 M5Unified `M5.Imu`）检测到晃动或拿起
- **THEN** clawd SHALL 显示 dizzy（晃动）或 heart（拿起）reaction

#### Scenario: 久静进入睡眠

- **WHEN** 设备长时间静止且会话空闲
- **THEN** clawd SHALL 切到 sleep 动画

### Requirement: GIF 资源与屏幕融合

所有 clawd GIF SHALL 为 120px 宽，背景为纯黑 `#000000` 或带透明索引（固件按透明 → 黑底绘制），以在 240×135 屏居中且与黑底无缝融合。外来 GIF 若背景非纯黑（如深灰 `#101010`）SHALL 在入库前重新着色为纯黑。

#### Scenario: 外来 GIF 背景对齐

- **WHEN** 引入非 clawd 家族的 GIF（如 clawstick 的 dizzy-120，背景为 `#101010`）
- **THEN** 入库前 SHALL 把背景重新着色为纯黑 `#000000`（或设为透明索引）
- **AND** 上屏后 SHALL 与 240×135 黑底无缝融合，无可见底框

