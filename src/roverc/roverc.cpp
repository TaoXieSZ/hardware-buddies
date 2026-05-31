// RoverC Pro chassis driver — drop-in for bugc2.cpp on the Plus1 + RoverC
// Pro HAT pairing. Implements the `bugc2_*` API (see bugc2.h) so main.cpp
// is unchanged; the "bugc2" naming is the chassis-abstract interface and
// the implementation behind it is picked by build_src_filter per env.
//
// Plus2 + BugC2  → bugc2.cpp (Plus2 envs)
// Plus1 + RoverC → roverc.cpp (m5stickc-plus-*-rover envs)
//
// Both chassis sit at I2C 0x38 on SDA=G0/SCL=G26 but their register maps
// differ; one or the other is mutually exclusive per build.
//
// Register map source (verbatim, no guessing):
//   Repo:   https://github.com/m5stack/M5-RoverC
//   Files:  src/M5_RoverC.h, src/M5_RoverC.cpp
//   Wire bus: master `Wire` on G0/G26, default speed.
//
// RoverC Pro hardware: 4 mecanum wheels (per-wheel int8_t pulse at regs
// 0x00..0x03) + 2 PWM servos (gripper, angle reg 0x10/0x11 in 0..180).
// No RGB LEDs (BugC2 has 4) — LED state requests are silently ignored.
//
// Axis convention (from selftest 2026-05):
//   x+ = strafe right   x- = strafe left
//   y+ = forward        y- = backward
//   z+ = rotate CCW     z- = rotate CW
//
// Safety: all looped motions are zero-net-displacement (in-place rotation,
// strafe ping-pong, gripper). One-shot translations are short (≤200ms) so
// the rover doesn't walk off a desk.

#include "../bugc2.h"
#include <Arduino.h>
#include <Wire.h>

#define ROVERC_ADDR 0x38
#define REG_MOTOR0  0x00   // 4 bytes int8_t, one per wheel
#define REG_SERVO0  0x10   // 1 byte uint8_t angle 0..180
#define REG_SERVO1  0x11

static bool     g_present       = false;
static bugc2_motion_t g_motion  = BUGC2_OFF;
static uint32_t g_phase_start   = 0;
static bool     g_oneshot_done  = false;

// Manual override (calibration tool). Stops after 1500ms with no keepalive.
static bool     g_manual        = false;
static uint32_t g_manual_until  = 0;

// ---- low-level I2C writes ------------------------------------------------

static void wrReg(uint8_t reg, const uint8_t* buf, uint8_t len) {
  if (!g_present) return;
  Wire.beginTransmission(ROVERC_ADDR);
  Wire.write(reg);
  for (uint8_t i = 0; i < len; i++) Wire.write(buf[i]);
  Wire.endTransmission();
}

static void setSpeed(int8_t x, int8_t y, int8_t z) {
  // RoverC firmware does the mecanum mix; we just send the holonomic
  // velocity vector. From upstream M5_RoverC::setSpeed: mixer applied
  // host-side. We replicate the same mix verbatim so the chip sees the
  // same per-wheel pulses upstream's example would emit.
  int8_t b[4];
  if (z != 0) {
    x = int(x * (100 - abs(z)) / 100);
    y = int(y * (100 - abs(z)) / 100);
  }
  b[0] = max(-100, min(100, y + x - z));
  b[1] = max(-100, min(100, y - x + z));
  b[3] = max(-100, min(100, y + x + z));
  b[2] = max(-100, min(100, y - x - z));
  wrReg(REG_MOTOR0, (uint8_t*)b, 4);
}

static void setGripper(uint8_t a0, uint8_t a1) {
  // 0 = closed, 180 = wide open. Two servos move together for symmetric
  // gripper but the API allows independent angles for asymmetric stunts.
  wrReg(REG_SERVO0, &a0, 1);
  wrReg(REG_SERVO1, &a1, 1);
}

static inline void hardStop() {
  int8_t z[4] = {0,0,0,0};
  wrReg(REG_MOTOR0, (uint8_t*)z, 4);
}

// ---- bugc2.h API ---------------------------------------------------------

bool bugc2_begin() {
  Wire.begin(0, 26);
  delay(10);
  Wire.beginTransmission(ROVERC_ADDR);
  g_present = (Wire.endTransmission() == 0);
  if (g_present) {
    hardStop();
    setGripper(90, 90);   // neutral
  }
  return g_present;
}

bool bugc2_present() { return g_present; }

void bugc2_stop() {
  hardStop();
  setGripper(90, 90);
  g_motion = BUGC2_OFF;
  g_manual = false;
}

void bugc2_request(bugc2_motion_t m, uint32_t now_ms) {
  if (g_manual) return;
  if (m == g_motion) return;  // idempotent for looped motions
  g_motion = m;
  g_phase_start = now_ms;
  g_oneshot_done = false;
  // Edge effects at transition.
  switch (m) {
    case BUGC2_OFF:
    case BUGC2_IDLE_LIT:
      hardStop();
      setGripper(90, 90);
      break;
    case BUGC2_SLEEP:
      hardStop();
      setGripper(0, 0);
      break;
    default:
      // looped motions do their work in tick()
      break;
  }
}

void bugc2_tick(uint32_t now_ms) {
  if (!g_present || g_manual) return;
  const uint32_t t = now_ms - g_phase_start;

  switch (g_motion) {
    case BUGC2_GREET: {
      // 2 gripper open/close in 480ms total, then idle.
      uint32_t p = t % 240;
      if (t >= 480) {
        if (!g_oneshot_done) { setGripper(90, 90); g_oneshot_done = true; }
        return;
      }
      setGripper(p < 120 ? 180 : 0, p < 120 ? 180 : 0);
      break;
    }
    case BUGC2_ATTENTION: {
      // 1s cycle: 200ms fwd, 200ms back, 600ms idle. Looped.
      uint32_t p = t % 1000;
      if      (p < 200) setSpeed(0,  50, 0);
      else if (p < 400) setSpeed(0, -50, 0);
      else              hardStop();
      break;
    }
    case BUGC2_CELEBRATE: {
      // 2.4s phrase, 8 beats of 300ms: spin alternates direction every 4
      // beats, gripper "claps" open/close every beat. Gives the dance
      // an audible+visual rhythm instead of a featureless spin.
      uint32_t beat = (t / 300) % 8;
      // Beats 0-3 CCW, 4-7 CW. Vary speed across the phrase for swing.
      int8_t spin = (beat < 4 ? 90 : -90);
      if (beat == 3 || beat == 7) spin = (int8_t)(spin / 2);   // soft landing
      setSpeed(0, 0, spin);
      // Clap on the beat: open on even, closed on odd.
      uint8_t a = (beat & 1) ? 0 : 180;
      setGripper(a, a);
      break;
    }
    case BUGC2_DIZZY: {
      // 600ms cycle: alternating slow spin. Stop after 2.4s (one-shot).
      if (t >= 2400) { hardStop(); return; }
      uint32_t p = t % 600;
      setSpeed(0, 0, p < 300 ? 40 : -40);
      break;
    }
    case BUGC2_THINKING: {
      // 1.6s cycle: tiny strafe wobble. Looped.
      uint32_t p = t % 1600;
      if      (p < 120)  setSpeed( 30, 0, 0);
      else if (p < 800)  hardStop();
      else if (p < 920)  setSpeed(-30, 0, 0);
      else               hardStop();
      break;
    }
    case BUGC2_HEART: {
      // 1.2s cycle: slow gripper open/close, motors stopped.
      uint32_t p = t % 1200;
      uint8_t a = p < 600 ? 180 : 0;
      setGripper(a, a);
      break;
    }
    case BUGC2_OFF:
    case BUGC2_IDLE_LIT:
    case BUGC2_SLEEP:
    default:
      break;  // resting; edge already set
  }
}

// ---- manual override (calibration HTML tool) ----------------------------

void bugc2_manual_drive(int8_t s0, int8_t s1, int8_t s2, int8_t s3, uint32_t now_ms) {
  if (!g_present) return;
  g_manual = true;
  g_manual_until = now_ms + 1500;
  int8_t b[4] = {s0, s1, s2, s3};
  wrReg(REG_MOTOR0, (uint8_t*)b, 4);
}

void bugc2_manual_tick(uint32_t now_ms) {
  if (!g_manual) return;
  if ((int32_t)(now_ms - g_manual_until) >= 0) {
    hardStop();
    g_manual = false;
  }
}

bool bugc2_manual_active(uint32_t now_ms) {
  (void)now_ms;
  return g_manual;
}

// ---- diag: blocking 4-wheel sweep ---------------------------------------

void bugc2_motor_diag() {
  if (!g_present) return;
  for (uint8_t i = 0; i < 4; i++) {
    int8_t b[4] = {0, 0, 0, 0};
    b[i] = 50;
    wrReg(REG_MOTOR0, (uint8_t*)b, 4);
    delay(1000);
  }
  hardStop();
  delay(200);
}
