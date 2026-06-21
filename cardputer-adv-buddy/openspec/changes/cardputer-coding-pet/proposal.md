## Why

Cardputer-ADV 现在只是一块「五态文字 HUD」——能看，但不好玩，也没用上这块卡片电脑最讨喜的特质：一块正对着你的小屏 + 新增的 BMI270 体感。把它变成一只**随你的 Claude Code 会话情绪起伏的电子宠物（Tamagotchi）**，让「等 agent 干活」这件枯燥的事变得有陪伴感：跑工具时它两眼放光，空闲久了打瞌睡，任务完成蹦跶庆祝，报错时垂头丧气，需要你批准工具时可怜巴巴盯着你求点头。

关键是这不用从零造轮子——直接站在**网上公开的成熟固件** [`meganetaaan/m5stack-avatar`](https://github.com/meganetaaan/m5stack-avatar) 肩膀上：它是 M5 生态最有名的开源「表情脸」库，程序化绘制会呼吸、眨眼、有 neutral/happy/sleepy/doubt/sad/angry 多种表情的脸，跑在 M5GFX 上、无需任何 GIF 美术资源。我们只做「Claude 会话状态 → 表情/心情」的映射这一层。

## What Changes

- **用程序化 avatar 脸替换当前的文字 HUD**：引入 `m5stack-avatar` 库，在 240x135 屏上渲染一张会呼吸眨眼的脸作为宠物主体。
- **会话状态 → 表情映射**：把已有的 5 态（IDLE/THINKING/TOOL/APPROVAL/DONE）映射到 avatar 表情 + 配色 + 气泡台词。状态来源沿用 Phase 1 的现有通道（当前是键盘自测切换；BLE 接入后无缝换成事件驱动）。
- **BMI270 体感互动**：拿起/晃动/翻转触发宠物反应——拿起=唤醒并看向你，晃动=被惊到/打个激灵，长时间静止=进入睡眠表情。基于 M5Unified 的 `M5.Imu`（已内置 BMI270 驱动）。
- **轻量 tamagotchi 心情循环**：一个随时间和事件演化的心情/精力模型——完成任务加心情、空闲过久变无聊、长时间待审批显出「焦虑」。心情反过来微调 avatar 的眨眼频率、看向方向等细节，让它「活」起来。

## Non-goals

- **不接 BLE**：真正的 BLE NUS「Claude-XXXX」状态推送是父项目 Phase 2 的事；本次只消费「现有状态源」这个抽象接口，BLE 接好后自动生效，本 change 不实现传输层。
- **不做声音**：ES8311 喇叭的叫声/提示音留到后续。
- **不用 GIF 角色包**：本次走 `m5stack-avatar` 程序化脸（无资源、省 flash）；clawd/calico GIF 路线是另一条独立分支，不在本次范围。
- **不做心情持久化**：心情存内存即可，不写 SD 卡跨重启保存。

## Capabilities

### New Capabilities
- `pet-avatar`: 在屏上渲染基于 m5stack-avatar 的程序化宠物脸，并把 Claude 会话状态映射为表情、配色与气泡台词。
- `motion-interaction`: 用 BMI270（经 M5Unified M5.Imu）识别拿起/晃动/静止等体感事件，驱动宠物的唤醒、惊讶、睡眠等反应。
- `pet-mood`: 一个随时间与会话事件演化的轻量心情/精力模型，调制 avatar 的细节表现，形成 tamagotchi 式的陪伴感。

### Modified Capabilities
<!-- 全新项目，openspec/specs/ 为空，无既有 capability 的需求变更。 -->

## Impact

- **依赖**：`platformio.ini` 的 `lib_deps` 新增 `m5stack-avatar`（procedural face，传递依赖 M5GFX/M5Unified，已在用）。
- **代码**：`src/main.cpp` 从「画文字 HUD」重构为「驱动 avatar + 心情 + 体感」；新增 `src/pet_avatar.*`（状态→表情映射）、`src/motion.*`（IMU 手势）、`src/mood.*`（心情模型）。已有的 `AgentState` 枚举保留并作为输入。
- **硬件**：用到 BMI270（ADV 专属，原版 Cardputer 无）；屏幕/键盘沿用 Phase 1 已验证的 `M5Cardputer.begin(cfg, true)` 初始化。
- **构建/验证**：仍以 `pio run -e cardputer-adv` 编译通过为第一道关；真机上按键切状态应看到表情切换、拿起/晃动有反应。
- **风险**：`m5stack-avatar` 默认面向更大屏（Core 320x240），需确认在 135px 高的小屏上的缩放/布局——属设计阶段要落实的点。
