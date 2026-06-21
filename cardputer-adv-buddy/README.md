# cardputer-adv-buddy

M5Stack **Cardputer-ADV** 上的 Claude Code 桌面伴侣固件。把 IDE / agent 的状态显示在这块卡片电脑的屏幕上，定位与同目录的 `claude-code-buddy`、`m5-paper-buddy` 一致 —— 只是换了块新硬件。

> 状态：**Claude Code 桌搭**。接 macOS 上的 `cc-bridge`（BLE NUS），clawd 随**真实** Claude 会话状态动；工具要审批时屏上弹审批面板、键盘当场拍板；`tab` 看多会话列表。
>
> 演进：① avatar 程序化脸（小屏尺寸难调，弃）→ ② clawd GIF 角色（真机验证 ✓）→ ③ 接 BLE 成真桌搭（本版）。前两步见 git 历史 / OpenSpec `cardputer-coding-pet`。

## 硬件（来自 [官方文档](https://docs.m5stack.com/en/core/Cardputer-Adv)，2026-06 核对）

| 项 | 参数 |
|---|---|
| SoC | ESP32-S3FN8 (Stamp-S3A)，8MB flash，双核 LX7 @240MHz |
| 屏幕 | ST7789V2 1.14"，240×135 |
| 键盘 | 56 键 (4×14) |
| IMU | BMI270（**ADV 新增**，原版 Cardputer 无） |
| 音频 | ES8311 codec + NS4150B 功放 + 1W 喇叭 + MEMS mic |
| 其它 | IR 发射、microSD、Grove HY2.0-4P、EXT 2.54-14P、1750mAh |

LCD 引脚：BL=G38 RST=G33 DC=G34 MOSI=G35 SCK=G36 CS=G37。

## 构建 / 烧录

需要 [PlatformIO](https://platformio.org/)。

```bash
cd cardputer-adv-buddy
pio run -e cardputer-adv                 # 编译 app
pio run -e cardputer-adv -t buildfs      # 生成 clawd 包的 littlefs.bin
pio device monitor -e cardputer-adv      # 串口监视 (115200)
```

### ⚠️ 烧录这块板子的坑（真机踩出来的）

ESP32-S3 原生 USB-Serial-JTAG 在「设备已跑着固件」时重进 bootloader 极不稳：高波特率切换失败、stub 掉线、端口瞬时消失。可靠做法：

1. **进 ROM 下载模式**（官方步骤）：侧边电源开关拨 **OFF** → **按住 G0** → 上电（拨回 ON / 插 USB）→ 松开 G0。屏幕黑属正常。
2. `upload_speed = 115200`（platformio.ini 已设）——与初始波特率一致，esptool 跳过最易失败的「切换波特率」步骤。
3. **app + 文件系统一次烧完**（避免多次进下载模式），在下载模式下用 esptool 直接写四个镜像：

```bash
cd .pio/build/cardputer-adv
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --before no_reset --after hard_reset write_flash -z \
  0x0 bootloader.bin  0x8000 partitions.bin  0x10000 firmware.bin  0x310000 littlefs.bin
```

（`--before no_reset` 不踢出下载模式；`0x310000` = 分区表里 spiffs 分区偏移。）烧完**干净上电**（OFF→ON，不按 G0）即运行。

板子 id/平台/M5Cardputer 库逐字采用 M5Stack 官方 PlatformIO 文档；`M5Cardputer` v1.1.1 的 `library.json` 明确支持 *M5Cardputer-ADV*。

## 当前行为（clawd 角色）

clawd GIF 居中循环播放，随 Claude 会话状态切换：

**接入**：广播 `Claude-<MAC末2字节>`（开放未加密的 debug NUS），由 macOS 上常驻的 `cc-bridge` 守护进程按前缀连上，收会话状态 JSON。

会话状态 → clawd（派生顺序对齐 cc-bridge）：`waiting>0`→**attention** / `completed`→**celebrate** / `running≥1`→**busy** / 否则 **idle**；离线或久空闲 → **sleep**。右上角 `总数·运行数` 角标。

**审批**：bridge 置 `prompt{tool,hint}` 时弹审批面板，键盘 **`ok`=approve once / `esc`=deny / `a`=always**；不按则超时回落 ask。决定经 NUS 回送 `{"cmd":"permission","id":..,"decision":..}`。
**会话列表**：`tab` 开/关只读会话列表，`,`/`.` 滚动，`esc` 返回。
**体感**：拿起 → heart；晃动 → dizzy（覆盖模式下暂不打扰）。

clawd 角色包（`data/characters/clawd/`，约 1.3MB）在 LittleFS。串口周期打印 `[main] conn= t= r= w= prompt= heap=` 便于真机诊断。

## 代码出处（不是凭记忆写的）

- BLE NUS 服务端 + debug 通道 + 设备名 ← `../claude-code-buddy/src/ble_bridge.*` + `main.cpp startBt`（`ble_link.*` / `cclink.cpp`）
- 状态 JSON 字段 + 决定回送格式 ← `../claude-code-buddy/src/data.h _applyJson` + `main.cpp`（`cclink.cpp` 注释标行号）
- AnimatedGIF 文件回调 + GIFDRAW ← `../claude-code-buddy/src/character.cpp`；clawd 资产 ← `characters/clawd/`
- `M5.Imu` ← `m5stack/M5Unified` `examples/Basic/Imu`；键盘 KeysState(ok/esc/tab) ← M5Cardputer `Keyboard.h`

## 模块结构

| 文件 | 职责 |
|---|---|
| `agent_state.h` | 会话状态枚举 |
| `link_state.h` | `BuddyState` 快照 + `deriveAgentState`（状态→clawd） |
| `ble_link.{h,cpp}` | BLE NUS+debug 桥（逐字复用 ble_bridge，开放路径）|
| `cclink.{h,cpp}` | 解析 cc-bridge 状态 JSON + 回送审批决定 |
| `clawd_player.{h,cpp}` | 合成器：NORMAL(GIF+角标)/APPROVAL(审批面板)/SESSIONS(会话列表) |
| `motion.{h,cpp}` | BMI270 手势 |
| `main.cpp` | 控制器：cclink → clawd + 审批/会话键盘分发 |
| `data/characters/clawd/` | clawd GIF 包（`buildfs` 打进 littlefs.bin） |

> `state_source.h`(KeyboardStateSource) / `mood.{h,cpp}` 是早期版残留,Claude 桌搭未用,保留备后续(如离线键盘模式/心情)。

## Roadmap

- **Cursor 变体**：`build_flags` 加 `-DBUDDY_BRAND_PREFIX='"Cursor-"'` 出 `Cursor-` 设备,cursor-bridge 连（同 StickC 的 -claude/-cursor 套路，本仓暂只出 Claude）。
- **会话操作**：MVP 只读；切换/向某 session 发指令需 bridge `target` 路由配合。
- **ADV 专属外设**：ES8311 mic 听写、IR/喇叭提示音、BMI270 更多手势。
- **未连线模式**：无 bridge 时键盘当本地遥控/工具（复用保留的 state_source）。

> 注：本目录是 monorepo `hardware-buddies` 的子项目；独立 PlatformIO 工程，与其它子项目不共享源码（资产与渲染思路显式复用，非编译期共享）。
