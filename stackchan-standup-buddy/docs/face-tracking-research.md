# StackChan 人脸检测 + 头部朝向用户 — 技术方案调研

- 日期:2026-08-10
- 目标硬件:M5Stack CoreS3(ESP32-S3 + 8MB PSRAM)+ StackChan 机身(Feetech SCS0009 舵机 ×2)
- 目标固件:`stackchan-standup-buddy`(Arduino / PlatformIO / espressif32@6.12.0 = Arduino core 2.x / IDF 4.4,millis() 非阻塞单线程 loop)
- 需求:机器人脸(屏幕)始终朝向用户,保证桌面任意位置都能看到倒计时

---

## TL;DR(结论先行)

**推荐方案:Mac 端人脸检测 + USB 串口下发偏航目标(方案 B)。**

理由一句话:CoreS3 摄像头的 SCCB 与 StackChan-BSP 赖以工作的内部 I2C 总线(G11/G12)共用引脚,片上开摄像头会和触摸/LED/IO 扩展打架;而且设备本来就 USB 拴在 Mac 上,Mac 端用 Vision framework / OpenCV 做人脸检测精度高、零固件侵入、当天可落地。片上方案(方案 A)技术上可行(EloquentEsp32cam + HumanFaceDetect 恰好支持 Arduino core 2.x / IDF 4.4),留作二期。

---

## 1. CoreS3 摄像头:型号、引脚、总线冲突分析

### 1.1 硬件规格(已核实)

- 型号:**GC0308**,0.3MP(640×480),排线与 LTR-553ALS-WA 接近传感器一体,SCCB(类 I2C)地址 **0x21**。
  来源:[CoreS3 官方文档](https://docs.m5stack.com/en/core/CoreS3)、[StackChan 官方文档](https://docs.m5stack.com/en/StackChan)
- 引脚(来源同上,PinMap 一节):

  | 信号 | GPIO |
  |---|---|
  | SIOC (SCCB 时钟) | **G11** |
  | SIOD (SCCB 数据) | **G12** |
  | VSYNC / HREF / PCLK | G46 / G38 / G45 |
  | D0–D7 | G39, G40, G41, G42, G15, G16, G48, G47 |
  | XCLK / PWDN / RESET | -1 / -1 / -1(无需) |

- 用户印象「GC0308」**属实**。

### 1.2 库支持

- **M5Unified 不封装摄像头**;M5 官方 Arduino 示例是直接用 esp32-camera(`esp_camera_init`),配置即上表引脚。
  来源:[CoreS3 Camera 官方示例](https://docs.m5stack.com/en/arduino/m5cores3/camera)
- esp32-camera 在 Arduino core 2.x(IDF 4.4)里随核心自带,GC0308 是受支持传感器,兼容性没有问题。

### 1.3 关键问题:SCCB 与内部 I2C 总线共用引脚

**摄像头 SCCB 的 G11/G12 就是 CoreS3 的内部系统 I2C 总线(I2C_SYS)。** 同一条总线上挂着(来源:[CoreS3 PinMap / I2C Address Table](https://docs.m5stack.com/en/core/CoreS3)):

- FT6336 触摸 0x38、AW88298 功放 0x36、ES7210 0x40、BMI270 IMU 0x69、AXP2101 PMU 0x34、BM8563 RTC 0x51、GC0308 0x21、LTR553 0x23

而 StackChan 机身设备同样挂在 G11/G12 上(来源:[StackChan PinMap](https://docs.m5stack.com/en/StackChan) 与 [StackChan-BSP 源码](https://github.com/m5stack/StackChan-BSP)):

- Si12T 三区触摸 0x68、PY32L020 IO 扩展 0x6F(RGB LED ×12、舵机电源使能)、INA226 电池计 0x41、NFC 0x50
- **舵机本身走 UART**(SCSCL 总线,UART1 @ 1Mbps,G6/G7,见 `StackChan-BSP/src/M5StackChan.cpp` 的 `_scs_bus.begin(UART_NUM_1, 1000000, 6, 7)`),**舵机控制与摄像头无冲突**;`PY32IOExpander` 默认构造参数就是 `m5::In_I2C`。

M5 官方摄像头示例在 `esp_camera_init` 前显式调用 `M5.In_I2C.release()`——即官方做法就是**把整条内部总线让渡给摄像头的 SCCB 位bang**。后果:

- 摄像头工作期间,`M5StackChan.update()` 里的 Si12T 触摸轮询、每 80ms 一次的 RGB LED 刷新(走 PY32 扩展器)、IMU/PMU 访问全部失去总线;
- 地址虽不冲突(0x21 vs 0x6F/0x68/...),但 esp32-camera 的 SCCB 软件实现与 M5Unified 的 I2C 驱动不能同时驱动同一对引脚,交替 release/re-init 属于未验证的 hack;
- 我们的固件恰恰重度依赖这条总线(触摸拍头确认 + LED 环 + IMU 拍头检测)。

**结论:片上摄像头与现有 BSP 功能存在硬冲突,是方案 A 最大的工程障碍,而非算力问题。**

---

## 2. 片上人脸检测(ESP-DL / HumanFaceDetect)现状

### 2.1 模型与性能

- ESP32-S3 上的人脸检测模型为 **MSR_S8_V1 + MNP_S8_V1 两级模型**(新版组件 `espressif/human_face_detect`,仅支持 S3 和 P4)。官方延迟数据:一级 MSR(120×160×3 输入)模型推理 ~32ms + 预处理 ~10ms;二级 MNP 每个候选脸 ~5.5ms。
  来源:[human_face_detect 组件文档](https://components.espressif.com/components/espressif/human_face_detect)
- 社区实测:EloquentEsp32cam 在 S3 上标称 **快速模式 ~50ms / 精确模式 ~80ms 每帧**(含五点关键点),加上 240×240 取帧,实际闭环约 5–10 fps——对头部跟踪绰绰有余。
  来源:[EloquentArduino: ESP32S3 cam Face Detection](https://eloquentarduino.com/posts/esp32-cam-face-detection)
- 内存:模型放 flash,推理张量 + RGB 帧缓冲需要 PSRAM(我们已有 `BOARD_HAS_PSRAM`,8MB OPI,够用)。

### 2.2 IDF / Arduino 版本匹配(与我们 IDF 4.4 的卡点)

- 新版 esp-dl(v3.x,组件仓库现行版本)面向 ESP-IDF 5.x,且人脸检测只保留 S3/P4;**Arduino core 3.x 已把旧人脸检测库移除**([arduino-esp32 issue #10881](https://github.com/espressif/arduino-esp32/issues/10881))。
- 旧版 esp-dl v1.1.0(2022 时代,支持 ESP32/S2/S3/C3)官方建议搭配 IDF master,对 IDF 4.4 是可用但已被官方放弃维护的组合([esp-dl v1.1.0 README](https://components.espressif.com/components/espressif/esp-dl/versions/1.1.0/readme?language=en)、[esp-dl 仓库](https://github.com/espressif/esp-dl))。
- **可行路径是 EloquentEsp32cam(≥2.2)**:它把 HumanFaceDetect 模型头文件(`human_face_detect_mnp01.hpp` 等,见其 `src/eloquent_esp32cam/face/`)打包成纯 Arduino 库,**明确要求 ESP32 Arduino core 2.x(3.x 不可用)**——恰好命中我们 espressif32@6.12.0 的平台。
  来源:[EloquentEsp32cam S3 课程环境要求](https://eloquentarduino.com/esp32s3-camera-mastery-course-intro)、[Arduino 论坛:库内人脸检测头文件](https://forum.arduino.cc/t/human-face-detect-mnp01-hpp-no-such-file-or-directory/1334499)
- 注意点:检测只支持 240×240 帧;需自定义 CoreS3 引脚(库内置 pinout 不含 CoreS3,需手写 §1.1 引脚表);`detection.run()` 阻塞 50–80ms,要放进现有 millis() tick 框架需接受该节拍抖动(GIF 动画会卡一下),或挪到 core 0 的 FreeRTOS 任务。

### 2.3 片上方案工作量评估

取帧(esp32-camera,官方示例 50 行)+ EloquentEsp32cam 检测 + 视觉伺服闭环,代码量不大;真正的成本在 **§1.3 的总线让渡**(要在摄像头与触摸/LED 之间二选一或做总线仲裁),以及与现有 GIF 刷屏的 CPU/带宽争抢。ESP-WHO([esp-who](https://github.com/espressif/esp-who))是完整应用框架,但要求 IDF 工程,与我们 Arduino 固件不兼容,不适用。

---

## 3. 社区先例

- **meganetaaan 的 stack-chan(Moddable 固件)没有片上人脸跟踪。** 它的 face_tracker MOD 是**外置 UnitV2 AI 摄像头**(K210,自带人脸检测)通过 HTTP 把人脸坐标发给机器人,`robot.follow(target)` 执行跟随——即「检测外置、串口/网络下发目标」的架构。
  来源:[Hackaday log: Stack-chan following human face](https://hackaday.io/project/181344/log/197815-stack-chan-following-human-face)、[face_tracker/mod.js 源码](https://raw.githubusercontent.com/meganetaaan/stack-chan/main/firmware/mods/face_tracker/mod.js)
- stack-chan 现行固件另提供 **MediaPipe BLE tracking**:在 PC/手机浏览器里用 Google MediaPipe 跑脸/手跟踪,结果经 BLE 发给机器人——又一个「主机端检测 + 无线下发」先例。
  来源:[stack-chan 仓库 README(Features: MediaPipe BLE tracking)](https://github.com/stack-chan/stack-chan)
- **M5Stack 官方 StackChan 出厂固件/StackChan-BSP 仓库内没有片上人脸跟踪示例**;BSP 只有舵机/LED/触摸/NFC 驱动([StackChan-BSP](https://github.com/m5stack/StackChan-BSP))。官方 App 的 Avatar 功能同样是**手机端采集头部动作**映射到机器人。
- UnitV / Unit Cam 类外置摄像头在本项目语境下无必要——Mac 本身就是更好的「外置摄像头」。

**模式很清晰:StackChan 生态里所有人脸跟踪先例都把检测放在机器人之外。**

---

## 4. Mac 端方案(推荐)

### 4.1 架构

Mac 摄像头 → 人脸检测(macOS **Vision framework** `VNDetectFaceRectanglesRequest`,Swift 小程序,几十行;或 Python + OpenCV DNN/YuNet)→ 得到人脸中心 x(归一化 0..1)→ 映射为 yaw 目标 → **USB 串口行文本**下发 → 固件 P 控制驱动 yaw 舵机。

### 4.2 优劣

优点:
- 检测精度/帧率碾压 GC0308 + HumanFaceDetect(Vision 在 Apple Silicon 上 30–60fps,带 landmarks;Mac 摄像头 1080p,弱光表现好得多);
- 零固件侵入:不动总线、不动 loop 节拍,只加一个串口解析 tick;
- 设备本来就 USB 连 Mac(充电+通信),无新增硬件;
- 开发量最小,当天可出原型。

缺点/风险:
- **依赖 Mac 在跑**且程序存活;Mac 睡眠/拔线时跟踪失效(固件需优雅降级为现状行为);
- **几何标定问题**:Mac 摄像头里的人脸 x 偏移 ≈ 用户相对 Mac 的方位,只有当 StackChan 固定摆在 Mac 旁时才能映射成机器人的 yaw 目标。桌面位置固定,这个假设成立;建议做个两点标定(「看机器人」「看屏幕左/右缘」)存下映射系数;
- 若用户离开 Mac 摄像头视野(起身走动),跟踪必然丢失——但对「坐下来能看到倒计时」这个需求,丢跟踪=回中即可。

### 4.3 串口协议建议(沿用行文本风格)

115200 8N1,每条一行 `\n` 结尾,Mac → 设备 10–20Hz:

```
TRACK <cx> <conf>\n    cx = 人脸中心 x,归一化 0..1000(500=居中);conf = 0..100
LOST\n                 人脸丢失(固件保持原位,超时后缓慢回中)
YAW?\n  /  YAW <val>\n 可选:查询/回报当前 yaw,便于 Mac 端调试
```

设备 → Mac 可回 `OK\n` / `ERR <msg>\n`。固件侧就是一个 `serialTick()`:攒行 → `sscanf` → 更新 `g_trackTarget`,完全不阻塞。

---

## 5. 跟踪控制策略

### 5.1 误差 → yaw 目标

无论检测在哪端,固件侧控制律相同(视觉伺服/外环给定都适用):

- **死区**:|err| < ~3–5% 画面宽度不动,抑制检测框抖动引起的舵机嗡嗡抖动;
- **P 控制 + 限速**:`yaw_target += Kp * err`,Kp 取小(如 err=半屏 → 转 20–30°),每次更新钳制最大步长;用 BSP 的 `Motion.move(yaw, pitch, pace)` 的 pace 参数限速度,不要用 PWM 旋转模式做跟踪;
- **低通**:对 err 做一阶滤波(α≈0.3)再进 P;
- **丢脸策略**:保持最后位置 3–5s → 缓慢回到 yaw=0 中位。

### 5.2 360° 连续旋转舵机做位置跟踪的注意事项

- BSP 里 yaw 是**位置模式 ±1280(=±128°)**(raw 0..1000,一步 0.3125°,见 `M5StackChan.cpp` 的 `mapped_angle` 注释);「360° 连续旋转」只是说机械结构能整圈转 + 有 PWM 速度模式,**位置闭环有效范围只有 ±128°**——足够覆盖桌面(±128° + 摄像头/屏幕约 ±30° 可视角 ≈ ±158°),不要按无限旋转设计;
- **禁止超过角度限幅**:BSP 已 clamp,但自己算目标时也要钳到 [-1200, 1200] 留余量;pitch 官方建议 5–85° 内使用(避免堵转损坏,见 [StackChan 文档 Motion Angle Notice](https://docs.m5stack.com/en/StackChan)),跟踪只动 yaw 即可,pitch 固定在现有 `PITCH_BASELINE`;
- 机械回差 + 检测抖动容易在死区边缘极限环振荡:死区 + 滤波 + 低 Kp 三件套必须一起上;
- 若将来真想全向跟踪(人绕到背后),位置模式不够,需「位置模式转到限位 → 切 PWM 模式旋转搜寻 → 发现脸切回位置模式」的状态机,复杂度高,本期不做。

---

## 6. 方案对比与推荐

| 维度 | A. 片上(CoreS3 摄像头 + HumanFaceDetect) | B. Mac 端(Vision/OpenCV + 串口) |
|---|---|---|
| 检测精度/帧率 | 0.3MP,~5–10 fps,弱光差 | 1080p,30–60 fps,Vision 带 landmarks |
| 与现有固件兼容 | **差**:SCCB 抢占 G11/G12 内部总线,冲突触摸/LED/IMU;检测阻塞 50–80ms 打乱 tick 节拍 | 好:只加一个 serialTick,不动总线不动节拍 |
| IDF 4.4 / Arduino 2.x | 可行(EloquentEsp32cam 恰好只支持 core 2.x),但属社区维护路径 | 不涉及 |
| 闭环质量 | 真闭环(摄像头在头上,自校准) | 开环映射,依赖 Mac 与机器人相对位置固定,需标定 |
| 依赖 | 无外部依赖 | Mac 必须在跑、程序存活 |
| 开发量 | 中(摄像头初始化 + 库集成 + 总线仲裁 + 节拍改造) | 小(Mac 端 ~100 行 + 固件 ~50 行) |
| 可靠性风险 | 总线仲裁属未验证领域;GC0308 弱光误检 | Mac 睡眠即失效(可降级) |

**推荐:B(Mac 端)。** 它直接命中需求场景(用户坐在 Mac 前工作,需要看见倒计时),避开 §1.3 的总线硬冲突,开发量最小。优雅降级:串口超时 → 机器人维持现状行为(中位 + 倒计时),功能无损。

**二期可选 A**:若未来要脱离 Mac 使用,再上 EloquentEsp32cam + 片上检测;届时需先解决总线仲裁(摄像头工作期间暂停 Si12T/LED 刷新,或验证 esp32-camera `sccb_i2c_port` 与 M5Unified 共口方案),并把检测放 FreeRTOS 任务避免阻塞 loop。

---

## 来源汇总

- CoreS3 规格/PinMap/I2C 地址表:https://docs.m5stack.com/en/core/CoreS3
- CoreS3 摄像头官方 Arduino 示例(含 `M5.In_I2C.release()`):https://docs.m5stack.com/en/arduino/m5cores3/camera
- StackChan 机身 PinMap(舵机 UART G6/G7、PY32 0x6F、Si12T 0x68、pitch 5–85° 警告):https://docs.m5stack.com/en/StackChan
- StackChan-BSP 源码(舵机 SCSCL/UART1、PY32IOExpander 走 In_I2C):https://github.com/m5stack/StackChan-BSP
- ESP-DL:https://github.com/espressif/esp-dl ;v1.1.0 README:https://components.espressif.com/components/espressif/esp-dl/versions/1.1.0/readme?language=en
- human_face_detect 组件(模型/延迟数据):https://components.espressif.com/components/espressif/human_face_detect
- ESP-WHO:https://github.com/espressif/esp-who
- EloquentEsp32cam 人脸检测(50/80ms、core 2.x 限定):https://eloquentarduino.com/posts/esp32-cam-face-detection 、https://eloquentarduino.com/esp32s3-camera-mastery-course-intro
- arduino-esp32 core 3.x 移除人脸检测:https://github.com/espressif/arduino-esp32/issues/10881
- stack-chan face_tracker MOD(UnitV2 + HTTP):https://hackaday.io/project/181344/log/197815-stack-chan-following-human-face 、https://raw.githubusercontent.com/meganetaaan/stack-chan/main/firmware/mods/face_tracker/mod.js
- stack-chan 仓库(MediaPipe BLE tracking):https://github.com/stack-chan/stack-chan
