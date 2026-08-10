# stackchan-standup-buddy

CoreS3 StackChan 站立提醒器。每 30 分钟摇头、唱歌(《两只老虎》)、LED 绿闪,
提醒你站起来看看窗外。

从 `stackchan-firmware` 的 Arduino 主固件衍生,去掉了 USB 串口问答功能,
只保留提醒器用途。待机时头完全不动(每分钟静默校正一次位置)。

系统架构图:[`docs/architecture.html`](./docs/architecture.html)(浏览器打开,
支持明暗主题和 PNG/SVG 导出;源文件 `architecture.architecture.json`)。

## 界面与交互

- **待机**:猫脸(上移 30px,眼睛在面板上方)+ 底部 92px 面板 ——
  Font7 数码管大倒计时 +「后提醒站立 · 看窗外」。
- **提醒**(8 秒):左右摇头 + 点头、喇叭唱《两只老虎》、机身 LED 绿闪,
  屏幕显示「该起来活动一下啦 / 站起来看窗外 · 摸头确认」。
- **确认("知道了")** 三选一,立即停:
  - **摸头** —— CoreS3 IMU 检测拍击(jerk > 0.8g,300ms 冷却;
    摇头自身的向心加速度只有 ~0.02g,不会误触发)
  - 点屏幕
  - 摸机身触摸区(前/中/后任意一条)
- 不理会则 8 秒后自动停,重新计 30 分钟。

界面设计稿:`docs/ui-preview/idle-mockup.html`(浏览器打开,含三个方案的对比,
实现的是方案 C)。

## 构建与烧录

```sh
pio run                                          # 构建
pio run -t upload   --upload-port /dev/cu.usb*   # 固件
pio run -t uploadfs --upload-port /dev/cu.usb*   # 文件系统(猫脸 GIF,首次或 data/ 变更时)
```

`uploadfs` 和 `upload` 分开跑;烧完设备自动复位。

## 摄像头高度自调(可选,片上)

Mac 跟踪离线时,设备每 30 秒借用一次自带 GC0308 摄像头(~0.7 秒窗口:
让渡 I2C 总线 → 抓两帧 → 帧差移动检测 → 归还总线),检测到移动就把
俯仰角分小步(≤5°)调向移动质心,直到移动目标落在画面 40% 高度。
Mac 跟踪在线时自动让位;睡觉态不启用。常量见 `src/main.cpp`
`CAM_*` 与 `src/camera_height.cpp`(DIFF_TH / MOTION_RATIO)。

## 工作时段

助手每分钟下发 `TIME <当天分钟数>`,固件据此门控提醒并切换状态
(常量 `WORK_AM_START` 等在 `src/main.cpp` 顶部):

- **08:00–12:00、14:00–17:30**:正常倒计时 + 提醒。
- **其他时段(且时钟已知)**:不提醒(每 5 分钟重查,进入时段立即补一次),
  猫切换为**睡觉表情**(`cat_sleep.gif`)、LED 全灭、头静止、跟踪不生效;
  面板显示灰色当前时间 + 状态(还没上班 / 午休中 / 已下班)。
- **Mac 带走**:TIME 同时写进了设备 RTC(BM8563,电池保持),设备按 RTC
  继续门控 —— 到点睡、到点醒,整夜安静。
- **助手从未运行且 RTC 未对时**:无时钟,退化为无条件 30 分钟提醒,不睡觉。

## 人脸跟踪(可选)

Mac 端跑 `tools/face-track`(AVFoundation + Vision,零依赖),通过 USB 串口
把人脸位置发给固件,头会一直转向你,屏幕(倒计时)始终可见。

```sh
swiftc -O tools/face-track.swift -o tools/face-track   # 首次编译
./tools/face-track /dev/cu.usbmodem2101                # 常驻;首次会弹摄像头授权
./tools/face-track /dev/cu.usbmodem2101 "iPhone"       # 多个摄像头时按名字选
```

协议:每秒 ~10 行 `TRACK <cx_pm> <cy_pm> <conf_pm>`(cx/cy 为画面横/纵向
-1000..1000 千分比)或 `TRACK LOST`;每分钟一行 `TIME <当天分钟数>`。
**非工作时段 helper 会完全停掉摄像头采集**(摄像头指示灯熄灭、省功耗),
只保留 TIME 对时;回到工作时段自动恢复。固件侧:P 控制 + 低通(0.25)
+ 2° 死区;3 秒收不到新数据自动释放,IDLE 步进表 1 分钟内把头停回中位。
方向反了就翻转 `src/main.cpp` 里的 `TRACK_SIGN` / `TRACK_PITCH_SIGN` 重烧。
仅 IDLE 状态生效,提醒摇头和睡觉时不干扰。

## 调参(`src/main.cpp`)

| 常量 | 默认 | 说明 |
|---|---|---|
| `REMINDER_INTERVAL_MS` | 30 min | 提醒间隔 |
| `REMINDER_DURATION_MS` | 8 s | 提醒最长持续(未被确认时) |
| `REMINDER_MELODY` | 两只老虎 | 音符表 `{频率Hz, 时长ms}`,可换曲 |
| `headPatted()` 里的 `0.8f` | 0.8g | 摸头灵敏度:误触发调大,太钝调小 |

猫脸 GIF 用 `tools/lottie-src/` 重新生成(见该目录脚本),放进
`data/characters/cat/` 后 `uploadfs`。

## 代码结构

- `src/main.cpp` —— 提醒调度、倒计时面板、旋律播放器、三路确认(摸头/点屏/摸机身)
- `src/motion.cpp` —— 舵机步进表(IDLE 静止、REMINDER 摇头点头)+ LED
- `src/gif_face.cpp` —— LittleFS GIF 脸播放(支持垂直偏移 + 底部面板裁剪)
