# 03 — 固件分支

> 同一硬件 (CoreS3 + StackChan 底板) 上三套固件的全景

## 三套固件对比

| 维度 | claude-code-buddy | claude-desktop-buddy | agent-farm (当前) |
|------|-------------------|---------------------|--------------------|
| 位置 | hardware-buddies/.../src/stackchan/ | claude-desktop-buddy/src/stackchan/ | agent-farm/stackchan-firmware/src/ |
| 用途 | BLE 桌面伴侣 (Claude/Cursor IDE 代理) | 镜像副本 (2026-05-24 快照) | 触屏 → USB Serial → personal-agent |
| 舵机代码 | 7 CharState + FEETECH BSP | 同 | 4 AgentState + FEETECH BSP + 旋转支持 |
| 面部渲染 | GIF 角色包 (AnimatedGIF + LittleFS) | 同 | M5GFX 矢量机器人猫 |
| 角色状态数 | 7 (SLEEP/IDLE/BUSY/ATTENTION/CELEBRATE/DIZZY/HEART) | 同 | 4 (IDLE/THINKING/REPLYING/ERROR) |
| HUD | ACNH 风格卡片 (上下文%、Token、Model、电池) | 同 | 底部文本带 (16px CJK) |
| 通信 | BLE NUS (b0c2dbe6-* UUIDs) | 同 | USB 串口 @115200 (@ASK/@REPLY) |
| 音频 | WAV 回放 + PCM/WS 音频中继 (Path A2) | 同 | beep() only |
| Agent 后端 | cc-bridge daemon (本地 Claude Code/Cursor) | cursor-bridge daemon | dispatch + agent-host (distributed) |

## claude-code-buddy 关键文件 (src/stackchan/)

| 文件 | 职责 |
|------|------|
| main.cpp (637行) | BLE 协议 + 角色状态机 + 屏幕休眠 + 触屏唤醒 + 摄像头 |
| character_chan.cpp (840行) | GIF 管道 + ACNH HUD 卡片 + CJK 字幕 + 电池指示器 |
| motion.cpp (216行) | 7 种 CharState Step 表 + motionTick() |
| settings.cpp (122行) | NVS 持久化: volume/brightness/motion/tilt/char_name |
| sound.cpp (120行) | LittleFS WAV 加载 + playWav 调度 |

## agent-farm 当前 main.cpp 结构 (774行)

| 行号 | 模块 | 功能 |
|------|------|------|
| 1-92 | 文件头 + include + layout | AgentState 枚举, prompts, 坐标常量 |
| 94-182 | 舵机部分 | 4 张步表 (支持 rotateX) + PATTERNS[] + PITCH_BASELINE |
| 184-202 | 动画计时器 | 6 个 millis 全局变量 |
| 204-415 | 表情绘制 | 机器人猫脸 4 种表情 |
| 321-328 | 状态机 + 舵机/LED 步进 | setAgentState → motionSetState → motionTick() |
| 536-563 | 眨眼引擎 | 300ms 周期, 3-5s 间隔 |
| 565-624 | 表情调度 + 总 tick | drawRobotFace + animateAll |
| 626-702 | UI / 协议 | showLines, readProtocolLine, sendTurn |
| 704-774 | setup / loop | 初始化 + 触控轮询 |

## 舵机模式支持 (当前固件特有)

与 claude-code-buddy 的纯位置模式不同，当前固件额外支持 **旋转模式**：

```cpp
struct ServoStep {
    int16_t  x;         // yaw angle (position) or yaw velocity (rotation)
    int16_t  y;         // pitch delta (ignored when rotateX)
    uint16_t speed;     // 0..1000
    uint16_t dwellMs;   // ms to wait
    bool     rotateX;   // true → rotateYaw(x), false → move(x, y, speed)
};
```

IDLE 包含旋转花活，REPLYING 包含旋转+点头混合模式。

## 从 claude-code-buddy 可借鉴的设计 (轻量)

| 设计点 | 价值 | 移植难度 | 说明 |
|--------|------|----------|------|
| 消息状态卡片 (ACNH 风格) | 高 | ~50行 | fillRoundRect + 文字换行 |
| CJK 字幕滚动 | 中 | ~40行 | M5Canvas sprite + right-to-left marquee |
| THINKING 随机表情 | 中 | ~15行 | 类似 BUSY 随机 busy_0/1/2 |

以上全部在现有矢量框架内轻量实现，零新文件，零新依赖。
