#pragma once
#include <stdint.h>

enum AgentState : uint8_t {
    STATE_IDLE     = 0,
    STATE_THINKING = 1,
    STATE_REPLYING = 2,
    STATE_ERROR    = 3,
    STATE_REMINDER = 4,
    STATE_SLEEP    = 5,   // must stay last: PATTERNS[]/STATE_FILES[] index by value
};

void motionInit();
void motionTick(AgentState state);
void motionSetState(AgentState state);

// Face-tracking override: while active (and in STATE_IDLE), servoTick ignores
// the pattern table and drives yaw toward the target set by motionTrackTarget().
void motionSetTracking(bool on);
void motionTrackTarget(int16_t yaw, int16_t pitch);
