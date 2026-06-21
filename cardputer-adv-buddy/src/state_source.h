// 「当前会话状态」抽象接口 + Phase 1 的键盘自测实现。
// BLE 接好后只需新增一个 BleStateSource 实现，main 的消费逻辑不变。
#pragma once
#include "agent_state.h"
#include "M5Cardputer.h"

class StateSource {
public:
    virtual ~StateSource() {}
    virtual void update() = 0;            // 每帧轮询输入
    virtual AgentState state() const = 0; // 当前状态
    virtual bool consumeChanged() = 0;    // 自上次以来是否变化（取走即清）
};

// 键盘自测源：任意可打印键 → 推进到下一个状态。
// 读取写法照搬 M5Cardputer inputText.ino：isChange()->isPressed()->keysState().word。
class KeyboardStateSource : public StateSource {
public:
    void update() override {
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto ks = M5Cardputer.Keyboard.keysState();
            if (!ks.word.empty()) {
                s_ = static_cast<AgentState>(
                    (static_cast<uint8_t>(s_) + 1) %
                    static_cast<uint8_t>(AgentState::Count));
                changed_ = true;
            }
        }
    }
    AgentState state() const override { return s_; }
    bool consumeChanged() override {
        bool c = changed_;
        changed_ = false;
        return c;
    }

private:
    AgentState s_ = AgentState::Idle;
    bool changed_ = true;  // 开机先渲染一次
};
