// BugC2 chassis I2C driver — motion + RGB tied to buddy persona states.
//
// Register map source (gate-zero, copied verbatim, no guessing):
//   Repo:   https://github.com/m5stack/M5Hat-BugC
//   Commit: c054b6ed777eeb56b0880eeb830b91aec3ba8307
//   Files:  src/M5HatBugC.h, src/M5HatBugC.cpp
//   Note:   M5Hat-BugC is the official m5stack library; the header defines
//           BUGC2_IR_RX_PIN, indicating m5stack uses one library for both
//           BugC and BugC2 chassis (same STM32 firmware, same I2C protocol,
//           same address 0x38). Predecessor BugC and BugC2 share the wire
//           protocol — only the chassis form factor differs.
//
// Wire bus: uses Arduino `Wire` (I2C_NUM_0) on G0/G26 at 400 kHz — matches
// upstream `bugc.begin(&Wire, BUGC_DEFAULT_I2C_ADDR, 0, 26, 400000U)` in
// examples/bugc_robot_test/. Critically NOT `Wire1`: M5Unified's In_I2C
// (IMU/RTC/PMIC) sits on I2C_NUM_1 = Arduino `Wire1` on Plus2, so sharing
// Wire1 collides with M5.Imu and clobbers BugC2 writes (motor stop drops).
//
// Constraint: this module uses only <Arduino.h> + <Wire.h>. No M5StickCPlus
// or M5Unified dependencies, so it survives Plus ↔ Plus2 board swaps
// unchanged.
//
// Safety: all motions are pure rotation (all four motors same signed speed,
// matching MOVE_ROTATE in upstream). Net displacement ≈ 0 so the toy stays
// on the desk even without cliff detection.

#include "bugc2.h"
#include <Arduino.h>
#include <Wire.h>

// ===== Wire-protocol constants (verbatim from upstream) =====
static const uint8_t BUGC2_ADDR        = 0x38;
static const uint8_t REG_MOTOR_BASE    = 0x00;  // 4 bytes: motor 0..3, signed int8
static const uint8_t REG_RGB_LED       = 0x10;  // 4-byte payload: [index, R, G, B]

// ===== State =====
static bool             s_present = false;
static bugc2_motion_t   s_motion  = BUGC2_OFF;
static uint32_t         s_t0      = 0;       // when current motion started
static uint32_t         s_lastBeat = 0;      // last sub-beat time (for looped motions)
static uint8_t          s_phase   = 0;       // intra-motion sub-state
static int8_t           s_lastSpeed = 0;     // remember last motor cmd to avoid redundant I2C

// Manual-mode override (BLE calibration tool).
static uint32_t         s_manualUntil = 0;   // motors auto-stop when now > this
static bool             s_manualHadCmd = false;  // ever entered manual since boot?

// ===== Low-level I2C =====
static bool i2c_write_bytes(uint8_t reg, const uint8_t* data, size_t n) {
  Wire.beginTransmission(BUGC2_ADDR);
  Wire.write(reg);
  for (size_t i = 0; i < n; i++) Wire.write(data[i]);
  return Wire.endTransmission() == 0;
}

static int8_t clamp_speed(int8_t s) {
  if (s > 100) return 100;
  if (s < -100) return -100;
  return s;
}

// Burst write all 4 motor channels — matches upstream setAllMotorSpeed.
static void motors_spin(int8_t speed) {
  if (!s_present) return;
  if (speed == s_lastSpeed) return;
  s_lastSpeed = speed;
  speed = clamp_speed(speed);
  uint8_t buf[4] = {(uint8_t)speed, (uint8_t)speed, (uint8_t)speed, (uint8_t)speed};
  i2c_write_bytes(REG_MOTOR_BASE, buf, 4);
}

// Translation primitive — confirmed via HTML calibration tool with full
// battery: upstream's MOVE_FORWARD pattern (s,-s,s,-s) walks the bot
// straight. Earlier failures were due to (a) battery too low to overcome
// translation friction, and (b) cmd routing bug in data.h (xferCommand
// catch-all). Both fixed.
// Net displacement ≠ 0, buddy walks. Desk-edge risk accepted.
static void motors_translate(int8_t speed) {
  if (!s_present) return;
  s_lastSpeed = 127;  // sentinel: invalidate spin dedup so next motors_spin re-writes
  speed = clamp_speed(speed);
  int8_t a = speed, b = -speed;
  uint8_t buf[4] = {(uint8_t)a, (uint8_t)b, (uint8_t)a, (uint8_t)b};
  i2c_write_bytes(REG_MOTOR_BASE, buf, 4);
}

// Asymmetric forward translation with separate right / left magnitudes
// (positive for forward direction; sign passed as `dir` is +1 or -1).
// Pattern on this hardware: (R, -L, R, -L) for forward.
static void motors_translate_asym(int8_t r, int8_t l, int8_t dir) {
  if (!s_present) return;
  s_lastSpeed = 127;
  int8_t rs = clamp_speed(dir > 0 ?  r : -r);
  int8_t ls = clamp_speed(dir > 0 ? -l :  l);
  uint8_t buf[4] = {(uint8_t)rs, (uint8_t)ls, (uint8_t)rs, (uint8_t)ls};
  i2c_write_bytes(REG_MOTOR_BASE, buf, 4);
}

static void motors_off_force() {
  s_lastSpeed = 0;
  if (!s_present) return;
  uint8_t z[4] = {0, 0, 0, 0};
  i2c_write_bytes(REG_MOTOR_BASE, z, 4);
}

static void led_set(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
  if (!s_present) return;
  uint8_t buf[4] = {idx, r, g, b};
  i2c_write_bytes(REG_RGB_LED, buf, 4);
}

static void leds_both(uint8_t r, uint8_t g, uint8_t b) {
  led_set(0, r, g, b);
  delay(10);  // upstream setAllLedColor inserts delay(10) between LEDs
  led_set(1, r, g, b);
}

// ===== Color presets (RGB 8/8/8, conservative brightness so the LEDs don't
// drown out the M5 stick screen at night). =====
static const uint8_t LIT_R = 0x00, LIT_G = 0x10, LIT_B = 0x10;  // dim cyan
static const uint8_t HI_R  = 0x00, HI_G  = 0x80, HI_B  = 0x80;  // greet cyan
static const uint8_t ATT_R = 0x80, ATT_G = 0x10, ATT_B = 0x00;  // amber/red attention
static const uint8_t CEL_R = 0x00, CEL_G = 0x80, CEL_B = 0x10;  // celebration green
static const uint8_t DIZ_R = 0x80, DIZ_G = 0x80, DIZ_B = 0x00;  // dizzy yellow

// ===== Motion timings =====
static const uint32_t GREET_T1 = 80;
static const uint32_t GREET_T2 = 160;
static const uint32_t GREET_T3 = 240;
static const int8_t   GREET_SPD     = 22;
static const int8_t   GREET_SPD_LO  = 15;

static const uint32_t ATT_PERIOD = 1200;
static const uint32_t ATT_BURST  = 150;  // long enough to see, short enough to feel like a twitch
static const int8_t   ATT_SPD    = 40;   // above the BugC2 DC-motor stall threshold (~25)

static const int8_t   CELEB_SPD  = 28;    // gentle continuous spin

static const uint32_t DIZ_PERIOD = 140;   // direction flips every 140ms (was 60)
static const int8_t   DIZ_SPD    = 25;

// THINKING — calm cool blue LED + brief in-place spin (no translation).
// One burst on BUSY entry: spin ~1.2s then stop. Sound (3-chirp) is
// triggered from main.cpp on state transition.
static const uint8_t  THK_R = 0x00, THK_G = 0x10, THK_B = 0x80;
static const uint32_t THK_PERIOD = 2048;  // LED breathe full cycle (bit-masked)
static const int8_t   THK_SPIN_SPD   = 30;  // in-place rotation speed
static const uint32_t THK_SPIN_DUR   = 1200; // ~1.2s of spin
static const uint8_t  THK_PHASE_DONE = 1;

// HEART — pink double-pulse (thump-thump) every ~1200ms; subtle wiggle every ~3s.
static const uint8_t  HRT_R = 0x80, HRT_G = 0x00, HRT_B = 0x30;
static const uint32_t HRT_BEAT_PERIOD   = 1200;
static const uint32_t HRT_THUMP1_END    = 100;   // first thump bright
static const uint32_t HRT_GAP_END       = 180;   // gap between thumps
static const uint32_t HRT_THUMP2_END    = 260;   // second thump bright
static const uint32_t HRT_WIGGLE_PERIOD = 3000;
static const uint32_t HRT_WIGGLE_BURST  = 80;
static const int8_t   HRT_WIGGLE_SPD    = 18;

bool bugc2_begin() {
  // Pin/freq/bus match upstream M5Hat-BugC example exactly:
  //   bugc.begin(&Wire, 0x38, /*sda=*/0, /*scl=*/26, 400000U)
  Wire.end();
  if (!Wire.begin(0, 26, 400000)) {
    Serial.println("[bugc2] Wire.begin(0,26,400k) failed");
    s_present = false;
    return false;
  }
  Wire.beginTransmission(BUGC2_ADDR);
  bool ack = (Wire.endTransmission() == 0);
  Serial.printf("[bugc2] probe 0x38 on Wire(SDA=0,SCL=26,400k) → %s\n",
                ack ? "ACK" : "NACK");
  if (!ack) {
    s_present = false;
    return false;
  }
  s_present = true;
  motors_off_force();
  leds_both(0, 0, 0);
  return true;
}

bool bugc2_present() { return s_present; }

bool bugc2_manual_active(uint32_t now_ms) {
  return s_manualHadCmd && (int32_t)(s_manualUntil - now_ms) > 0;
}

void bugc2_manual_drive(int8_t s0, int8_t s1, int8_t s2, int8_t s3, uint32_t now_ms) {
  if (!s_present) return;
  s_manualHadCmd = true;
  s_manualUntil = now_ms + 1500;  // keepalive window
  s_lastSpeed   = 127;             // invalidate dedup
  uint8_t buf[4] = {
    (uint8_t)clamp_speed(s0), (uint8_t)clamp_speed(s1),
    (uint8_t)clamp_speed(s2), (uint8_t)clamp_speed(s3),
  };
  i2c_write_bytes(REG_MOTOR_BASE, buf, 4);
  Serial.printf("[bugc2.manual] %d,%d,%d,%d\n", s0, s1, s2, s3);
}

void bugc2_manual_tick(uint32_t now_ms) {
  if (!s_manualHadCmd) return;
  // Just expired? force stop and resume normal.
  if ((int32_t)(s_manualUntil - now_ms) <= 0 && s_manualUntil != 0) {
    s_manualUntil = 0;
    motors_off_force();
    leds_both(0, 0, 0);
    Serial.println("[bugc2.manual] keepalive expired → stop");
    // Force the persona path to refresh on next request: pretend we were OFF.
    s_motion = BUGC2_OFF;
  }
}

void bugc2_motor_diag() {
  if (!s_present) {
    Serial.println("[bugc2.diag] not present, skipping");
    return;
  }
  Serial.println("[bugc2.diag] === MOTOR CHANNEL DIAG ===");
  Serial.println("[bugc2.diag] watch each wheel; report which physical wheel moves which way per ch");

  static const struct { uint8_t r, g, b; const char* name; } cues[4] = {
    {0xA0, 0x00, 0x00, "ch0 RED"},
    {0xA0, 0x80, 0x00, "ch1 YELLOW"},
    {0x00, 0xA0, 0x00, "ch2 GREEN"},
    {0x00, 0x00, 0xA0, "ch3 BLUE"},
  };

  // Make sure everything is at rest first.
  uint8_t z[4] = {0,0,0,0};
  i2c_write_bytes(REG_MOTOR_BASE, z, 4);
  delay(300);

  for (uint8_t ch = 0; ch < 4; ch++) {
    Serial.printf("[bugc2.diag] %s: writing +50 to ch %u\n", cues[ch].name, ch);
    leds_both(cues[ch].r, cues[ch].g, cues[ch].b);
    Wire.beginTransmission(BUGC2_ADDR);
    Wire.write(ch);
    Wire.write((uint8_t)50);
    Wire.endTransmission();
    delay(1000);
    Wire.beginTransmission(BUGC2_ADDR);
    Wire.write(ch);
    Wire.write((uint8_t)0);
    Wire.endTransmission();
    leds_both(0, 0, 0);
    delay(400);
  }

  // ---- Find which sign combo walks STRAIGHT ----
  // Each pattern runs 1.5s. Watch for clean translation (any direction).
  // Skip rotations and wiggles — we want a clear straight line.
  static const struct { int8_t s[4]; uint8_t r,g,b; const char* tag; } tests[] = {
    {{ 0,  0, 50, 50}, 0xC0,0x00,0x00, "T1 ch2+ ch3+ (RED)"},
    {{ 0,  0, 50,-50}, 0xC0,0x80,0x00, "T2 ch2+ ch3- (ORANGE)"},
    {{ 0,  0,-50, 50}, 0xC0,0xC0,0x00, "T3 ch2- ch3+ (YELLOW)"},
    {{50, 50,  0,  0}, 0x00,0xC0,0x00, "T4 ch0+ ch1+ (GREEN)"},
    {{50,-50,  0,  0}, 0x00,0xC0,0xC0, "T5 ch0+ ch1- (CYAN)"},
    {{-50,50,  0,  0}, 0x00,0x00,0xC0, "T6 ch0- ch1+ (BLUE)"},
    {{50,-50,-50, 50}, 0xC0,0x00,0xC0, "T7 (s,-s,-s,s) MAGENTA"},
    {{50, 50,-50,-50}, 0xFF,0xFF,0xFF, "T8 (s,s,-s,-s) WHITE"},
  };
  for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
    Serial.printf("[bugc2.diag] %s\n", tests[i].tag);
    leds_both(tests[i].r, tests[i].g, tests[i].b);
    uint8_t buf[4] = {(uint8_t)tests[i].s[0], (uint8_t)tests[i].s[1],
                      (uint8_t)tests[i].s[2], (uint8_t)tests[i].s[3]};
    i2c_write_bytes(REG_MOTOR_BASE, buf, 4);
    delay(1500);
    uint8_t z[4] = {0,0,0,0};
    i2c_write_bytes(REG_MOTOR_BASE, z, 4);
    leds_both(0, 0, 0);
    delay(800);
  }
  Serial.println("[bugc2.diag] === DONE ===");
}

void bugc2_request(bugc2_motion_t m, uint32_t now_ms) {
  if (!s_present) {
    s_motion = m;  // remember intent so a later attach could start fresh
    return;
  }
  // Manual override owns the chassis until keepalive expires.
  if (bugc2_manual_active(now_ms)) return;
  // Re-requesting the same looped motion mid-flight is a no-op — preserves
  // phase so the rhythm doesn't reset every frame.
  // DIZZY included so re-requests during the 5s shake cooldown don't reset
  // s_t0 — otherwise the 600ms hard-cap in bugc2_tick never fires and the
  // motor never auto-stops.
  if (m == s_motion &&
      (m == BUGC2_IDLE_LIT || m == BUGC2_OFF || m == BUGC2_ATTENTION ||
       m == BUGC2_CELEBRATE || m == BUGC2_DIZZY ||
       m == BUGC2_SLEEP || m == BUGC2_THINKING || m == BUGC2_HEART)) {
    return;
  }
  s_motion = m;
  s_t0 = now_ms;
  s_lastBeat = now_ms;
  s_phase = 0;

  switch (m) {
    case BUGC2_OFF:
      motors_off_force();
      leds_both(0, 0, 0);
      break;
    case BUGC2_IDLE_LIT:
      motors_off_force();
      leds_both(LIT_R, LIT_G, LIT_B);
      break;
    case BUGC2_GREET:
      motors_spin(GREET_SPD);
      leds_both(HI_R, HI_G, HI_B);
      break;
    case BUGC2_ATTENTION:
      motors_spin(ATT_SPD);
      leds_both(ATT_R, ATT_G, ATT_B);
      break;
    case BUGC2_CELEBRATE:
      motors_spin(CELEB_SPD);
      leds_both(CEL_R, CEL_G, CEL_B);
      break;
    case BUGC2_DIZZY:
      motors_spin(DIZ_SPD);
      leds_both(DIZ_R, DIZ_G, DIZ_B);
      break;
    case BUGC2_SLEEP:
      motors_off_force();
      leds_both(0, 0, 0);
      break;
    case BUGC2_THINKING:
      // Start in-place spin; tick stops after THK_SPIN_DUR.
      motors_spin(THK_SPIN_SPD);
      leds_both(THK_R / 2, THK_G / 2, THK_B / 2);
      break;
    case BUGC2_HEART:
      motors_off_force();
      leds_both(HRT_R, HRT_G, HRT_B);  // first thump on entry
      break;
  }
}

void bugc2_stop() {
  s_motion = BUGC2_OFF;
  motors_off_force();
  if (s_present) leds_both(0, 0, 0);
}

void bugc2_tick(uint32_t now_ms) {
  if (!s_present) return;
  if (bugc2_manual_active(now_ms)) return;  // manual mode owns the chassis
  uint32_t dt = now_ms - s_t0;

  switch (s_motion) {
    case BUGC2_OFF:
    case BUGC2_IDLE_LIT:
      // Static states — but re-assert motors=0 every ~500ms in case the
      // BugC2 STM32 firmware retained a stale command from a prior burst.
      if (now_ms - s_lastBeat >= 500) {
        s_lastBeat = now_ms;
        motors_off_force();
      }
      break;

    case BUGC2_GREET:
      // Phased nod: CW → CCW → small CW → stop → drop into IDLE_LIT.
      if (s_phase == 0 && dt >= GREET_T1) {
        motors_spin(-GREET_SPD); s_phase = 1;
      } else if (s_phase == 1 && dt >= GREET_T2) {
        motors_spin(GREET_SPD_LO); s_phase = 2;
      } else if (s_phase == 2 && dt >= GREET_T3) {
        motors_off_force();
        leds_both(LIT_R, LIT_G, LIT_B);
        s_motion = BUGC2_IDLE_LIT;
        s_phase = 0;
      }
      break;

    case BUGC2_ATTENTION: {
      // Looped 80ms burst every 800ms; LEDs slow-pulse amber.
      uint32_t since = now_ms - s_lastBeat;
      if (s_phase == 0 && since >= ATT_BURST) {
        // burst over → motors off, wait
        motors_off_force();
        s_phase = 1;
      } else if (s_phase == 1 && since >= ATT_PERIOD) {
        // start next burst, alternate direction so net rotation stays ~0
        s_lastBeat = now_ms;
        motors_spin((dt / ATT_PERIOD) & 1 ? -ATT_SPD : ATT_SPD);
        s_phase = 0;
      }
      // Breathing pulse ~2s cycle, baseline floor so always visible.
      uint32_t pulse = (now_ms / 8) & 0xFF;                // 2048ms full cycle
      uint8_t tri = pulse < 128 ? pulse : (255 - pulse);   // 0..127..0
      uint16_t br = 48 + (uint16_t)tri;                    // 48..175 — never dark
      leds_both((uint8_t)((ATT_R * br) >> 8),
                (uint8_t)((ATT_G * br) >> 8),
                (uint8_t)((ATT_B * br) >> 8));
      break;
    }

    case BUGC2_CELEBRATE:
      // Continuous spin; main.cpp owns when to revert (oneShotUntil expiry
      // → request IDLE_LIT). LEDs solid green; no per-tick I2C needed for
      // motor since we already drove it on entry, but reaffirm every 200ms
      // in case of bus glitch.
      if (now_ms - s_lastBeat >= 200) {
        s_lastBeat = now_ms;
        motors_spin(CELEB_SPD);
      }
      break;

    case BUGC2_DIZZY:
      // Hard cap: dizzy can never last more than 600ms regardless of what
      // the persona-state mapping requests. The vibration from dizzy spin
      // re-triggers IMU shake detection on the stick, which triggers a new
      // dizzy oneShot — without this cap that becomes a self-sustaining
      // loop. Motion intentionally short so vibration decays before main.cpp
      // can re-fire shake.
      if (dt >= 600) {
        motors_off_force();
        leds_both(LIT_R, LIT_G, LIT_B);
        s_motion = BUGC2_IDLE_LIT;
        s_phase = 0;
        s_lastBeat = now_ms;
        break;
      }
      if (now_ms - s_lastBeat >= DIZ_PERIOD) {
        s_lastBeat = now_ms;
        s_phase ^= 1;
        motors_spin(s_phase ? DIZ_SPD : -DIZ_SPD);
      }
      if (((now_ms / 100) & 1) != (s_phase & 1)) {
        leds_both(DIZ_R, DIZ_G, DIZ_B);
      } else {
        leds_both(DIZ_R >> 2, DIZ_G >> 2, 0);
      }
      break;

    case BUGC2_SLEEP:
      // Static: nothing to update. Re-assert motors=0 every 500ms in case
      // the chassis firmware retained a stale value.
      if (now_ms - s_lastBeat >= 500) {
        s_lastBeat = now_ms;
        motors_off_force();
      }
      break;

    case BUGC2_THINKING: {
      // Slow blue breathe — never goes fully dark, baseline floor 32/255.
      uint32_t pulse = now_ms & (THK_PERIOD - 1);   // 0..2047
      uint8_t  tri   = pulse < (THK_PERIOD / 2)
                       ? (pulse * 255) / (THK_PERIOD / 2)
                       : ((THK_PERIOD - pulse) * 255) / (THK_PERIOD / 2);
      uint16_t br    = 32 + ((uint16_t)tri * 3 / 4);  // 32..223
      leds_both((uint8_t)((THK_R * br) >> 8),
                (uint8_t)((THK_G * br) >> 8),
                (uint8_t)((THK_B * br) >> 8));

      // In-place spin for THK_SPIN_DUR, then stop. Stays silent after.
      uint32_t since = now_ms - s_lastBeat;
      switch (s_phase) {
        case 0:  // spinning
          if (since >= THK_SPIN_DUR) {
            motors_off_force();
            s_lastBeat = now_ms;
            s_phase = THK_PHASE_DONE;
          }
          break;
        case THK_PHASE_DONE:
          // Burst complete; re-assert motors=0 every 500ms.
          if (since >= 500) {
            s_lastBeat = now_ms;
            motors_off_force();
          }
          break;
      }
      break;
    }

    case BUGC2_HEART: {
      // LED double-pulse heartbeat (thump … thump … rest).
      uint32_t beat = (now_ms - s_t0) % HRT_BEAT_PERIOD;
      uint8_t r, g, b;
      if (beat < HRT_THUMP1_END) {
        r = HRT_R; g = HRT_G; b = HRT_B;
      } else if (beat < HRT_GAP_END) {
        r = HRT_R >> 2; g = HRT_G >> 2; b = HRT_B >> 2;
      } else if (beat < HRT_THUMP2_END) {
        r = HRT_R; g = HRT_G; b = HRT_B;
      } else {
        r = HRT_R >> 3; g = HRT_G >> 3; b = HRT_B >> 3;  // very dim baseline
      }
      leds_both(r, g, b);

      // Wiggle: every HRT_WIGGLE_PERIOD, do a short alternating twitch.
      uint32_t since_wiggle = now_ms - s_lastBeat;
      if (s_phase == 0 && since_wiggle >= HRT_WIGGLE_PERIOD) {
        s_lastBeat = now_ms;
        // alternate direction by counting wiggles via dt/period parity
        bool dir = (((now_ms - s_t0) / HRT_WIGGLE_PERIOD) & 1) != 0;
        motors_spin(dir ? HRT_WIGGLE_SPD : -HRT_WIGGLE_SPD);
        s_phase = 1;
      } else if (s_phase == 1 && since_wiggle >= HRT_WIGGLE_BURST) {
        motors_off_force();
        s_phase = 0;
        s_lastBeat = now_ms;  // next wiggle ~HRT_WIGGLE_PERIOD from here
      }
      break;
    }
  }
}
