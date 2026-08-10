#pragma once
#include <stdint.h>

enum AgentState : uint8_t {
    STATE_IDLE     = 0,
    STATE_THINKING = 1,
    STATE_REPLYING = 2,
    STATE_ERROR    = 3,
    STATE_REMINDER = 4,   // must stay last: PATTERNS[]/STATE_FILES[] index by value
};

void motionInit();
void motionTick(AgentState state);
void motionSetState(AgentState state);
