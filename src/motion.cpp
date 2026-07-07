// StackChan body driver — servos + 12 RGB LEDs.
// Hardware: M5StackChan (FEETECH SCSCL servos + PY32 IO-expander LEDs).
// Both animation machines are non-blocking (millis timers).
// Supports position moves AND rotation spins (rotateYaw for continuous
// rotation via the BSP's PWM/velocity mode).

#include "motion.h"
#include <M5StackChan.h>
#include <Arduino.h>

// ---- Servo patterns ---------------------------------------------------------

namespace {
constexpr int16_t PITCH_BASELINE = 600;   // 60 deg head-up

struct ServoStep {
    int16_t  x;         // yaw angle (position) or yaw velocity (rotation spin)
    int16_t  y;         // pitch delta from PITCH_BASELINE (ignored when spin)
    uint16_t pace;     // 0..1000 (for move() calls; ignored when spin)
    uint16_t dwell;   // ms to wait after issuing this step
    bool     spin;   // true → x is rotation velocity, use rotateYaw(x)
};
// Sentinel: dwell==0 && pace==0 && !spin → loop back to step 0.

// --- IDLE: wide breathing sweeps with a slow rotation flourish every few cycles
// The 360-degree yaw can sweep ±20 degrees comfortably; the step table does a
// full left-to-right "looking around" motion with an occasional slow rotation
// segment so the head explores the full range.
const ServoStep PAT_IDLE[] = {
    { -200,  30, 120, 3000, false },   // look left + slight up, ~3s
    {    0,  20, 120, 2500, false },   // center, up
    {  200,  30, 120, 3000, false },   // look right + slight up, ~3s
    {    0,  20, 120, 2500, false },   // center, up
    {  250,   0,   0, 2000, true  },   // rotate CW for 2s (velocity 250)
    {    0,   0,   0,  500, false },   // pause after spin
    {    0, -15, 100, 3000, false },   // center, slight down
    { 0, 0, 0, 0, false }             // loop (~20s total cycle)
};

// --- THINKING: dramatic head tilt using full yaw and pitch range ---
const ServoStep PAT_THINKING[] = {
    {  500, 180, 250, 3500, false },   // hard right + gaze way up (pitch 60+18=78deg)
    {    0, 200, 200, 1500, false },   // center, extremely up
    { -500, 180, 250, 3500, false },   // hard left + gaze way up
    {    0, 200, 200, 1500, false },   // center, extremely up
    { 0, 0, 0, 0, false }
};

// --- REPLYING: joyful spin dance + nod ---
const ServoStep PAT_REPLYING[] = {
    {  400,   0,   0, 1000, true  },   // spin CW at velocity 400 for 1s
    {    0,   0,   0,  200, false },   // brief pause (position-mode park at 0)
    { -400,   0,   0, 1000, true  },   // spin CCW at velocity 400 for 1s
    {    0,   0,   0,  200, false },   // brief pause
    {    0, 150, 300, 350, false },    // nod way up
    {    0, -20, 300, 350, false },    // nod down
    {    0, 150, 300, 350, false },
    {    0, -20, 300, 350, false },
    {    0,   0, 250, 600, false },    // settle at centre
    { 0, 0, 0, 0, false }
};

// --- ERROR: violent wide shake using the full speed and range ---
const ServoStep PAT_ERROR[] = {
    {  300,  30, 600, 180, false },    // throw head right + slight up
    { -300,  30, 600, 180, false },    // throw head left
    {  300,  30, 600, 180, false },
    { -300,  30, 600, 180, false },
    {  300,  30, 600, 180, false },
    { -300,  30, 600, 180, false },
    {    0,   0, 400, 800, false },    // settle
    { 0, 0, 0, 0, false }
};
const ServoStep* const PATTERNS[] = { PAT_IDLE, PAT_THINKING, PAT_REPLYING, PAT_ERROR };

// ---- Runtime globals --------------------------------------------------------

AgentState g_state        = (AgentState)0xFF;
uint32_t   g_motionEntryMs = 0;

uint32_t g_lastServoMs   = 0;
uint8_t  g_servoStep     = 0;

uint32_t g_lastLedMs     = 0;
uint8_t  g_ledPos        = 0;
bool     g_ledFlashOn    = false;

// ---- Helpers ----------------------------------------------------------------

int16_t clampedPitch(int16_t base, int16_t delta) {
    int y = (int)base + (int)delta;
    if (y < 0)   y = 0;
    if (y > 900) y = 900;
    return (int16_t)y;
}

// ---- Servo ticker -----------------------------------------------------------

void servoTick(AgentState st) {
    const ServoStep* pat = PATTERNS[(int)st];
    if (!pat) return;

    uint32_t now = millis();
    if (g_lastServoMs > 0 && now - g_lastServoMs < pat[g_servoStep].dwell) {
        return;
    }

    const ServoStep& s = pat[g_servoStep];
    if (s.dwell == 0 && s.pace == 0 && !s.spin) {
        g_servoStep  = 0;
        g_lastServoMs = 0;
        return;
    }

    if (s.spin) {
        M5StackChan.Motion.rotateYaw(s.x);
    } else {
        M5StackChan.Motion.move(s.x, clampedPitch(PITCH_BASELINE, s.y), s.pace);
    }

    g_lastServoMs = now;
    g_servoStep++;
}

// ---- LED ticker -------------------------------------------------------------

void ledTick(AgentState st) {
    uint32_t now = millis();

    switch (st) {
    case STATE_IDLE: {
        if (now - g_lastLedMs < 80) return;
        g_lastLedMs = now;
        float t = (float)((now - g_motionEntryMs) % 3000) / 3000.0f;
        float b = (sinf(t * 2.0f * PI) * 0.4f + 0.6f) * 40.0f;
        for (int i = 0; i < 12; i++)
            M5StackChan.setRgbColor(i, 0, 0, (uint8_t)b);
        M5StackChan.refreshRgb();
        break;
    }
    case STATE_THINKING: {
        if (now - g_lastLedMs < 80) return;
        g_lastLedMs = now;
        for (int i = 0; i < 12; i++) {
            int dist = (i - g_ledPos + 12) % 12;
            if (dist == 0)
                M5StackChan.setRgbColor(i, 80, 50, 0);
            else if (dist <= 2) {
                uint8_t f = (uint8_t)(20 / dist);
                M5StackChan.setRgbColor(i, f, (uint8_t)(f * 5 / 8), 0);
            } else
                M5StackChan.setRgbColor(i, 0, 0, 0);
        }
        M5StackChan.refreshRgb();
        g_ledPos = (g_ledPos + 1) % 12;
        break;
    }
    case STATE_REPLYING: {
        if (now - g_lastLedMs < 250) return;
        g_lastLedMs = now;
        g_ledFlashOn = !g_ledFlashOn;
        M5StackChan.showRgbColor(0, g_ledFlashOn ? 100 : 20, 0);
        break;
    }
    case STATE_ERROR: {
        if (now - g_lastLedMs < 300) return;
        g_lastLedMs = now;
        g_ledFlashOn = !g_ledFlashOn;
        M5StackChan.showRgbColor(g_ledFlashOn ? 100 : 0, 0, 0);
        break;
    }
    }
}

}  // namespace

// ---- Public API -------------------------------------------------------------

void motionInit() {
    M5StackChan.begin();
    M5StackChan.setServoPowerEnabled(true);
    M5StackChan.Motion.setAutoTorqueReleaseEnabled(true);
    M5StackChan.Motion.move(0, PITCH_BASELINE, 250);
    M5StackChan.showRgbColor(0, 0, 0);

    g_state         = STATE_IDLE;
    g_motionEntryMs = millis();
}

void motionSetState(AgentState next) {
    if (g_state == next) return;
    g_state         = next;
    g_motionEntryMs = millis();
    g_servoStep     = 0;
    g_lastServoMs   = 0;
    g_ledPos        = 0;
    g_lastLedMs     = 0;
    g_ledFlashOn    = false;
}

void motionTick(AgentState state) {
    if (state != g_state) g_state = state;

    M5StackChan.update();    // polls Si12T touch zones + pushes LED buffer + M5.update()
    servoTick(g_state);
    ledTick(g_state);
}
