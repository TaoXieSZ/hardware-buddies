# stackchan-standup-buddy

CoreS3 StackChan 站立提醒器。每 30 分钟摇头、唱歌(《两只老虎》)、LED 绿闪,
提醒你站起来看看窗外。

从 `stackchan-firmware` 的 Arduino 主固件衍生,去掉了 USB 串口问答功能,
只保留提醒器用途。待机时头完全不动(每分钟静默校正一次位置)。

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

## 人脸跟踪(可选)

Mac 端跑 `tools/face-track`(AVFoundation + Vision,零依赖),通过 USB 串口
把人脸位置发给固件,头会一直转向你,屏幕(倒计时)始终可见。

```sh
swiftc -O tools/face-track.swift -o tools/face-track   # 首次编译
./tools/face-track /dev/cu.usbmodem2101                # 常驻;首次会弹摄像头授权
./tools/face-track /dev/cu.usbmodem2101 "iPhone"       # 多个摄像头时按名字选
```

协议:每秒 ~10 行 `TRACK <cx_pm> <conf_pm>`(cx 为画面横向 -1000..1000 千分比)
或 `TRACK LOST`。固件侧:P 控制 + 低通(0.25)+ 2° 死区;3 秒收不到新数据
自动释放,IDLE 步进表 1 分钟内把头停回中位。方向反了就翻转 `src/main.cpp`
里的 `TRACK_SIGN` 重烧。仅 IDLE 状态生效,提醒摇头时不干扰。

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
