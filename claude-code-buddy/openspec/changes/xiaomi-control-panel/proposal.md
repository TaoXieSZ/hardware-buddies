# Proposal: xiaomi-control-panel

## Why

小咪（`cores3-stackchan-voice`）现在只能靠重新编译烧录来改配置：空闲断开时长、轮数上限、
音量、音色、人设、动作开关全都埋在 NVS 和 build flag 里，用户看不见也改不了。同时也没有
任何窗口能看它的实时状态（在做什么、连没连上、这轮花了多少 token、卡没卡顿）。

设备本身有 WiFi 和富余的 flash/PSRAM，完全可以自己托管一个网页控制台——浏览器输 IP 就能
调参和观察，不需要 Mac、不需要重新烧录。这也延续小咪"不依赖 Mac"的定位。

## What Changes

- 小咪固件新增**设备端 HTTP 服务**（LAN，无鉴权，见 Impact 的安全边界）：
  - 托管控制面板网页本身（gzip 后嵌在固件里，不占 LittleFS）
  - JSON API：读实时状态、读/写设置、触发动作
- 控制面板网页从原型（artifact 879f3954）落地为真页面：实时状态、对话记录、
  设置（空闲/轮数/音量/亮度/音色/人设）、动作（摇头跳舞/东张西望/抬头角度）、用量。
- 设置写入即时生效并持久化到 NVS；音色与人设需要在下次建立会话时生效（会话已建立时不重连）。
- 语音固件启动后在屏幕字幕短暂显示自己的 IP，便于用户找到面板地址。
- **不改动**任何现有对话行为（PTT、半双工、懒连接、费用护栏均不变）。

## Capabilities

### New Capabilities

- `xiaomi-control-panel`: 小咪设备端控制台的行为——HTTP 服务生命周期、状态/设置/动作
  三类 API 的语义、设置生效与持久化规则、以及不得干扰语音链路的约束。

### Modified Capabilities

- `cores3-voice-assistant`: 新增"音色/人设/动作参数可在运行时经面板调整"的需求；原有
  对话、半双工、懒连接、费用护栏需求不变。

## Impact

- 新增：`src/stackchan_voice/web_panel.{h,cpp}`（HTTP 服务 + API）、
  `src/stackchan_voice/panel_html.h`（gzip 后的网页字节数组）、构建期把面板 HTML
  转成头文件的脚本（`tools/gen_panel_html.py`）。
- 修改：`src/stackchan_voice/main.cpp`（启动 HTTP 服务、暴露状态给面板）、
  `settings.cpp`（新增音色/人设/说话跳舞三个可持久化项）。
- 依赖：arduino-esp32 自带 `WebServer`（无新增第三方库）。
- **安全边界**：服务仅监听局域网、无鉴权、无 TLS——面板能读到人设文本与用量，不暴露
  DashScope API key（key 只留在固件里，API 永不回传）。适用于家庭/办公内网，
  不要把设备暴露到公网。
- 不触碰：buddy 固件、BLE 桥、cc-bridge 守护进程、其他设备。
