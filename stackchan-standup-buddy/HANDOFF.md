# HANDOFF — 2026-08-10

当前状态与待办,供下一个 agent(Codex / Claude / Kimi)接续。

## 项目一句话

CoreS3 StackChan 桌面站立提醒器:每 30 分钟摇头 + 唱歌 + 文字提醒;皮卡丘 GIF 脸
+ 倒计时面板;Mac 助手人脸跟踪(yaw/pitch);工作时段门控(TIME 对时 + RTC 保持,
下班猫睡觉);片上 GC0308 间歇帧差做高度自调。详见 README.md 和 docs/architecture.html。

## 唯一待办:烧录验证 4fec2c2

`4fec2c2`(已 push 到 `agent/stopwatch-walkie-prototype`)修了面板布局 bug
(标签「后提醒站立 · 看窗外」因锚点残留按左对齐从屏幕中心起画,右半截出屏;
顺带数字字形 64→48px、标签回到 efontCN_16),**尚未烧录** —— 当时设备从 USB 掉线。

设备重连后(`/dev/cu.usbmodem2101` 出现):

```sh
pkill -f tools/face-track          # 助手占着串口,烧录前必须停
cd stackchan-standup-buddy
~/.platformio/penv/bin/pio run -t uploadfs --upload-port /dev/cu.usbmodem2101
~/.platformio/penv/bin/pio run -t upload   --upload-port /dev/cu.usbmodem2101
./tools/face-track /dev/cu.usbmodem2101 &   # 烧完重启助手
```

验证点:

- 串口日志有 `[font] poke digits loaded`,无 `[gif]` 报错
- 面板:48px 等宽数字居中 +「后提醒站立 · 看窗外」完整居中显示,无截断
- 烧录报 `serial noise / data stream stopped` → 等 3 秒重试;连续失败检查助手是否在跑

## 运维要点

- pio 不在 PATH:`~/.platformio/penv/bin/pio`;uploadfs / upload 分开跑
- 猫脸/字体改版:`tools/lottie-src/gen-cat-styles.mjs`(风格参数化,当前
  f-pikachu)→ `/tmp/stackchan-lottie`(需 `npm i canvaskit-wasm`,脚本要拷进去跑)
  → `export-gifs.mjs` → `assemble-gifs.py`;数字字形:`tools/make-digit-font.py`
  (当前 Geist Mono SemiBold;改字体改 TTF/WEIGHT/RENDER 常量)。两者都只需 uploadfs。
- 串口协议(入):`TRACK <cx_pm> <cy_pm> <conf_pm>` / `TRACK LOST` / `TIME <分钟>`,行文本。
- 真机已验证:提醒全流程、三路确认(摸头 IMU jerk 0.8g 阈值)、人脸跟踪方向、
  睡觉态、摄像头帧差收敛。
