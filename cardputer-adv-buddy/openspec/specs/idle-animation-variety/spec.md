# idle-animation-variety Specification

## Purpose
TBD - created by archiving change cardputer-idle-variety. Update Purpose after archive.
## Requirements
### Requirement: Idle 态偶尔切换到 idle-reading 变体

固件 SHALL 在 `AgentState::Idle`（且非 sleeping、非临时 reaction 动画期间）持续播放
`idle.gif` 为主，并 SHALL 以低频、非固定周期的方式偶尔切换到 `clawd-idle-reading.gif`
变体播放一段较短时间后自动切回 `idle.gif`。切换节奏（触发概率、停留时长）不做数值
硬性约束，允许实现调参。

#### Scenario: 长时间 Idle 时不会永远只播 idle.gif

- **WHEN** clawd 连续处于 Idle 态超过若干个变体判定周期
- **THEN** 固件 SHALL 至少偶尔切换到 `clawd-idle-reading.gif` 播放一段时间
- **AND** SHALL 在该变体停留结束后自动切回 `idle.gif`

#### Scenario: 非 Idle 态不受影响

- **WHEN** clawd 处于 Thinking / ToolUse / Approval / Done / Notification 任一非 Idle 态，
  或处于 sleeping / 临时 reaction 动画期间
- **THEN** 固件 SHALL NOT 触发 idle-reading 变体切换（该机制仅在纯 Idle 态生效）

#### Scenario: 变体切换不打断真实状态变化

- **WHEN** idle-reading 变体正在播放期间，clawd 收到非 Idle 的真实状态（如 Thinking）
- **THEN** 固件 SHALL 立即按新状态渲染（既有 `setState`/`applyTarget` 路径），
  SHALL NOT 因变体计时器未到期而延迟状态切换

