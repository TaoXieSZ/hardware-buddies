# cardputer-adv-buddy — 项目状态

M5Stack **Cardputer-ADV** 上的 Claude Code 桌搭。人类可读的状态摘要;细节见 `README.md` 与 `openspec/changes/`。

## 现在它是什么

接 macOS 上的 `cc-bridge`(BLE NUS,未加密 debug 通道),**clawd GIF 角色随真实 Claude 会话状态动**;工具要审批时屏上弹审批面板、键盘当场拍板;`tab` 看多会话列表。

## 演进路线

1. **avatar 程序化脸** —— 默认脸为 320×240 设计,240×135 小屏尺寸难调(脸过高被裁),弃。
2. **clawd GIF 角色** —— LittleFS + AnimatedGIF,固定像素居中,真机验证 ✓(OpenSpec `cardputer-coding-pet`)。
3. **接 BLE 成真桌搭** —— 本版(OpenSpec `cardputer-claude-buddy`):cc-bridge 驱动 + 审批 + 多会话。

## 真机验证(2026-06)

| 项 | 结果 |
|---|---|
| cc-bridge BLE 连接 | ✅ `[main] conn=1` |
| 真实会话状态驱动 | ✅ `t=1 r=1 w=0` → clawd busy |
| 内存余量(512KB 无 PSRAM) | ✅ **空闲 142KB**(BLE+clawd+JSON 全跑着),R1 风险证伪 |
| 审批面板 + 决定回送 | 🔄 端到端测试中(本文件即测试触发) |

## 烧录这块板子(踩过的坑)

ESP32-S3 原生 USB-Serial-JTAG 在「设备跑着固件」时重进 bootloader 极不稳。可靠流程:

1. ROM 下载模式:侧电源 **OFF** → 按住 **G0** → 上电 → 松开 G0(屏黑正常)
2. `upload_speed=115200`(跳过最易失败的波特率切换)
3. esptool 一次烧完:`--before no_reset --after hard_reset write_flash 0x0 bootloader 0x8000 partitions 0x10000 firmware [0x310000 littlefs]`
   - **只改 app 时跳过 littlefs**(4.9M clawd 包不变,省时间)
4. 干净上电(OFF→ON,不按 G0)

> bridge 占串口会另外阻碍烧录(`TAB5_SERIAL` 常开 + reconnect 抢回),走 BLE 不碰串口可回避——详见 `cardputer-coding-pet/design.md`。

## 待办

- 审批端到端真机确认(本测试)
- 多会话列表真机确认
- Cursor 变体(`-DBUDDY_BRAND_PREFIX='"Cursor-"'`)
- `cardputer-coding-pet` change 的 spec 从 avatar 对齐到 clawd 后归档
- Phase 3:ES8311 mic 听写 / IR / 更多体感
