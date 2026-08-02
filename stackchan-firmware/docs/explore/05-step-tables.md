# 05 — 步表 (Step Tables)

> 坐标体系 + 转换规则 + 4 种角色动作模式的完整解释

## 坐标体系

```cpp
struct ServoStep {
    int16_t  x;         // yaw 角度 (position) 或 yaw 速度 (rotation)
    int16_t  y;         // 相对于基线的垂直偏移 (spin 时忽略)
    uint16_t pace;     // 0..1000, 弹簧刚力度
    uint16_t dwell;   // 到达后停留 ms
    bool     spin;   // true → rotateYaw(x), false → move(x, y, speed)
};
```

Sentinel: dwell==0 && pace==0 && !spin.

## Y 轴到物理含义的映射

Y=0 (0°): 低头, 下巴贴胸口
Y=450 (45°): 平视前方
Y=600 (60°): 默认基线 (PITCH_BASELINE), 头自然 idle 位置
Y=900 (90°): 头竖直朝天

## 步表字段的精确语义

x (位置模式): 绝对位置, 十分之一度. 上次 x=+500 后这次 x=-500 意味着从右 50° 摆到左 50°.
x (旋转模式/spin): 旋转速度, -1000 (CW) ~ +1000 (CCW). 约 2.5s 转一圈 (IDLE 使用).
y: 相对于 PITCH_BASELINE(600) 的偏移. y_abs = 600 + y, clamp [0, 900].

## 当前 4 种动作模式

### PAT_IDLE — 宽幅扫视 + 旋转花活 (~20s 循环)

慢速扫视 ±20° (±200) + 缓慢旋转花活 (spin, velocity 250).
包含左右扫视 + 抬头 + 旋转 + 低头. servoTick() 内部自动处理
spin 的模式切换 (位置↔PWM).

### PAT_THINKING — 极端角度思考 (~10s 循环)

极端 yaw 角度 (±50°) + 极高仰头 (pitch 78°).
来回左右看, 停顿时间长 (3.5s), 呈现 "深度思考" 姿态.

### PAT_REPLYING — 旋转庆祝 + 点头 (~5s 循环)

混合两种模式:
1. 旋转段: CW spin (velocity 400, 1s) → 暂停 → CCW spin → 暂停
2. 点头段: 快速上下点头 (pitch +150 → -20, speed 300, 350ms dwell)
3. 归位: 平稳回中线

### PAT_ERROR — 剧烈摇头 (~1.5s 循环)

±300 (±30°) @ speed 600, 共 6 次快速甩头.
呈现清晰的 "NO NO NO" 头部摇动. 最后一步归位.
