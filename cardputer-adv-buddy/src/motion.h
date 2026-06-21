// BMI270 体感手势识别（经 M5Unified 的 M5.Imu，不裸写寄存器）。
// 产出离散一次性事件 + 静止时长（供 main 判睡眠）。
#pragma once
#include <stdint.h>

enum class MotionEvent : uint8_t { None, PickedUp, Shaken };

class Motion {
public:
    void begin();                 // 探测 IMU 类型（自检日志用）
    void update(uint32_t dtMs);   // 每帧采样并判定
    MotionEvent event();          // 取走一次性事件（取走即清）
    uint32_t stillMs() const { return stillMs_; }
    bool imuOk() const { return imuOk_; }

private:
    bool imuOk_ = false;
    float lastMag_ = 1.0f;        // 上次加速度幅值（g）
    float activity_ = 0.0f;       // 低通平滑后的运动量
    uint32_t stillMs_ = 0;        // 连续静止时长
    MotionEvent pending_ = MotionEvent::None;
};
