// 轻量内存心情模型（tamagotchi 式）。不跨重启持久化。
// happy: 0..100（完成提升、空闲缓降）；anxiety: 0..1（久等审批上升）。
#pragma once
#include "agent_state.h"
#include <stdint.h>

class Mood {
public:
    void onStateEnter(AgentState s);              // 事件式：进入某状态的一次性影响
    void tick(AgentState current, uint32_t dtMs); // 时间式：随时间演化

    float happy01() const { return happy_ / 100.0f; }
    float anxiety01() const { return anxiety_; }

private:
    float happy_ = 60.0f;
    float anxiety_ = 0.0f;
};
