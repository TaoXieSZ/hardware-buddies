# audio-feedback Specification

## Purpose
TBD - created by archiving change cardputer-feedback-channels. Update Purpose after archive.
## Requirements
### Requirement: hook 事件 wav 音效

固件 SHALL 读取 cc-bridge 心跳 JSON 中的 `play` 字段（one-shot 小写事件名），从 LittleFS 读取 `/sounds/<event>.wav` 到 RAM，并经 M5 `Speaker.playWav` 播放。文件不存在时 SHALL 静默忽略（只有关键事件配 wav，bridge 发的其他 `play` 值自动落空）。wav SHALL 为 RAM-safe 的 PCM：16kHz、mono、单文件 ≤ 100KB（实测 142KB 空闲堆，~48KB/文件安全）。

#### Scenario: 关键事件播放 wav

- **WHEN** 收到 `play` 字段为 `stop` / `permissionrequest` / `posttoolusefailure` / `notification` 之一
- **THEN** 固件 SHALL 读取并播放对应 `/sounds/<event>.wav`

#### Scenario: 未配 wav 的事件静默忽略

- **WHEN** 收到 `play` 字段为未配 wav 文件的事件（如 `pretooluse`/`posttooluse`）
- **THEN** 固件 SHALL 静默忽略，不报错、不阻塞

#### Scenario: 超大文件保护

- **WHEN** 目标 wav 文件大小为 0 或 > 100KB
- **THEN** 固件 SHALL 拒绝加载（避免耗尽 RAM）

### Requirement: 本地 tone 提示

固件 SHALL 为本地事件（BLE 连接 / 断开、nudge 按键、开机）播放短 tone 音符序列，不依赖 wav 文件。这些 tone 与 hook 事件 wav 互不重复（hook 事件声音统一走 wav）。

#### Scenario: 连接与断开提示

- **WHEN** 与 cc-bridge 的 BLE 连接建立或断开
- **THEN** 固件 SHALL 播放对应的上行/下行 tone

### Requirement: 音量控制

固件 SHALL 提供键盘音量调节：`-` 降、`=` 升，范围 0-255，tone 与 wav 共用同一音量。调节 SHALL 立即生效并给出反馈（一声提示音 + 屏底短暂显示当前音量值）。

#### Scenario: 调节音量

- **WHEN** NORMAL 模式（非审批/会话/帮助覆盖层）按下 `-` 或 `=`
- **THEN** 音量 SHALL 相应增减（步进 25，clamp 到 0-255）并立即对后续声音生效
- **AND** 屏底 SHALL 短暂显示 `vol <N>`

