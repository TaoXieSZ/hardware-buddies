## Context

现有固件（`stopwatch-walkie/src/main.cpp`）的工作流程是：Wi-Fi → WebSocket 连 Mac 桥 → 录音分块实时发出 → 桥负责转写、朗读和指挥编码助手。状态机 `AudioLoop`（`include/audio_loop.h`）已经有连接中、就绪、录音、转写中、结果、出错这几个状态；PSRAM 里预留了一块 960000 字节、约 30 秒的朗读缓冲。个人云已提供设备转写和协议 2 提问接口；StopWatch 已作为独立设备配对，令牌由使用者本机保存。

## Goals / Non-Goals

**Goals**：不经 Mac 完成一次“说 → 听懂 → 问 Muse → 显示”；不改原有固件环境的行为；令牌不进仓库。

**Non-Goals**：朗读回答；在手表上配网（仍在编译时写入）；指挥编码助手；多行或滚动显示完整回答（以后与螃蟹界面一起做）；绕过访客网的网页登录。

## Decisions

1. **用编译开关区分两种模式，不在运行时切换**。`-DWALKIE_MUSE_DIRECT=1` 只在新环境里打开。两种模式的联网方式、按键含义都不一样，运行时切换要做模式记忆和界面提示，第一版不值得做。代价是切换模式要重新烧录。
2. **复用状态机和界面，只替换传输层**。新增 `MuseClient`（HTTPS 请求，用 `WiFiClientSecure` 加内置根证书）。直连模式下，`AudioLoop` 的开始、结束、取消动作，由它代替 WebSocket 消息来执行。回答通过现有的 `onTranscript(id, text)` 进入结果页，错误通过 `onError(id, code, retryable)` 进入出错页，界面代码不用动。
3. **录音先存后传**。录音帧写进复用的 30 秒 PSRAM 缓冲，松开按键后补上 44 字节的 WAV 头，一次性上传。原来的逐块发送依赖 WebSocket，HTTPS 上传改成整段更简单，也正好对应服务器一次转写一整段的接口。
4. **网络请求放在后台任务里做**。上传和提问在一个固定栈大小的 FreeRTOS 任务里执行，结果经队列交回主循环。转写中的转圈动画和按键不会被卡住。每次最多只有一个请求在进行。
5. **请求号**：每段录音生成一个 UUID，作为协议 2 的 `requestId`；202 重试和断线重试都沿用这个 UUID，保证同号重放不重复计费。
6. **配置**：在被 git 忽略的 `walkie_config.h` 里新增 `WALKIE_MUSE_HOST`（个人云域名）和 `WALKIE_MUSE_TOKEN`。`config.h.example` 里只放占位符。串口日志绝不打印令牌。
7. **根证书**：复用 `stackchan-openmuse-probe/src/certs.h` 已核对指纹的 ISRG Root X1、X2。所用的 Arduino-ESP32 2.0.17 不校验证书有效期，因此不需要先对时。

## Risks / Trade-offs

- [访客网要求网页登录] → 直连模式在公司不可用。烧录后第一步就实测；不可用就改到家里或手机热点下使用，代码不需要改。
- [HTTPS 握手慢] → MOSAICO 实测，每个请求到达云端前要花 4–10 秒，StackChan 的数据还没测。手表要做 status、transcribe、chat 三次请求。缓解方式：transcribe 和 chat 两次请求复用同一条 TLS 连接（keep-alive）；实测总时长写进验收记录。
- [内存] → TLS 大约占 40–50 KB 内部 RAM；录音放在 PSRAM，WAV 头在发送时现拼，不再额外复制一份录音。
- [无朗读] → 只能看一行回答。完整回答可以在手机的“问答记录”里看。

## Migration Plan

新增编译环境，不影响已有设备。回退方式：重新烧录 `m5stack-stopwatch`。
