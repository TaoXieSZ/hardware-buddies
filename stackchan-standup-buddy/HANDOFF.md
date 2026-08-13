# HANDOFF — 2026-08-13

供下一个 agent(Codex / Claude / Kimi)接续。项目已合并进 main(PR #1,
merge commit `ea81336`),后续工作从 main 开新分支。

## 项目一句话

CoreS3 StackChan 桌面健康伙伴:设备自主管理每日点屏打卡、工作/自由/会议/休息
模式、30 分钟站立周期、Clawd 头像(每 5 分钟随机轮换)和统一监控;Mac helper
(face-track)只做 Vision 人脸跟踪、CLOCK 对时和哲理句缓存,摄像头由设备的
`MODE WORK|FREE|OFF` 统一控制。

## 当前状态:全部验证通过,真机在用

真机交互验证已全部完成(2026-08-12,办公室机器):

- 点屏打卡(原双拍有结构性锁死,已改触屏)、金句、头像轮换、午休显示均正常
- 打卡保持:RTC 垃圾读数误清打卡已修(readRtc 严格校验 + CLOCK 日期优先)
- 金句/提醒面板闪屏已修(仅进入或内容变化时重绘)
- 摆头误触发金句:埋点验证不存在,jerk 阈值 0.8g 裕度充足

## 烧录与验证

```sh
pkill -f tools/face-track        # helper 占串口,烧录必挂,先停
cd stackchan-standup-buddy
~/.platformio/penv/bin/pio run -e cores3-standup -t uploadfs --upload-port /dev/cu.usbmodemXXXX
~/.platformio/penv/bin/pio run -e cores3-standup -t upload   --upload-port /dev/cu.usbmodemXXXX
swiftc -O tools/face-track.swift -o tools/face-track
./tools/face-track /dev/cu.usbmodemXXXX
```

端口重插会变,先 `ls /dev/cu.usb*` 确认。本地验证:swiftc typecheck、
`g++ test/test_mode_logic.cpp`、`pio run`、`buildfs` 均应通过。

## 可做的后续(按价值排序)

1. **哲理句远端缓存回填**:helper 的 `codex exec` 缓存在本机模型缓存异常时
   30s 无产出,固件即时 fallback 内置句库 —— 功能正常但远端句从未真正命中过,
   值得修 helper 侧(或换成其它生成源)。
2. **剩余验证点**:会议 15/30/60/90 倒计时与提前结束热区、提醒 5 分钟重试、
   午夜/重启恢复边界(代码读过没实测)。
3. **face-track 开机自启**(launchd),现在每次要手动起。
4. 头像风格想换:`tools/lottie-src/gen-cat-styles.mjs`(当前 Clawd 资产是
   固定 GIF,风格生成器产出的是 cat 系;两者管线不同,见 README)。

## 运维要点

- pio 不在 PATH:`~/.platformio/penv/bin/pio`;uploadfs / upload 分开跑。
- 串口协议(入):`TRACK ...` / `TRACK LOST` / `CLOCK <YYYYMMDD> <分钟>` /
  `WISDOM <文本>`;(出):`MODE WORK|FREE|OFF` / `WISDOM_REQUEST`。
- Clawd 头像在 `data/characters/clawd/`;头像或字体变更只需 uploadfs。
  **不要重新量化 GIF**:播放器专门处理 disposal=2,删掉会留残影。
- 数字字形:`tools/make-digit-font.py`(Geist Mono SemiBold),变更后只需 uploadfs。
- AGENTS.md 根目录的 Gotchas 一节有本项目的 I2C/RTC/烧录坑清单。
