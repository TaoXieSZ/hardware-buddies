## ADDED Requirements

### Requirement: 按键起停本地录音并存为 WAV 到 SD

固件 SHALL 在 NORMAL 态提供一个专用键，按下开始录音、再按停止。录音 SHALL 从板载 mic 采集
16000 Hz / 单声道 / 16-bit 音频，写成合法 WAV 文件保存到 microSD 卡，文件名 SHALL 自增
（如 `/note_0001.wav`，通过扫描 SD 现有 `note_*.wav` 取下一序号，不依赖 RTC）。

#### Scenario: 起录到停录产出可用 WAV

- **WHEN** 用户在 NORMAL 态按录音键开始、说话、再按停止
- **THEN** 固件 SHALL 在 SD 根目录生成一个 16kHz/mono/16-bit 的 WAV 文件
- **AND** WAV header 的 fileSize/dataSize SHALL 在停录时正确回填（文件在电脑上可正常播放）

#### Scenario: 文件名自增不覆盖

- **WHEN** SD 上已存在 `note_0001.wav`
- **THEN** 新录音 SHALL 命名为下一个未占用序号（如 `note_0002.wav`），SHALL NOT 覆盖已有文件

### Requirement: Mic 与 Speaker 互斥切换

固件 SHALL 在起录时先停止 Speaker（`M5Cardputer.Speaker.end()`）再启用 Mic
（`M5Cardputer.Mic.begin()`），在停录时 `M5Cardputer.Mic.end()` 后 `M5Cardputer.Speaker.begin()`
恢复。录音期间固件 SHALL NOT 播放任何提示音（Speaker 已让出 I2S）。停录后 Speaker 提示音
SHALL 恢复正常。

#### Scenario: 录音期间静音、录后恢复

- **WHEN** 正在录音
- **THEN** 固件 SHALL NOT 触发任何 Speaker 提示音（nudge/tone）
- **AND** 停录后按会触发提示音的键 SHALL 能正常发声（Speaker 已归还）

### Requirement: 分帧采集不阻塞主循环

固件 SHALL 以分帧方式采集（每次主循环迭代抓取一小段并写盘），使录音期间 clawd 动画与
BLE 状态更新 SHALL 继续运行，SHALL NOT 因录音而长时间冻结主循环。

#### Scenario: 录音时 avatar/链路仍活

- **WHEN** 设备正在录音
- **THEN** clawd 动画与 cc-bridge 状态更新 SHALL 继续（无明显长时间卡死）

### Requirement: 录音态可见指示

固件 SHALL 在录音期间显示持续的录音指示（如顶栏 `REC` + 时长），区别于普通 toast（不自动
消失），停录后指示 SHALL 撤下。

#### Scenario: 录音中显示 REC

- **WHEN** 设备处于录音态
- **THEN** 屏幕 SHALL 持续显示录音指示，直到停录

### Requirement: SD 不可用时安全降级

固件 SHALL 在起录时挂载/检查 SD；若 SD 未插入、挂载失败或写入失败，SHALL 提示（如 toast
`no SD`）、不进入/立即退出录音态、并归还 Speaker，SHALL NOT 崩溃或卡在录音态。

#### Scenario: 无 SD 卡时不崩

- **WHEN** 未插 SD（或 `SD.begin` 失败）时按录音键
- **THEN** 固件 SHALL 提示无 SD 并停留在正常态，SHALL NOT 崩溃，Speaker SHALL 保持可用

#### Scenario: 写入中途失败安全退出

- **WHEN** 录音写盘过程中 SD 写入失败（如拔卡/卡满）
- **THEN** 固件 SHALL 停止录音、尽力关闭文件、归还 Speaker、提示错误，SHALL NOT 崩溃
