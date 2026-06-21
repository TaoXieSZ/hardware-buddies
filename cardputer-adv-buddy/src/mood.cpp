#include "mood.h"

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void Mood::onStateEnter(AgentState s) {
    // 完成任务 → 心情提升。
    if (s == AgentState::Done) {
        happy_ = clampf(happy_ + 15.0f, 0.0f, 100.0f);
    }
}

void Mood::tick(AgentState current, uint32_t dtMs) {
    const float dt = dtMs / 1000.0f;
    switch (current) {
        case AgentState::Idle:
            // 空闲越久越无聊，心情缓慢下降。
            happy_ = clampf(happy_ - 1.5f * dt, 0.0f, 100.0f);
            break;
        case AgentState::Approval:
            // 久等审批 → 焦虑上升。
            anxiety_ = clampf(anxiety_ + 0.25f * dt, 0.0f, 1.0f);
            break;
        default:
            break;
    }
    // 离开审批后焦虑回落。
    if (current != AgentState::Approval) {
        anxiety_ = clampf(anxiety_ - 0.4f * dt, 0.0f, 1.0f);
    }
}
