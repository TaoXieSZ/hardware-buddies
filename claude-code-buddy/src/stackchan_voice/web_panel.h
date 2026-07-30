#pragma once
#include "panel_state.h"

// 小咪控制面板的设备端 HTTP 服务（openspec change xiaomi-control-panel）。
//
// 为什么由设备自己托管：浏览器禁止 https 页面 fetch 局域网 http 设备
// （mixed content + Private Network Access），所以云端托管的面板永远连不上
// 小咪。设备同时发网页和 JSON，同源、无 CORS、且不依赖 Mac —— 与小咪
// "Mac 关机也能用"的定位一致。详见 design.md Resolved 段。
//
// 线程模型（照搬播放任务的成功经验，design 决策 3/4）：HTTP 在独立 FreeRTOS
// 任务里跑，主循环被 TLS 阻塞读卡住 ~550ms 时面板照样响应；反过来 HTTP 处理
// 也拖不慢音频。跨任务只走 panel_state.h 的两个结构，HTTP 任务从不直接碰
// 外设或 NVS。

// 启动服务（WiFi 已连上后调用）。幂等：重复调用只启动一次。
bool webPanelStart();
bool webPanelRunning();

// 主循环每 tick 调用：发布状态快照给面板读。
void webPanelPublish(const panel::Snapshot& snap);

// 主循环每 tick 调用：取走面板暂存的设置/动作改动。返回 false 表示没有待办。
bool webPanelTakePending(panel::Pending* out);

// --- 配网模式（openspec change xiaomi-wifi-provisioning） ---
// 与面板互斥：连上网才有面板，连不上才配网。两者共用同一个 WebServer 实例与
// HTTP 任务，避免两个 server 抢 80 端口（design 决策 6）。
//
// 开热点 + 强制门户（DNS 全解析到自身），手机连上后自动弹出配网页。
// 返回热点 IP 字符串供屏幕显示；失败返回 nullptr。
const char* webPanelStartApMode(const char* ap_ssid, const char* ap_pass);
void webPanelStopApMode();
bool webPanelInApMode();
// 用户在配网页提交过凭据后置位一次 —— 主循环据此结束配网、去连新网络。
bool webPanelTakeProvisioned();

// 面板当前设置的来源 —— 由主循环在启动时和每次应用后刷新，供 GET /api/settings 返回。
void webPanelPublishSettings(uint8_t volume, uint8_t brightness, uint16_t idle_sec,
                             uint8_t turn_limit, bool motion, bool idle_wiggle,
                             uint8_t tilt, bool dance, const char* voice,
                             const char* persona);
