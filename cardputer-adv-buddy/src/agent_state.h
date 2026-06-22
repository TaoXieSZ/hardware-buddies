// 会话状态枚举：所有模块共享的输入语义。
#pragma once
#include <stdint.h>

enum class AgentState : uint8_t {
    Idle = 0,   // 空闲 / 等待用户
    Thinking,   // 模型推理中
    ToolUse,    // 正在执行工具
    Approval,     // 等待审批（manual 模式）
    Done,         // 本轮完成
    Notification, // 提示用户输入（Claude 等待输入）
    Count
};
