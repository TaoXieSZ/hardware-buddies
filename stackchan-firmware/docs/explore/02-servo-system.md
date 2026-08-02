# 02 — 舵机系统

> 五层驱动栈 + 弹簧物理引擎 + FEETECH 协议

## 五层驱动栈

### 1. 项目动作引擎 — motion.cpp
AgentState Step 表, motionTick() 步进播放器, 运行时控制。

### 2. BSP 协调层 — Motion 类 (StackChan-BSP)
管理两个 unique_ptr<Servo>, std::mutex 线程安全,
FreeRTOS 独立任务 (50Hz, 4096B 栈, 优先级 10), IK 路径规划。

### 3. 弹簧物理层 — Servo 抽象类 (StackChan-BSP)
解析解弹簧 (AnimateValue + Spring uitk 框架),
speed → stiffness 二次映射 (10 → 650), 自动转矩释放 (200ms 静止后解锁)。

### 4. 硬件对接 — ScsServo (M5StackChan.cpp 嵌类)
BSP 角度 → FEETECH rawPos 转换: rawPos = zeroPos + angle × 16 / 5 / 10,
check_mode 模式切换 (Position ↔ PWM), NVS 零位持久化。

### 5. FEETECH 协议 — SCSCL → SCS → SCSerial

| 类 | 职责 |
|---|------|
| SCSerial | UART1 硬件驱动: uart_driver_install + uart_set_pin + uart_write_bytes |
| SCS | 帧打包: writeBuf — [0xFF][0xFF][ID][Len][INST][Addr][Data...][~CHK] |
| SCSCL | WritePos/ReadPos/EnableTorque/WritePWM/SwitchMode |

## 当前舵机通信引脚

| 功能 | GPIO | 细节 |
|------|------|------|
| 舵机通信 TX | GPIO_6 | UART1, 1Mbps, 8N1 |
| 舵机通信 RX | GPIO_7 | UART1, 1Mbps, 8N1 |
| 舵机电源控制 | PY32 IO Expander pin 0 | I2C @ 0x6F (VM_EN) |

初始化 (M5StackChan.cpp:259): _scs_bus.begin(UART_NUM_1, 1000000, 6, 7)

## 调用链

```
motionTick() → servoTick()
  → M5StackChan.Motion.move(x, y, speed)         [BSP 协调层]
    → Servo::moveWithSpeed(angle, speed)           [弹簧物理]
      → map_speed_to_spring_options(speed)          [刚度映射]
      → moveWithSpringParams(angle, k, d)

[FreeRTOS 独立任务, 50Hz]
servo update()
  → _angle_anim.updateWithDelta(0.02f)             [AnimateValue]
    → Spring::next(t = 0.02)                       [解析解弹簧]
  → set_angle_impl(angle)                          → ScsServo
    → _scs_bus.WritePos(ID, rawPos, 20, 0)         [FEETECH 协议]
      → uart_write_bytes(UART1, buf, len)           [UART 发送]

[旋转模式 (rotateYaw)]
  → ScsServo::rotate(velocity)
    → check_mode(Mode::PWM)
    → _scs_bus.WritePWM(ID, mapped_velocity)
```

## 弹簧物理

数学模型: m × x'' + c × x' + k × x = 0

- 无阻尼角频率: ω₀ = √(k/m)
- 阻尼比: ζ = c / (2 × √(k×m))
- 临界阻尼 (ζ=1): x(t) = target - exp(-ω₀t) × (c₂ + c₁t)
- 解析解 (非欧拉积分), 零数值漂移
- 收敛: |velocity| ≤ restSpeed && |target - position| ≤ restDelta

### Speed → Stiffness 映射

stiffness = 10 + (speed/1000)² × 640    [10@0 → 650@1000]
damping   = 2 × √(stiffness)             [临界阻尼]

## 两个舵机的配置 (servo_init)

| 参数 | Yaw (ID=1) | Pitch (ID=2) |
|------|-----------|--------------|
| 零位 (defaultZeroPos) | 460 | 620 |
| 角度范围 (angleLimit) | -1280~+1280 (-128°~+128°) | 0~900 (0°~90°) |
| 原始位置范围 (rawPosLimit) | 0~1000 | 0~1000 |
| 连续旋转 (enablePwmMode) | ✅ | ❌ |
| 角度转换公式 | rawPos = 460 + angle × 16/5/10 | rawPos = 620 + angle × 16/5/10 |

## FEETECH SCSCL 寄存器映射

| 类型 | 地址 | 宏 | 含义 |
|------|------|-----|------|
| SRAM RW | 40 | TORQUE_ENABLE | 力矩使能 |
| SRAM RW | 42-43 | GOAL_POSITION | 目标位置 |
| SRAM RW | 44-45 | GOAL_TIME | 运动时间 |
| SRAM RW | 46-47 | GOAL_SPEED | 速度 |
| SRAM RO | 56-57 | PRESENT_POSITION | 当前位置 |
| SRAM RO | 62 | PRESENT_VOLTAGE | 电压 (0.1V) |
| SRAM RO | 63 | PRESENT_TEMPERATURE | 温度 (℃) |
| SRAM RO | 66 | MOVING | 运动状态 |
