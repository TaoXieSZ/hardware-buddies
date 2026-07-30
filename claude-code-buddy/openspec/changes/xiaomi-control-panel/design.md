# Design: xiaomi-control-panel

## Context

小咪固件（`cores3-stackchan-voice`）已完成并归档：PTT 对话、半双工音频、懒连接、费用护栏
均真机验证。配置项散在 NVS（`vidle`/`vturns`/`vol`/`bright`/`motion`/`idlew`/`tilt`）与
编译期常量（音色 `longpaopao_v3.6`、人设 instructions）里，运行时无法查看或修改。

已有一个可交互的面板原型（artifact 879f3954）确定了信息架构与控件集合，本变更是把它落地。

关键既有约束（来自已归档的 `cores3-voice-assistant` 排障记录）：
- 主循环在下载回复时会被 TLS 阻塞读卡住 **~550ms**；任何"必须按时发生"的事都不能挂在主循环上
  （音频播放因此已移交独立 FreeRTOS 任务）。
- `M5.Mic.record()` 期间主循环延迟会饿死触摸释放检测——采集期同样不能被新工作阻塞。

## Goals / Non-Goals

**Goals:**
- 浏览器打开 `http://<小咪IP>/` 即可看状态、调参数，无需 Mac、无需重新烧录。
- 设置改动即时生效（能即时的）并持久化到 NVS，重启后保留。
- HTTP 服务**不得**影响对话质量：不引入 underrun、不影响 PTT 松手检测。

**Non-Goals:**
- 鉴权 / TLS / 公网访问（局域网内使用，见 proposal 安全边界）。
- 面板远程发起对话、远程听音（只调参与观察）。
- OTA 固件升级（另开变更）。
- agent-fleet / pilot 层对接（下一个变更）。

## Decisions

1. **面板由设备自己托管（被技术约束逼定）。** 浏览器禁止 https 页面 fetch 局域网 http
   设备（mixed content），所以托管在 claude.ai artifact / Mac https 的页面**永远连不上小咪**。
   设备同时发网页和 JSON，浏览器直连 IP，一步到位且无 CORS 问题。
2. **网页 gzip 后嵌进固件**（`panel_html.h` 字节数组，构建期由 `tools/gen_panel_html.py`
   从 HTML 源文件生成），不落 LittleFS。理由：小咪自 cat_face 起已**不依赖 LittleFS**
   （无需 `uploadfs`），保持"一次 upload 就能用"的属性。原型 HTML ~25KB，gzip 后约 6KB。
3. **HTTP 服务跑独立 FreeRTOS 任务**（core 0，优先级低于播放任务），不挂主循环。
   直接照搬播放任务的成功经验：主循环被 TLS/mic 卡住时，面板照样响应；反过来，面板
   处理请求时也不会拖慢音频。
4. **跨任务写入走"暂存 → 主循环应用"**，不在 HTTP 任务里直接调 `motionSet*` /
   `M5.Speaker` / NVS 写入。HTTP 任务只做解析+校验，写进一个 pending 结构；主循环下一
   tick 取走并应用。理由：M5Unified 的外设 API 与 NVS 都不是为并发设计的，而这个
   stage→apply 结构成本极低。（形状同 agent-fleet 的 stage→confirm，纯属巧合但好记。）
5. **实时状态用轮询，不用 WebSocket/SSE。** 面板每 1s `GET /api/state`。设备已经维护着
   一条到 DashScope 的 WSS 客户端连接，再加一个 WS 服务端徒增复杂度与内存；1s 轮询对
   "看状态"完全够用。
6. **状态快照单向发布**：主循环把 FSM 状态、连接、轮次、延迟、usage、underrun 写进一个
   snapshot 结构，HTTP 任务只读。避免读到半更新的状态。
7. **API 形状**（全部 `application/json`）：
   - `GET /api/state` → 实时快照（状态、WiFi/DashScope 连接、轮次、上轮延迟与 token、
     underrun、电量、当前字幕）
   - `GET /api/settings` → 全部可调项当前值
   - `POST /api/settings` → 部分更新（只发改动的键），返回应用后的值
   - `POST /api/action` → `{"do":"dance"}` 让小咪跳一下、`{"do":"disconnect"}` 立即断开回打盹
8. **设置生效时机分两类**（面板需如实告知用户）：
   - **即时**：音量、亮度、动作三项（motion/idlew/tilt）、空闲秒数、轮数上限
   - **下次会话生效**：音色、人设 instructions —— 已建立的 DashScope 会话不重连
     （重连会丢上下文且额外计费）；面板对这两项显示"下次唤醒生效"。
9. **新增三个 NVS 持久化项**：音色（`vvoice`，字符串）、人设（`vpersona`，字符串）、
   说话时跳舞（`vdance`，bool）。编译期常量降级为**出厂默认值**，NVS 有值则覆盖。
10. **IP 发现与可用窗口**：首次连上 WiFi 后在字幕带显示 `面板 http://<ip>/` 若干秒。
    小咪是懒连接——WiFi 直到**首次唤醒**才关联，故面板在那之前打不开。
    **但打盹并不断开 WiFi**（核对代码确认：`goSleep()` 只调 `rtDisconnect()` 断云端会话，
    全项目无 `WiFi.disconnect()`），所以首次唤醒之后面板一直可用，打盹期间也能调参——
    这比"只有醒着才能调"实用得多。初稿 spec 曾误写成打盹不可访问，已按实际行为更正。

## Risks / Trade-offs

- [HTTP 任务与 WiFi 栈并发] → 只用 lwIP socket（`WebServer` 底层），不碰 M5 外设；
  真机需验证对话过程中打开面板不产生 underrun（沿用现成的 `[underrun]` 仪表当验收指标）。
- [懒连接 ↔ 面板可用性矛盾] 小咪打盹时没连 WiFi，面板打不开。→ 决策 10 已明确此行为；
  若日后不可接受，再单独提"面板保活"变更（代价是常驻 WiFi 耗电）。
- [无鉴权] 同网段任何人可改小咪参数、读人设文本。→ proposal 已声明边界；API key 不外泄。
- [gzip 网页嵌固件导致改版必须重烧] → 面板迭代期可临时用未压缩 + 本地文件调试；
  定版后再嵌入。flash 占用 ~6KB，可忽略。
- [面板轮询增加设备负载] → 1s 一次、快照只读，代价可忽略；若真机观测到影响再降频。

## Resolved

- **托管方式**（2026-07-19 与用户讨论后确认设备托管）：硬约束只有一条——云端 https 页面
  永远无法访问局域网 http 设备（mixed content + Private Network Access），所以 artifact
  版面板不可能连上小咪。技术上还有"本地 HTML 文件"和"Mac 起本地静态服务"两条路（设备回
  CORS 头即可），但都重新引入了"面板要靠另一台机器才能打开"的依赖，与小咪定位相悖。
  选设备托管，代价是**改面板 UI 需重烧固件**；开发迭代期允许临时用本地文件 + CORS 调 UI，
  定版再嵌入。
- **对话记录范围**：只显示**最近一轮**（当前字幕 + 上一轮问答文本），不做历史环形缓冲。
