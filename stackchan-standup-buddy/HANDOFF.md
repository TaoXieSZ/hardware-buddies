# HANDOFF — 2026-08-11

当前状态与待办,供下一个 agent(Codex / Claude / Kimi)接续。

## Kimi 接手步骤（另一台电脑）

1. 拉取 `agent/stopwatch-walkie-prototype`，进入本子项目；不要改用仓库里的
   `stackchan-firmware` 构建环境。
2. 运行主机逻辑测试、Swift typecheck、`pio run -e cores3-standup` 和 `buildfs`。
   本次交付结果：RAM 19.5%（63,804 bytes），Flash 22.9%（1,503,381 bytes）；
   buildfs 成功打包 15 个 Clawd GIF、字体和数字字模。
3. 连接 CoreS3 后重新枚举 `/dev/cu.usbmodem*`，不要沿用旧端口号；若
   `tools/face-track` 正在占用串口，烧录前先停止。
4. 必须分两条命令依次执行 `uploadfs`、`upload`，每次显式传入刚识别的真实端口。
5. 编译并启动 `face-track`，按下方验证点走完整模式流程。当前机器未烧录，
   因此真机交互仍是唯一未完成项。

不要重新生成或量化 GIF：`data/characters/clawd/` 中的 15 个文件与
`cardputer-adv-buddy` 已验证素材逐字节一致；播放器已专门处理 disposal=2，
删掉这段处理会让 notification/thinking/busy_3 等动画留下残影。

## 项目一句话

CoreS3 StackChan 桌面健康伙伴：设备自主管理每日打卡、工作/自由/会议/休息、
30 分钟站立周期、Clawd 头像和统一监控；Mac helper 仅做人脸跟踪与哲理缓存。

## 已实现功能

- 当天双拍打卡一次，NVS 保存今日日期和打卡时间；午休后不重打，17:30 清理。
- 工作仅在 08:00–12:00、14:00–17:30 监控；自由忽略日程但夜间自动静默。
- 会议 15/30/60/90 分钟/直到回来；结束后休息 10 分钟再回来源模式。
- 站立提醒双按钮、单拍确认、5 分钟重试；身体三分区触摸确认已禁用。
- 工作蓝色、自由紫色呼吸 LED；其余 LED 关闭；各模式固定/随机 Clawd 已映射。
- 普通单拍显示随机 Clawd 与哲理 8 秒；helper 用隔离 `codex exec` 缓存，固件内置 fallback。
- 设备通过 `MODE WORK|FREE|OFF` 统一控制 helper 摄像头，不在 Mac 重复日程状态机。

## 本地验证

```sh
cd stackchan-standup-buddy
swiftc -warnings-as-errors -typecheck tools/face-track.swift
swiftc -O tools/face-track.swift -o /tmp/standup-face-track
g++ -std=c++17 -Wall -Wextra -Werror test/test_mode_logic.cpp -o /tmp/standup-mode-test
/tmp/standup-mode-test
~/.platformio/penv/bin/pio run -e cores3-standup
~/.platformio/penv/bin/pio run -e cores3-standup -t buildfs
git diff --check -- .
```

以上均通过。隔离 Codex 烟测在本机模型缓存异常下 30 秒内未产出；helper 已增加
`--ignore-user-config --ignore-rules` 且只在后台维持一个生成任务，固件始终即时
fallback。连接设备后仍需验证缓存最终回填。

## 真机烧录与验证

```sh
pkill -f tools/face-track
cd stackchan-standup-buddy
~/.platformio/penv/bin/pio run -e cores3-standup -t uploadfs --upload-port /dev/cu.usbmodemXXXX
~/.platformio/penv/bin/pio run -e cores3-standup -t upload   --upload-port /dev/cu.usbmodemXXXX
swiftc -O tools/face-track.swift -o tools/face-track
./tools/face-track /dev/cu.usbmodemXXXX
```

验证点:

- 双拍窗口手感、工作时段外提示、午休/17:30/午夜与重启恢复。
- 菜单和提醒按钮无裁切，会议/休息倒计时与提前结束热区正确。
- 提醒动作约 3 秒、yaw ±8°、nod 约 5°，没有连续旋转；提示音响度舒适。
- 工作蓝色、自由紫色呼吸，其余 LED 关闭；固定 Clawd 无残影。
- `MODE` 切换后 Mac 摄像头指示灯同步开关，板载摄像头仅在监控启用时探测。
- 哲理缓存命中时立即显示远端句，离线/超时时立即显示固件句库。

## 运维要点

- pio 不在 PATH:`~/.platformio/penv/bin/pio`;uploadfs / upload 分开跑。
- 串口协议(入):`TRACK ...` / `TRACK LOST` / `CLOCK <YYYYMMDD> <分钟>` /
  `WISDOM <文本>`；协议(出):`MODE WORK|FREE|OFF` / `WISDOM_REQUEST`。
- Clawd 头像位于 `data/characters/clawd/`；头像或字体变更只需 uploadfs。
- 数字字形:`tools/make-digit-font.py`，变更后只需 uploadfs。
