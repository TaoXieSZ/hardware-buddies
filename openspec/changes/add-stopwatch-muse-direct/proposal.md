## Why

使用者想在 StopWatch 上直接问个人 Muse。现有固件只能通过同一局域网里的 Mac 桥工作，但在公司里，Mac 连的是员工网，手表只能连访客网，两者互不相通，经过 Mac 的方案走不通。个人云 2026-10-10 已上线设备转写接口（腾讯一句话识别，经个人服务器调用，见 `improve-openmuse-device-experience` 的 D1），手表可以不经 Mac，直接访问个人云。

## What Changes

- 新增独立的编译环境 `m5stack-stopwatch-muse`（直连 Muse 模式）。原有的 `m5stack-stopwatch`（Mac 桥 + 指挥编码助手）一行不改；切换模式靠重新烧录。
- 直连模式下，手表只连 Wi-Fi，用 HTTPS 访问个人云域名：
  - 开机和空闲时调用 `GET /device/v1/status`，确认在线并读取转写是否可用；
  - 按住 KEYA 录音，录音先存进 PSRAM（最长 30 秒）；松开后上传到 `POST /device/v1/transcribe` 转成文字；
  - 用协议 2 调用 `POST /device/v1/chat` 问 Muse（同一段录音固定一个请求号，重试不会重复计费）；
  - 表盘结果页显示一行精简回答，振动一下提示。
- 直连模式没有朗读：表端没有中文语音合成，Mac 的 `say` 也不在这条链路上。
- 设备令牌、Wi-Fi 名称和密码只写在被 git 忽略的 `include/walkie_config.h` 里，不进仓库。

## Capabilities

### New Capabilities
- `stopwatch-muse-direct`：手表不经 Mac、直接问个人 Muse 的连接、录音上传、提问、显示与出错行为。

### Modified Capabilities
（无。原有 `stopwatch-audio-loop` 与多代理控制不变。）

## Impact

- **固件**：`stopwatch-walkie/` 新增一个编译环境和一个 HTTPS 传输模块，复用现有状态机、按键、录音、界面。
- **个人云**：不改动，使用已上线的 `/device/v1/status`、`/v1/transcribe`、`/v1/chat`。StopWatch 已作为独立设备配对。
- **网络前提**：访客网 需要能直接访问公网 HTTPS。如果它要求网页登录（强制门户），直连模式在公司不可用，只能在家里或手机热点下使用。烧录后先实测。
- **额度**：每次提问用 1 次转写额度（每天上限 100）和 1 次模型额度（每天上限 20）；没录到声音、或识别结果为空时，不问 Muse。
