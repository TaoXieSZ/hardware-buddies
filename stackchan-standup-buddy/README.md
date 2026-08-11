# stackchan-standup-buddy

CoreS3 StackChan 站立提醒器。设备管理每日打卡与工作/自由/会议/休息模式，
每个活跃周期 30 分钟，提醒你站起来看看窗外。屏幕头像使用原尺寸 Clawd
小螃蟹像素动画。

从 `stackchan-firmware` 的 Arduino 主固件衍生,去掉了 USB 串口问答功能,
只保留提醒器用途。非活跃状态头完全不动。

系统架构图:[`docs/architecture.html`](./docs/architecture.html)(浏览器打开,
支持明暗主题和 PNG/SVG 导出;源文件 `architecture.architecture.json`)。

## 界面与交互

- **未打卡**:固定 `sleep.gif`，三类监控、LED 和舵机全部静默；工作时段内
  双拍脑袋完成当天唯一一次打卡。记录写入 NVS，当天重启仍保留。
- **工作 / 自由**:底栏显示模式与 30 分钟倒计时。工作仅在日程内监控、LED
  蓝色呼吸；自由从底栏菜单手动进入/退出、忽略工作日程、LED 紫色呼吸。
- **会议**:可选 15/30/60/90 分钟或“直到我回来”，固定
  `clawd-thinking.gif`；结束后自动进入 10 分钟休息。
- **休息**:固定 `heart.gif` 并显示倒计时；点“继续工作”可提前结束。
- **提醒**(8 秒):固定 `clawd-notification.gif`，约 3 秒轻柔摇头/点头，屏幕提供
  “开始休息”“进入会议”。提醒态单拍脑袋等同开始休息；忽略后每 5 分钟重提醒。
- 普通模式单拍脑袋会随机显示 Clawd + 一句短哲理 8 秒，不改变模式或倒计时；
  未打卡单拍需等双拍窗口过期。身体三分区触摸不再用于确认。

界面设计稿:`docs/ui-preview/idle-mockup.html`(浏览器打开,含三个方案的对比,
实现从原方案 C 演进为底栏模式菜单和状态倒计时)。

## 构建与烧录

```sh
~/.platformio/penv/bin/pio run -e cores3-standup
~/.platformio/penv/bin/pio run -e cores3-standup -t buildfs
~/.platformio/penv/bin/pio run -e cores3-standup -t upload   --upload-port /dev/cu.usb*
~/.platformio/penv/bin/pio run -e cores3-standup -t uploadfs --upload-port /dev/cu.usb*
```

`uploadfs` 和 `upload` 分开跑;烧完设备自动复位。

## 摄像头高度自调(可选,片上)

三类监控启用且 Mac 跟踪离线时,设备每 30 秒借用一次自带 GC0308 摄像头
(~0.7 秒窗口:让渡 I2C 总线 → 抓两帧 → 帧差移动检测 → 归还总线),检测到移动
就把俯仰角分小步(≤5°)调向移动质心,直到移动目标落在画面 40% 高度。
Mac 跟踪在线时自动让位;未打卡、会议、休息、提醒和夜间不启用。

## 打卡、工作时段与模式恢复

- **08:00–12:00、14:00–17:30**:当天双拍打卡后运行工作模式；14:00
  直接开始新的完整 30 分钟周期，无需重打卡。
- **17:30**:自动结束工作并清除当天打卡。工作时段外双拍只显示提示，不进入自由。
- **自由模式**:忽略工作时段，进入即开始新周期；00:00–08:00 禁止进入，
  跨午夜自动停止。
- 会议/自由不持久化；重启仅在“今天已打卡且仍处工作时段”时恢复工作，否则静默。
- Mac helper 下发 `CLOCK <YYYYMMDD> <当天分钟数>` 并写入设备 RTC，断开后仍可门控。

## 人脸跟踪与哲理缓存(可选)

Mac 端跑 `tools/face-track`(AVFoundation + Vision,零依赖),通过 USB 串口
把人脸位置发给固件。StackChan 是模式权威：设备上报 `MODE WORK|FREE|OFF`，
helper 只按设备模式开关摄像头，不复制日程状态机。

```sh
swiftc -O tools/face-track.swift -o tools/face-track
./tools/face-track /dev/cu.usbmodem2101
./tools/face-track /dev/cu.usbmodem2101 "iPhone"
```

跟踪协议仍为 `TRACK <cx_pm> <cy_pm> <conf_pm>` / `TRACK LOST`。设备发送
`WISDOM_REQUEST` 后，helper 用隔离的 `codex exec --ephemeral --sandbox read-only`
后台预生成并缓存一句哲理，再发 `WISDOM <文本>`。缓存空、超时或离线时，固件
立即使用内置句库，拍头交互不会等待网络。

## 调参与验证

| 常量 | 默认 | 说明 |
|---|---|---|
| `CYCLE_MS` | 30 min | 活跃周期 |
| `REPEAT_MS` | 5 min | 忽略后的重提醒间隔 |
| `BREAK_MS` | 10 min | 休息时长 |
| `headPat()` 里的 `0.8f` | 0.8g | 摸头灵敏度 |

```sh
swiftc -warnings-as-errors -typecheck tools/face-track.swift
g++ -std=c++17 -Wall -Wextra -Werror test/test_mode_logic.cpp -o /tmp/standup-mode-test
/tmp/standup-mode-test
```

Clawd GIF 复用自 `../cardputer-adv-buddy/data/characters/clawd/`，放在
`data/characters/clawd/`。睡眠/会议/休息/提醒使用固定素材；工作/自由与哲理彩蛋
使用随机素材。播放器不放大素材，而是按底部面板起点水平、垂直居中。详细设计见
`docs/plans/2026-08-11-clawd-random-avatar-design.md`。

## 代码结构

- `src/main.cpp` —— 模式状态机、UI、打卡持久化、提醒和双向串口协议
- `src/mode_logic.h` —— 可在主机测试的工作时段与恢复规则
- `src/motion.cpp` —— 舵机步进表、人脸目标和工作/自由 LED
- `src/gif_face.cpp` —— LittleFS Clawd 播放(固定/随机、原尺寸、底部面板裁剪)
- `tools/face-track.swift` —— 设备模式驱动的摄像头 helper 与 Codex 哲理缓存
