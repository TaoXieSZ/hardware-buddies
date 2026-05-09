#pragma once
#include <stdint.h>

// BugC2 chassis driver. Stick sits on top of BugC2; BugC2 listens on I2C 0x38.
// See bugc2.cpp for upstream register-map source + citation.
//
// Motion catalog mirrors the buddy's PersonaState. All motions are
// rotational (zero net displacement) so the toy never walks off a desk.

enum bugc2_motion_t : uint8_t {
  BUGC2_OFF = 0,        // motors=0, LEDs=0
  BUGC2_IDLE_LIT,       // motors=0, LEDs dim cyan (default while connected, no event)
  BUGC2_GREET,          // ~250ms nod on BLE connect
  BUGC2_ATTENTION,      // looped 80ms twitch every ~800ms while a prompt is waiting
  BUGC2_CELEBRATE,      // continuous spin while activeState is celebrate
  BUGC2_DIZZY,          // quick alternating spin while dizzy one-shot
  BUGC2_SLEEP,          // motors=0, LEDs fully off — distinct from IDLE_LIT's dim cyan
  BUGC2_THINKING,       // motors=0, LEDs slow breathe blue — Claude is processing
  BUGC2_HEART,          // pink heartbeat LEDs (thump-thump) + occasional gentle wiggle
};

bool bugc2_begin();
void bugc2_tick(uint32_t now_ms);
// Diagnostic: spin each motor channel solo for 1s with a coloured LED cue.
// Red=ch0, Yellow=ch1, Green=ch2, Blue=ch3. Blocks ~6s. Call once from setup().
void bugc2_motor_diag();

// Manual override (BLE calibration tool). Writes raw 4-channel speeds and
// puts the driver into "manual mode" for 1500ms — bugc2_tick / bugc2_request
// become no-ops in that window so the persona-state mapping doesn't fight.
// HTML tool sends new cmds @ ~10Hz to stay live; missing keepalive → motors
// auto-stop on next bugc2_manual_tick.
void bugc2_manual_drive(int8_t s0, int8_t s1, int8_t s2, int8_t s3, uint32_t now_ms);
void bugc2_manual_tick(uint32_t now_ms);
bool bugc2_manual_active(uint32_t now_ms);
// Request a motion. Idempotent: requesting the same motion mid-flight is a
// no-op for looped motions; for one-shots it restarts.
void bugc2_request(bugc2_motion_t m, uint32_t now_ms);
// Synchronous hard-stop for shutdown / reset paths.
void bugc2_stop();
bool bugc2_present();
