## Context

Phase 1 的 `src/main.cpp` 已跑通屏幕+键盘，并用一个 `AgentState` 枚举（IDLE/THINKING/TOOL/APPROVAL/DONE）+ 文字 HUD 表达会话状态，键盘任意键循环切换状态做自测，`pio run -e cardputer-adv` 编译通过。本次在此基础上，把「文字 HUD」升级为「会随情绪起伏的电子宠物」。

硬件约束：屏幕仅 240x135（横屏），CPU 是 ESP32-S3 双核 @240MHz，8MB flash。ADV 相对原版 Cardputer 多了 **BMI270 IMU**，由 M5Unified 内置驱动支持。会话状态来源目前是键盘自测（BLE 是父项目 Phase 2，不在本次）。

公开固件基础：[`meganetaaan/m5stack-avatar`](https://github.com/meganetaaan/m5stack-avatar) —— M5 生态最成熟的开源「程序化表情脸」库，跑在 M5GFX 上，内置呼吸/眨眼/视线动画与 `Expression`（Neutral/Happy/Sleepy/Doubt/Sad/Angry）枚举，无需任何美术资源。

## Goals / Non-Goals

**Goals:**
- 用 `m5stack-avatar` 的程序化脸替换文字 HUD，承载五态表情。
- 用 BMI270 体感（拿起/晃动/静止）驱动唤醒/惊讶/睡眠反应。
- 一个轻量内存心情模型，调制 avatar 细节，形成 tamagotchi 陪伴感。
- 保持「状态来源」与「表现层」解耦，使 BLE 接好后零改动切换事件源。

**Non-Goals:**
- 不实现 BLE 传输层（父项目 Phase 2）。
- 不做声音、GIF 角色包、心情持久化（见 proposal 的 Non-goals）。

## Decisions

### D1：表现层用 m5stack-avatar，而非自绘脸或 GIF
- **选择**：引入 `m5stack-avatar` 库，用其 `Avatar` + `Expression` API。
- **理由**：站在成熟公开固件肩膀上，省掉自绘表情/动画与 GIF 美术资源；程序化脸省 flash、天然有呼吸眨眼。契合「基于网上公开固件」的诉求。
- **备选**：(a) 继续自绘——工作量大且效果差；(b) 复用 buddy 家族的 AnimatedGIF+LittleFS 角色包——需要美术资源且占 flash，是另一条独立路线。
- **apply 阶段硬性要求**：`Avatar.init()` / `setExpression()` / `setSpeechText()` 等具体调用 MUST 对照 m5stack-avatar 仓库 `examples/` 逐字核对后再写（库版本、是否需要传入 M5GFX 实例、是否自起绘制 task 等以 upstream example 为准），不凭记忆写。

### D2：依赖引入方式
- **选择**：`platformio.ini` 的 `lib_deps` 增加 m5stack-avatar；优先用 PlatformIO registry 名，若 registry 不可用则用 git 直链 `https://github.com/meganetaaan/m5stack-avatar.git`（pin 到具体 release/commit）。
- **理由**：与父项目其它 env 的依赖写法一致（registry 优先，必要时 github 直链 + pin）。
- **风险见 R1**：该库历史上面向 Core 系列大屏与旧 M5Stack 库，需确认对 M5Unified/M5GFX 与小屏的兼容。

### D3：模块划分（表现/输入/状态解耦）
- `src/pet_avatar.{h,cpp}`：封装 avatar，提供 `applyState(AgentState)`（状态→Expression+强调色+气泡）与 `applyMood(Mood)`（心情→眨眼/视线微调）。
- `src/motion.{h,cpp}`：封装 BMI270，经 `M5.Imu` 读加速度，产出离散手势事件 `{PickedUp, Shaken, Still}`。
- `src/mood.{h,cpp}`：内存心情模型，输入会话事件+时间，输出有界 `Mood`。
- `src/state_source.h`：抽象「当前会话状态」接口；Phase 1 实现为键盘自测源（`KeyboardStateSource`），Phase 2 换 `BleStateSource` 时不动表现层。
- `src/main.cpp`：组合根——`setup()` 初始化 M5Cardputer + IMU + avatar；`loop()` 拉状态源→喂 mood→驱动 avatar，并轮询 IMU 手势。
- **理由**：让 BLE 接入只新增一个 state source 实现；符合最简且可独立验证。

### D4：IMU 手势识别走 M5Unified
- **选择**：`M5.Imu.getAccelData()`（M5Unified 已内置 BMI270 驱动）按固定频率采样，用加速度幅值的阈值 + 时间窗判定 PickedUp / Shaken / Still。
- **理由**：不裸写 BMI270 寄存器（符合约定）。具体 API 调用在 apply 阶段对照 M5Unified IMU example 核对。
- **备选**：用 BMI270 的硬件中断（any-motion）——更省电但更复杂，MVP 不需要。

### D5：状态/手势/心情如何合成为最终表情
- 主表情类别由**会话状态**决定（D1 映射表）。
- **手势**触发短时「覆盖反应」（唤醒看你/激灵/睡眠），结束后回落到状态表情。
- **心情**只做细节调制（眨眼频率、视线），不改主表情类别（与 pet-mood spec 一致）。
- 优先级：手势瞬时反应 > 状态主表情 > 心情微调。

## Risks / Trade-offs

- **R1：m5stack-avatar 在 240x135 小屏 + M5Unified 上的兼容性未验证** → apply 第一步就做最小集成 spike（仅 `Avatar` 显示默认脸）跑 `pio run` + 真机确认尺寸不裁切；不行则回退到「自绘简化脸」方案，但仍保留状态/心情/手势架构。
- **R2：avatar 库自带绘制 task 可能与 M5Cardputer 键盘扫描/主循环抢显示或抢 CPU** → 集成时确认库的绘制模型（是否独立 task），必要时降低绘制帧率或显式让出；键盘扫描 `M5Cardputer.update()` 保持在主循环。
- **R3：lib 在 PlatformIO registry 的名称/可用性不确定** → 用 github 直链 + pin commit 兜底（D2）。
- **R4：手势阈值在 ADV 上需实测标定**（拿起 vs 晃动易混淆）→ 阈值集中为常量，真机标定；先给保守默认。
- **Trade-off**：程序化脸放弃了 clawd/calico 的品牌形象一致性，换取零美术资源与快速落地；品牌 GIF 路线作为独立后续。

## Migration Plan

纯新增固件能力，无数据迁移。回退即把 `main.cpp` 还原为 Phase 1 的文字 HUD（保留在 git 历史），并从 `lib_deps` 移除 avatar 库。建议按 tasks 顺序小步提交，每步 `pio run` 绿灯，便于任一步回退。

## Open Questions

- ~~m5stack-avatar 当前 release 是否直接接受 M5GFX/M5Unified 的 display 实例？~~ **已解（apply Task 1 spike）**：pin `v0.10.0`，`#include <Avatar.h>` + `avatar.init()` 与 `M5Cardputer.begin(cfg,true)` 共存，`pio run` 链接 `libM5Stack-Avatar.a` 通过；avatar 用全局 `M5.Display`（M5Cardputer 基于 M5Unified，已初始化），无需手动传 display 实例。
- ~~五态 → Expression 的具体落位~~ **apply 已定**：Idle→Neutral / Thinking→Doubt / ToolUse→Happy(嘴 0.3) / Approval→Sad(视线上扬+「approve me?」) / Done→Happy(嘴全开)。ToolUse 与 Done 同 Happy 基底，靠嘴开度+强调色+台词区分；最终观感待真机目检微调。
- ~~是否需要「调试叠层」~~ **已定**：屏幕被 avatar 绘制线程独占，屏上叠层会被覆盖，故改为**串口调试**（编译开关 `-DPET_DEBUG_OVERLAY=1`）。
- **待真机确认（板子未到）**：① avatar 默认脸在 240x135 是否完整不裁切（不行则 `setScale/setPosition` 调，仍不行回退自绘脸，见 R1）；② BMI270 是否被 `M5.Imu` 识别为 `m5::imu_bmi270`（开机串口已打印自检）；③ 手势阈值 `MOVE_THRESH/SHAKE_THRESH/STILL_*` 标定。

## 烧录冲突根因 + Phase 2 串口所有权约束（探索发现，2026-06-18）

> 真机调试时发现：烧录 Cardputer-ADV 反复失败，根因是**两个独立拦路虎**叠加，且第二个预示了 Phase 2 的一个结构性约束。记录于此。

**拦路虎 #1 — bridge 占串口（守护进程造成）**
- `claude-code-buddy/tools/cc-bridge/bridge.py` 有个「有线 Tab5 peer」：`TAB5_SERIAL = env CC_BRIDGE_TAB5_SERIAL` → `serial.Serial(port, baud, timeout=0)`（`buddy_core/core.py:795`）常开不放，持续发 NDJSON 心跳、读回 btn/permission。
- macOS `usbmodemNNNN` 按 USB 物理槽位(LOCATION)分配；Cardputer 插进的槽正好是 bridge 盯着的 `/dev/cu.usbmodem21401`。cc-bridge + cursor-bridge 都配了同口 → 都开着(FD 11u)。
- 串口在 macOS 是独占设备 → esptool 再开报 `multiple access on port / device disconnected`。
- **最坑的是主动抢回**：`reconnect_loop`（`core.py:1201`，注释 `:756` "ensure_connected() reopens it... re-plugged stick reconnects within a minute"）把 esptool 复位设备造成的掉线当成"重插"，立刻重开口，和 esptool 抢 → `No serial data received`。光 kill 一次未必够（launchd 守护会重生；本次是 shell 起的，kill 即止）。

**拦路虎 #2 — USB-JTAG 复位不稳（与 bridge 无关，S3 硬件特性）**
- 端口释放后仍失败（baud 切换失败 / port doesn't exist / stub 掉线）。这是 ESP32-S3 原生 USB-Serial-JTAG「设备跑着固件时重进 bootloader」本身的毛病。解法：ROM 下载模式（OFF→按住 G0→上电→松手）+ `upload_speed=115200`（跳过波特率切换）+ app/littlefs 一次烧完。详见 README「烧录这块板子的坑」。

**Phase 2 约束 — 串口所有权仲裁**
- 这不是 bug，是结构性矛盾的预告：`烧录工具链(esptool 要独占写固件)` ⚔ `buddy 守护进程(要常开来驱动它)` 抢同一资源。
- 一旦做 Thread A（让 bridge 真正驱动 Cardputer），bridge 会像现在盯 Tab5 一样长期持有连接 → 每次想重烧固件都会撞。
- AhaKey 已有等价解法 `bluetoothConnectionOwner`（GUI vs daemon 仲裁 GATT 所有权）。串口/连接侧需要一个「我要烧录，请松手 N 秒」的握手，而非手动 kill。
- **岔路决策**：bridge 用 **BLE NUS** 驱动大多数 buddy，只有 **Tab5 走有线串口**（P4 + esp-hosted，BLE 链路麻烦）。Cardputer 有 BLE，**应走 BLE NUS（像 StickC）而非有线串口** → bridge 根本不碰串口，烧录时只剩 #2 的 JTAG 问题，#1 自动消失。Phase 2 接入方式优先 BLE。
