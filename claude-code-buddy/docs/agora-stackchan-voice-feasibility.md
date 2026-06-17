# StackChan 出声 — 可行性调研

> 目标：让 Agora ConvoAI agent 的语音从 **StackChan(CoreS3)** 的扬声器出来，
> 而不是 Mac。本文给两条落地路线的硬数据 + 推荐。
> 调研日期 2026-05-24。来源见文末。

## TL;DR

| | Path B 设备直连 | Path A2 Mac 中继音频 |
|---|---|---|
| 形态 | StackChan 自己加入 RTC，收发音频，脱离 Mac | Mac 当 RTC 客户端，把 agent TTS 推给 StackChan 放 |
| 工具链 | **ESP-IDF 5.2.3 + ESP-ADF 2.7**（换栈） | 现有 PlatformIO/Arduino 不变 |
| Agora SDK | IoT/嵌入式 RTC SDK，**90 天试用，之后需商业授权** | 免费的 Web RTC + ConvoAI（同 Path A baseline） |
| 回声(AEC) | CoreS3 **无硬件 AEC**，参考板靠 XVF3800 芯片做 → 半双工/PTT | 麦克风在 Mac，浏览器自带 AEC，**无此问题** |
| 音质 | 8kHz G.711 μ-law（电话音质） | 可 16kHz PCM，更好 |
| 现有固件功能 | 表情/舵机/LED/GIF 需移植到 ESP-IDF（Arduino-as-component） | **全保留**，只加一个 WiFi 音频接收 |
| Mac 是否必需 | 否（真独立） | 是 |
| 工期 | 周级 + 授权不确定性 | 数天 |
| **推荐** | 想做"独立产品"且能接受授权成本时再上 | ✅ **先做这条**，最快让声音从 StackChan 出来 |

**建议**：先做 **Path A2**——保留全部现有固件，几天内就能"声音从 StackChan 出来"，
零授权成本。等验证体验、确认要做脱离 Mac 的独立版，再评估 Path B 的授权和 ESP-IDF 重写。

---

## 硬件事实：CoreS3 音频通路（M5 官方 PinMap 确认）

CoreS3 音频齐全，I2S 全双工硬件没问题：

- **麦克风 ADC：ES7210**（I2C `0x40`）— 数据 DOUT=G13
- **扬声器功放：AW88298**（I2C `0x36`）— 数据 DIN=G14
- 共享 I2S：BCK=G34、WCK=G33、MCLK=G0；I2C 控制走 I2C_SYS（G11/G12）
- M5Unified 已经驱动两者（`M5.Speaker` / `M5.Mic`），所以**放音/收音在现有 Arduino 栈里就能做**。

➜ 硬件不是瓶颈。瓶颈在 **AEC（边放边听会听到自己）** 和 **软件栈**。

---

## Path B：StackChan 直连 Agora（真独立）

### 参考实现
Agora 官方 **Conversational AI Device Kit**。参考硬件 = Seeed reSpeaker XVF3800 +
XIAO ESP32-S3。设备端音频管线：

```
麦克风(XVF3800) → I2S → AEC/处理 → RTC编码(G.711 μ-law 8kHz)
   → Agora RTC 上行 → AI Agent v2 (云: ASR→LLM→TTS)
   → Agora RTC 下行 → RTC解码 → I2S → 扬声器
```

### 三个硬约束

1. **工具链要换**：需 ESP-IDF **v5.2.3** + ESP-ADF **v2.7** + 一个 IDF patch
   (`idf_v5.2_freertos.patch`)。现有固件是 PlatformIO + Arduino + M5Unified。
   要共存得用 "Arduino as ESP-IDF component" 把 M5GFX/舵机/LED 拉进 IDF 工程——可行但是大工程。
2. **授权**：嵌入式 RTC SDK 是 **90 天免费试用**，商业用途要联系 `sales@agora.io` 买授权。
   不像 web/mobile RTC 的免费额度。对个人项目这是真门槛。
3. **CoreS3 没有硬件 AEC**：参考板的回声消除是 **XVF3800 DSP 芯片**做的，CoreS3 没有。
   ESP-ADF 有软件 AEC（esp-sr）但 ESP32-S3 上效果有限。大概率只能**半双工/按键说话**
   （正好契合你现有 PTT 手势设计），否则 agent 会听到自己的 TTS。

### 还要付出
- 音质 8kHz μ-law（电话级）。
- 现有表情/舵机/LED/GIF 全部要在 ESP-IDF 下重新跑通。

### 换来
- 真正脱离 Mac 的独立桌面语音机器人。

---

## Path A2：Mac 收发，音频中继给 StackChan 放（推荐先做）

思路：**RTC 客户端仍在 Mac（浏览器）**，麦克风/AEC 都由浏览器处理；只把 agent 的
**TTS 音频**抓出来，经 WiFi 推到 StackChan 扬声器播放。StackChan 固件不换栈。

### 数据通路
```
浏览器(已订阅 agent 音频轨)
   → WebAudio AudioWorklet 抓远端 PCM，降采样 16kHz/PCM16
   → WebSocket 发给本地中继(daemon/小脚本)
   → UDP 推到 StackChan 的 IP
StackChan(Arduino/M5Unified):
   WiFi UDP 收 → 环形缓冲(jitter buffer) → M5.Speaker.playRaw(16kHz)
   → 同时用音频包络驱动嘴巴动画
```

### 要做的事（都在现有栈内）
- **浏览器侧**（改 buddy-voice 前端，baseline 已通，允许改）：加一个 AudioWorklet
  tap 远端 agent 音频轨 → WS 推 PCM；可选把浏览器本地播放静音，只让 StackChan 出声。
- **中继**：一个小 Python（可并入 daemon）WS→UDP 转发。
- **StackChan 固件**：新增 WiFi 连接 + UDP 音频接收 + ring buffer + `M5.Speaker.playRaw`，
  外加从音频能量驱动嘴部动画（嘴型同步）。

### 优点
- 零授权成本（用的还是 Path A 那套免费 ConvoAI + Web RTC）。
- 不换工具链，现有 GIF/舵机/LED/HUD 全保留。
- 麦克风在 Mac，**没有回声问题**。
- 音质比设备端 8kHz 好。

### 缺点
- 必须开着 Mac（不独立）。
- 自建音频流通路，要调 buffer/延迟、做嘴型同步。

---

## 推荐路线

1. **先做 Path A2**，几天内拿到"StackChan 出声 + 嘴型同步"的可玩版本，零成本零换栈。
2. 玩过之后若确实要"脱离 Mac 的独立机器人"，再单独立项做 **Path B**：先在 90 天试用内做
   spike 验证（ESP-IDF 工程 + Agora IoT SDK + CoreS3 软件 AEC/PTT 实测回声），评估授权成本后再决定。

## 已查清：固件已有 WiFi（Path A2 更省事）
`src/stackchan/wifi_stream.cpp/h` 已实现 **WiFi 连接 + 配网 + TCP socket + 重连**，
现用于权限手势时把摄像头 JPEG 帧推给 Mac（device→Mac）。`M5.Speaker.playWav` 也在用。
所以 Path A2 能复用现成 WiFi 栈，只需新增 **Mac→device 方向**的音频接收 + `playRaw` 流式播放。
工期比预估还短。

## 待确认（影响 Path A2 动手）
- 嘴型同步：用音频能量包络驱动现有 GIF 帧，还是新做一套口型？
- 浏览器本地是否静音（只 StackChan 出声）还是双声道都留。
- 音频传输：复用现有 TCP socket（加反向通道）还是新开 UDP（更低延迟、丢包可容忍）。

---

## 来源
- Agora ConvoAI Device Kit: https://www.agora.io/en/products/convoai-device-kit/
- Seeed reSpeaker XVF3800 + Agora ConvoAI 部署指南（含 ESP-IDF/ESP-ADF 版本、AEC、管线）:
  https://wiki.seeedstudio.com/respeaker_xvf3800_agora_convo_client/
- Agora ESP32 设备 demo（90 天试用/商业授权说明）:
  https://github.com/AgoraIO-Community/ag-iot-device-demo-esp32
- CoreS3 硬件 PinMap（ES7210/AW88298/I2S）: https://docs.m5stack.com/en/core/CoreS3
- Agora IoT SDK: https://www.agora.io/en/products/iot-sdk/
