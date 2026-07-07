# 01 — 硬件平台

> CoreS3 主机 + StackChan 底板 = M5Stack StackChan (SKU K151)

## CoreS3 主机

| 参数 | 值 |
|------|-----|
| SoC | ESP32-S3, 240MHz 双核 |
| Flash | 16MB |
| PSRAM | 8MB (octal qio_opi) |
| 显示屏 | 2.0" ILI9342, 320×240, SPI, CS=GPIO3 |
| 触摸 | FT6336 电容触摸, I2C 0x38, INT=GPIO21 |
| IMU | BMI270 6 轴, I2C 0x68 |
| RTC | BM8563, I2C 0x51 |
| 音频输出 | AW88298 功放, I2C 0x36, DIN=GPIO14 |
| 音频输入 | ES7210 ADC, I2C 0x40, DOUT=GPIO13 |
| I2S | BCK=GPIO34, WCK=GPIO33, MCLK=GPIO0 |
| 电源管理 | AXP2101 PMIC, I2C 0x34 |
| GPIO 扩展 | AW9523B, I2C 0x58 |

## 系统固定占用 GPIO (不可用作通用 GPIO)

| GPIO | 功能 |
|------|------|
| 0 | I2S MCLK (音频主时钟) |
| 3 | LCD SPI CS |
| 4 | SD card SPI CS |
| 11, 12 | 系统 I2C SCL/SDA (挂在 8 个设备上) |
| 13, 14 | I2S 音频数据 (DOUT/DIN) |
| 21 | 触摸屏 INT 中断 |
| 33, 34 | I2S WCK/BCK |
| 35, 36, 37 | LCD SPI (MISO/D-C, CLK, MOSI) |

## 可用作 GPIO 的排针

| GPIO | 排针 | 说明 |
|------|------|------|
| 6, 7 | — | StackChan 底板上的舵机 UART；不接底板时闲置 |
| 8, 9 | PortB | I2C-B / GPIO |
| 17, 18 | PortB | UART-B / GPIO |
| 43, 44 | PortA | I2C-A / GPIO |
| 5, 10 | Grove | LCD 辅助 / GPIO |
| 1, 2 | — | 辅助 I2C / GPIO |

## StackChan 底板

| 组件 | 芯片/引脚 | 说明 |
|------|-----------|------|
| Yaw 舵机 | FEETECH SCS0009, ID=1, UART1 | ±128° (连续旋转支持) |
| Pitch 舵机 | FEETECH SCS0009, ID=2, UART1 | 0-90° |
| 舵机 UART | GPIO6=TX, GPIO7=RX, 1Mbps 半双工 | SCSCL 协议 |
| 舵机电源 | PY32 IO 扩展器 pin0 (VM_EN), I2C 0x6F | 独立电源轨 |
| RGB LED ×12 | PY32 IO 扩展器 pin13 | 可独立寻址, RGB565 |
| 触控区 ×3 | Si12T, I2C 0x68 (GND 地址) | 前/中/后 |
| NFC | M5Unit-NFC | 全功能 NFC 模块 |
| IR | 发送+接收 | 红外遥控 |
| 电池监测 | INA226, I2C 0x41 | 电压+电流, 0.01Ω shunt |
| 电池 | 550mAh Li-Po | USB-C 充电 |

## 舵机 SCS0009 规格

- 品牌: FEETECH (飞特), SC/SCL 系列
- 尺寸: 23.2 × 12.1 × 25.25 mm, 13g
- 齿轮: 金属 (铜+钢), 1:416
- 电压: 4.0-7.4V DC
- 堵转扭矩: 2.3 kg·cm @6V
- 空载速度: 0.1 sec/60°
- 分辨率: 0.293° (1024 步/300°)
- 协议: TTL 半双工异步串行 @1Mbps
- ID 范围: 0-253, 单总线最大 253 个舵机
- 反馈: 位置/速度/负载/电压/温度/电流
