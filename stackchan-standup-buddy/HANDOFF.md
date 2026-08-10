# HANDOFF — 2026-08-10

当前状态与待办,供下一个 agent(Codex / Claude / Kimi)接续。

## Kimi 接手步骤（另一台电脑）

1. 拉取 `agent/stopwatch-walkie-prototype`，进入本子项目；不要改用仓库里的
   `stackchan-firmware` 构建环境。
2. 先运行 `pio run -e cores3-standup`。本次交付已在 2026-08-11 离线构建通过：
   RAM 19.5%，Flash 22.9%；`buildfs` 也已成功打包 15 个 Clawd GIF。
3. 连接 CoreS3 后重新枚举 `/dev/cu.usbmodem*`，不要沿用旧端口号；若
   `tools/face-track` 正在占用串口，烧录前先停止。
4. 因本次同时修改了 GIF 资产和播放器代码，必须分两条命令依次执行
   `uploadfs`、`upload`，每次都显式传入刚识别的真实端口。
5. 按下方“验证点”检查串口、Clawd 动画和倒计时面板。确认无误后可重新启动
   `face-track`。当前机器未检测到 CoreS3 USB，因此真机验证是唯一未完成项。

不要重新生成或量化 GIF：`data/characters/clawd/` 中的 15 个文件与
`cardputer-adv-buddy` 已验证素材逐字节一致；播放器已专门处理 disposal=2，
删掉这段处理会让 notification/thinking/busy_3 等动画留下残影。

## 项目一句话

CoreS3 StackChan 桌面站立提醒器:每 30 分钟摇头 + 唱歌 + 文字提醒;Clawd 小螃蟹 GIF 头像
+ 倒计时面板;Mac 助手人脸跟踪(yaw/pitch);工作时段门控(TIME 对时 + RTC 保持,
下班 Clawd 睡觉);片上 GC0308 间歇帧差做高度自调。详见 README.md 和 docs/architecture.html。

## 唯一待办:真机烧录验证

当前分支包含新的 Clawd 随机头像，以及 `4fec2c2` 的面板布局修复。
后者修了标签布局 bug
(标签「后提醒站立 · 看窗外」因锚点残留按左对齐从屏幕中心起画,右半截出屏;
顺带数字字形 64→48px、标签回到 efontCN_16)。两项都**尚未真机验证**：当前没有检测到 CoreS3 USB 串口。

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
- Clawd:原尺寸居中、动画无残影,任何帧都不覆盖底部面板;多次状态切换时随机头像不连续重复
- 固定状态:提醒始终显示 `clawd-notification.gif`,非工作时段始终显示 `sleep.gif`
- 烧录报 `serial noise / data stream stopped` → 等 3 秒重试;连续失败检查助手是否在跑

## 运维要点

- pio 不在 PATH:`~/.platformio/penv/bin/pio`;uploadfs / upload 分开跑
- Clawd 头像位于 `data/characters/clawd/`，复用 Cardputer-ADV 的小尺寸包。
  sleep/reminder 固定资产，其余状态从 13 个 GIF 随机且不连续重复；播放器原尺寸
  居中，`main.cpp` 的头像偏移已归零。设计与测试语义见
  `docs/plans/2026-08-11-clawd-random-avatar-design.md`。头像变更只需 uploadfs。
- 数字字形:`tools/make-digit-font.py`(当前 Geist Mono SemiBold;改字体改
  TTF/WEIGHT/RENDER 常量)，变更后只需 uploadfs。
- 串口协议(入):`TRACK <cx_pm> <cy_pm> <conf_pm>` / `TRACK LOST` / `TIME <分钟>`,行文本。
- 真机已验证:提醒全流程、三路确认(摸头 IMU jerk 0.8g 阈值)、人脸跟踪方向、
  睡觉态、摄像头帧差收敛。
