### [Decision] Tab5 采用全局横屏后再接 Agent Farm

- **时间**: 2026-07-11T14:07:31+08:00
- **项目**: hardware-buddies / esp-claw
- **上下文**: 规划 Tab5 ESP-Claw 与新 Agent Farm 的双向联动。
- **内容**: 选择独立 `tab5-operator` 和主 Mac 受限 gateway；Tab5 整个 ESP-Claw 先通过 ESP32-P4 PPA 转为 1280×720 横屏，USB 位于左侧。设备不保存 Agent Farm 管理 token，gateway 只允许专用 definition 和脱敏事件。
- **收获**: 将显示基础、设备信任边界和生产控制面分层，可以分别验证与回退。

### [Progress] ESP-Claw 全局横屏真机启动

- **时间**: 2026-07-11T14:07:31+08:00
- **项目**: esp-claw
- **上下文**: 为 Agent Farm 桌宠 UI 建立横屏渲染基础。
- **内容**: 固件构建和全量烧录成功；启动日志确认逻辑 1280×720、物理 720×1280、PPA 旋转启用，用户确认 USB 在左侧时画面横向正立。触摸坐标已做逆变换，待横屏交互 UI 中完成实点验证。
- **收获**: ST7121 不支持硬件 swap_xy，正确路径是保持 DSI 原生时序并在 panel draw 边界统一用 PPA 旋转。

### [Progress] Agent Farm scoped gateway 进入只读 observe

- **时间**: 2026-07-11T14:55:15+08:00
- **项目**: agent-farm / hardware-buddies
- **上下文**: 在创建 `tab5-operator` 前验证最小权限事件链路。
- **内容**: Mac2 部署 `/api/tab5/events` 与固定 dispatch scoped routes；scoped token 与 dashboard admin token 强制隔离。主 Mac gateway 以 loopback、chat disabled、observe 模式持续建立 SSE，未创建 Agent 或发送消息。
- **收获**: 设备接入控制面时，专用路由和专用 token 应在源头限制权限，不能把管理员 token 交给下游代理再依赖“自觉不调用”。

### [Pitfall] Tab5 满背光导致 USB-C 区域严重发热

- **时间**: 2026-07-11T21:10:00+08:00
- **项目**: esp-claw
- **上下文**: 横屏 ESP-Claw 空闲显示小龙虾时，Tab5 USB-C 附近严重发热。
- **内容**: 静态画面仅略降温，排除 PPA 为主因；旧 SSH 固件 `setBrightness(100)` 是 0–255 量程约 39%，而 ESP-Claw `default_percent:100` 是真实满 duty。改为 40% 后 duty 1023→409，三分钟同条件复测明显降温。
- **收获**: 跨框架移植“亮度 100”必须先确认量程；M5GFX 的 100/255 与百分比 100% 完全不同。
