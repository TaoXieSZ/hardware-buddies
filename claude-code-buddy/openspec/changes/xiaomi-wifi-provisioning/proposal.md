# Proposal: xiaomi-wifi-provisioning

## Why

小咪的 WiFi 凭据编译期烧进固件（`-DSTACKCHAN_WIFI_SSID/PASS`），换一次网络就得接电脑
重新编译烧录——2026-07-19 一天之内为此重烧三次。用户想把它当桌宠**带着上下班**，
现状下每换一个地方都要带电脑，不可用。

设备本身有屏、有触摸、有摄像头、有 WiFi，完全可以自己完成配网，不该依赖开发机。

## What Changes

- **凭据从固件搬进 NVS，支持多组网络**：存最多 5 组 (SSID, 密码)，用 `WiFiMulti`
  扫描现场自动连上认识的那个。编译期 flag 降级为"出厂种子"——NVS 为空时灌入一次，
  此后 NVS 为准（保证现有设备升级后行为不变）。
- **控制面板新增网络管理**：列出已存网络（只回 SSID，**密码只写不读**）、扫描附近
  网络辅助选择、增删改。解决"出门前先在家把公司网加上"。
- **热点配网兜底**：连不上任何已知网络时，小咪自己开热点（`小咪-setup`，密码显示在
  屏幕上）+ 强制门户，手机连上去自动弹出配网页，选网络输密码即存。到陌生地方无需电脑。
- 屏幕字幕在各配网阶段给出人话提示（正在找网络 / 开热点了请连它 / 连上了）。
- **不改动**对话链路（PTT、半双工、懒连接、费用护栏）与控制面板既有功能。

## Capabilities

### New Capabilities

- `xiaomi-wifi-provisioning`: 多网络凭据存储与选择、面板端网络管理、热点强制门户配网
  的完整行为，以及各阶段的屏幕反馈与安全边界。

### Modified Capabilities

- `xiaomi-control-panel`: 新增"网络管理"API 与页面区块的需求；原有状态/设置/动作
  需求不变。

## Impact

- 新增：`src/stackchan_voice/wifi_store.{h,cpp}`（NVS 多凭据存储 + WiFiMulti 连接）、
  `src/stackchan_voice/wifi_setup_ap.{h,cpp}`（SoftAP + DNS 强制门户 + 配网页）。
- 修改：`web_panel.cpp`（网络管理 API）、`panel.html`（网络区块）、
  `main.cpp`（唤醒时走新的连接流程、连不上则进配网模式）。
- 依赖：arduino-esp32 自带 `WiFiMulti` 与 `DNSServer`（无新增第三方库）。
- **安全边界**：
  - 面板与配网页**永不回显已存密码**（只写不读）；列表只给 SSID。
  - 配网热点带密码（显示在设备屏幕上），避免路人连上改配置。
  - 配网模式有超时（默认 10 分钟无人配置则关闭热点回打盹），不长期广播。
  - 已存密码明文躺在 NVS —— 物理接触设备者可读出。家用/办公可接受，
    与"设备不该带去不可信场所"的常识一致。
- 不触碰：buddy 固件、BLE 桥、cc-bridge 守护进程。
