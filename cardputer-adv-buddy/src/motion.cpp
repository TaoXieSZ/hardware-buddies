// IMU 读取逐字核对自 upstream（不凭记忆）:
//   M5.Imu.getType() / M5.Imu.update() / M5.Imu.getImuData().accel.x/y/z
//     ← m5stack/M5Unified examples/Basic/Imu/Imu.ino
//   M5Cardputer.begin() 已初始化内部 IMU；此处不重复 begin。
#include "motion.h"
#include "M5Cardputer.h"
#include <math.h>

namespace {
constexpr float MOVE_THRESH = 0.08f;            // g：超过算「在动」
constexpr float SHAKE_THRESH = 1.2f;            // g：单帧剧变算「晃动」（调高：需明显甩动才触发，避免轻碰/挪动误触晕）
constexpr uint32_t STILL_FOR_PICKUP = 1500;     // 静止≥此值后再动 = 拿起
}  // namespace

void Motion::begin() {
    imuOk_ = (M5.Imu.getType() != m5::imu_none);
    Serial.printf("[motion] imu type=%d ok=%d (bmi270 expected on ADV)\n",
                  static_cast<int>(M5.Imu.getType()), imuOk_ ? 1 : 0);
}

void Motion::update(uint32_t dtMs) {
    if (!imuOk_) return;
    if (!M5.Imu.update()) {  // 无新数据：累计静止
        stillMs_ += dtMs;
        return;
    }
    auto d = M5.Imu.getImuData();
    float mag = sqrtf(d.accel.x * d.accel.x +
                      d.accel.y * d.accel.y +
                      d.accel.z * d.accel.z);
    float delta = fabsf(mag - lastMag_);
    lastMag_ = mag;
    activity_ = activity_ * 0.7f + delta * 0.3f;  // 低通平滑

    if (delta > SHAKE_THRESH) {
        pending_ = MotionEvent::Shaken;
        stillMs_ = 0;
    } else if (activity_ > MOVE_THRESH) {
        if (stillMs_ > STILL_FOR_PICKUP) {        // 从静止/睡眠被拿起
            pending_ = MotionEvent::PickedUp;
        }
        stillMs_ = 0;
    } else {
        stillMs_ += dtMs;
    }
}

MotionEvent Motion::event() {
    MotionEvent e = pending_;
    pending_ = MotionEvent::None;
    return e;
}
