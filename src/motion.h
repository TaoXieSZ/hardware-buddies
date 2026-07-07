#pragma once
#include <stdint.h>

enum AgentState : uint8_t {
    STATE_IDLE     = 0,
    STATE_THINKING = 1,
    STATE_REPLYING = 2,
    STATE_ERROR    = 3,
};

void motionInit();
void motionTick(AgentState state);
void motionSetState(AgentState state);
