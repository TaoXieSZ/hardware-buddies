## Why

Cardputer-ADV 板载 mic（ES8311 codec + MEMS mic）与 microSD 卡槽，用户已插入 SD 卡、存储充足，
但固件从没用过 mic 做本地录音（现有 `v` 键 PTT 只是把 mic 热键**转发给 Mac 的听写**，不在本机录）。
把它变成一个**随手按键就能录一段语音笔记、存成 WAV 到 SD 卡**的小工具——写代码时冒出个想法，
按一下录下来，不用切到手机/电脑。upstream M5Cardputer 自带 `mic_wav_record` 示例，
mic 初始化 / 录制 / WAV 落盘 / SD 引脚全部有 verbatim ground truth 可抄。

## What Changes

- 新增**本地录音笔记**：NORMAL 态按一个专用键进入录音，屏上显示录音指示（时长/REC），
  再按停止 → 把采集到的音频写成 **16kHz / mono / 16-bit WAV** 存到 SD 卡（文件名自增，
  如 `/note_0001.wav`）。
- **SD 卡挂载**：`SPI.begin(40,39,14,12)` + `SD.begin(12, SPI, 25000000)`（Cardputer-ADV 引脚，
  抄 upstream `sdcard.ino`）。SD 是独立于 LittleFS（clawd GIF 包）的文件系统，互不干扰。
- **Mic/Speaker 互斥处理**：M5Unified 下 Mic 与 Speaker 共用 I2S，录音期间必须先
  `Speaker.end()` 再 `Mic.begin()`，停止后 `Mic.end()` 再 `Speaker.begin()` 恢复提示音——
  与现有 `sound_player` 协调。
- 录音采用**分帧采集**（每个 loop() 抓一小段写盘），避免阻塞 clawd 动画/BLE 主循环。

## Capabilities

### New Capabilities
- `voice-notes`: cardputer 本地录音——按键起停，mic → 16kHz mono WAV → microSD，
  作为随手语音笔记；与 Speaker 互斥切换、分帧不阻塞主循环。

### Modified Capabilities
(none — 全新能力；与现有 `audio-feedback`(Speaker 提示音) 有 I2S 互斥关系，但不改其语义，
录音时临时让出 I2S、录完归还)

## Non-goals

- **不做云端上传 / 转写 / 发给 Claude**：MVP 只本地录 + 存 SD。（"笔记进 AI" 是后续独立议题，
  可能走已有 BLE 通道或取卡导出。）
- **不做设备端回放/文件管理 UI**（列表/删除/播放）：upstream 示例有回放，但本次只做「录+存」，
  回放/管理后续再开 change。
- **不复用 `v` 键 PTT**：PTT 是转发 Mac 听写，语义不同，保留不动；录音用**另一个专用键**。
- **不做 RTC 时间戳文件名**（cardputer 未必有已校准 RTC）：用自增序号命名（扫描 SD 现有
  `note_*.wav` 取下一号）。
- **不做长时间/大文件分段**：一段笔记一个文件，靠 SD 容量兜底（用户已确认够用）。

## Impact

- 新增 `cardputer-adv-buddy/src/recorder.{h,cpp}`（或并入现有音频模块）：SD 挂载、Mic 起停、
  分帧采集、WAV header 写入与 finalize。
- `cardputer-adv-buddy/src/main.cpp`：NORMAL 态新增录音起停键分发 + 录音态屏幕指示；
  与 `sound_player`/PTT 的 I2S 协调（录音时禁提示音）。
- `cardputer-adv-buddy/src/sound_player.{h,cpp}`：暴露 Speaker end/begin 让位给 Mic 的钩子。
- `cardputer-adv-buddy/platformio.ini`：确认 `SD`/`SPI` 库可用（Arduino 框架内置，通常无需加）。
- `cardputer-adv-buddy/src/clawd_player.{h,cpp}`：录音态的屏幕指示（REC + 时长）。
- 不改协议、不改 cc-bridge、不动其它子项目。
