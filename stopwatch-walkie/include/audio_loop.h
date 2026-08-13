#pragma once

#include <cstdint>
#include <string>

namespace stopwatch {

enum class DeviceState {
    Connecting,
    Ready,
    Recording,
    Transcribing,
    Result,
    Error,
    ProposalReview,
    Dispatching,
    Running,
    WaitingPermission,
    Completed,
    Cancelled,
    Failed,
};

enum class LoopAction {
    None,
    StartUtterance,
    EndUtterance,
    CancelUtterance,
    DropPartial,
    ApproveProposal,
    RejectProposal,
    ApprovePermission,
    DenyPermission,
    Acknowledge,
};

inline const char* stateName(DeviceState state)
{
    switch (state) {
        case DeviceState::Connecting:
            return "connecting";
        case DeviceState::Ready:
            return "ready";
        case DeviceState::Recording:
            return "recording";
        case DeviceState::Transcribing:
            return "transcribing";
        case DeviceState::Result:
            return "result";
        case DeviceState::Error:
            return "error";
        case DeviceState::ProposalReview:
            return "proposal_review";
        case DeviceState::Dispatching:
            return "dispatching";
        case DeviceState::Running:
            return "running";
        case DeviceState::WaitingPermission:
            return "waiting_permission";
        case DeviceState::Completed:
            return "completed";
        case DeviceState::Cancelled:
            return "cancelled";
        case DeviceState::Failed:
            return "failed";
    }
    return "unknown";
}

class AudioLoop {
public:
    DeviceState state() const { return state_; }
    bool connected() const { return connected_; }
    bool active() const { return !active_id_.empty(); }
    const std::string& activeId() const { return active_id_; }
    const std::string& resultText() const { return result_text_; }
    const std::string& errorCode() const { return error_code_; }
    bool retryableError() const { return retryable_error_; }
    const std::string& commandId() const { return command_id_; }
    const std::string& taskId() const { return task_id_; }
    const std::string& permissionId() const { return permission_id_; }
    bool permissionActionable() const { return permission_actionable_; }

    LoopAction onConnected()
    {
        connected_ = true;
        active_id_.clear();
        result_text_.clear();
        error_code_.clear();
        retryable_error_ = false;
        state_ = task_id_.empty() ? DeviceState::Ready : DeviceState::Running;
        return LoopAction::None;
    }

    LoopAction onDisconnected()
    {
        const bool had_active = active();
        connected_ = false;
        active_id_.clear();
        state_ = DeviceState::Connecting;
        return had_active ? LoopAction::DropPartial : LoopAction::None;
    }

    LoopAction onKeyADown(const std::string& utterance_id)
    {
        if (!connected_) {
            setError("connection_unavailable", true);
            return LoopAction::None;
        }
        if (state_ == DeviceState::ProposalReview && !command_id_.empty()) {
            state_ = DeviceState::Dispatching;
            return LoopAction::ApproveProposal;
        }
        if (state_ == DeviceState::WaitingPermission && permission_actionable_ && !permission_id_.empty()) {
            state_ = DeviceState::Running;
            return LoopAction::ApprovePermission;
        }
        if (isTerminalState() || state_ == DeviceState::Result || state_ == DeviceState::Error) {
            clearControlDisplay();
            state_ = DeviceState::Ready;
            return LoopAction::Acknowledge;
        }
        if (state_ != DeviceState::Ready) {
            return LoopAction::None;
        }
        active_id_ = utterance_id;
        result_text_.clear();
        error_code_.clear();
        retryable_error_ = false;
        state_ = DeviceState::Recording;
        return LoopAction::StartUtterance;
    }

    LoopAction onKeyAUp()
    {
        if (state_ != DeviceState::Recording || active_id_.empty()) {
            return LoopAction::None;
        }
        state_ = DeviceState::Transcribing;
        return LoopAction::EndUtterance;
    }

    LoopAction onKeyBCancel()
    {
        if (state_ == DeviceState::ProposalReview && !command_id_.empty()) {
            state_ = DeviceState::Cancelled;
            return LoopAction::RejectProposal;
        }
        if (state_ == DeviceState::WaitingPermission) {
            if (permission_actionable_ && !permission_id_.empty()) {
                state_ = DeviceState::Running;
                return LoopAction::DenyPermission;
            }
            permission_id_.clear();
            permission_actionable_ = false;
            state_ = DeviceState::Running;
            return LoopAction::Acknowledge;
        }
        if (isTerminalState()) {
            clearControlDisplay();
            state_ = connected_ ? DeviceState::Ready : DeviceState::Connecting;
            return LoopAction::Acknowledge;
        }
        if (state_ != DeviceState::Recording || active_id_.empty()) {
            return LoopAction::None;
        }
        active_id_.clear();
        state_ = connected_ ? DeviceState::Ready : DeviceState::Connecting;
        return LoopAction::CancelUtterance;
    }

    bool onTranscript(const std::string& id, const std::string& text)
    {
        if (state_ != DeviceState::Transcribing || id != active_id_) {
            return false;
        }
        active_id_.clear();
        result_text_ = text.empty() ? "No speech recognized" : text;
        error_code_.clear();
        retryable_error_ = text.empty();
        state_ = DeviceState::Result;
        return true;
    }

    bool onError(const std::string& id, const std::string& code, bool retryable)
    {
        if (!id.empty() && (active_id_.empty() || id != active_id_)) {
            return false;
        }
        active_id_.clear();
        setError(code, retryable);
        return true;
    }

    bool onProposal(const std::string& command_id, const std::string& preview)
    {
        if (state_ != DeviceState::Result || command_id.empty() || command_id.size() > 64 || preview.size() > 240) {
            return false;
        }
        command_id_ = command_id;
        result_text_ = preview;
        state_ = DeviceState::ProposalReview;
        return true;
    }

    bool onTaskAccepted(const std::string& command_id, const std::string& task_id)
    {
        if (state_ != DeviceState::Dispatching || command_id != command_id_ || task_id.empty() || task_id.size() > 64) {
            return false;
        }
        task_id_ = task_id;
        state_ = DeviceState::Running;
        return true;
    }

    bool onTaskRunning(const std::string& task_id)
    {
        if (task_id != task_id_ || task_id.empty()) {
            return false;
        }
        state_ = DeviceState::Running;
        return true;
    }

    bool onPermission(const std::string& task_id, const std::string& request_id,
                      const std::string& detail, bool actionable)
    {
        if (task_id != task_id_ || request_id.empty() || request_id.size() > 64 || detail.size() > 160) {
            return false;
        }
        permission_id_ = request_id;
        permission_actionable_ = actionable;
        result_text_ = detail;
        state_ = DeviceState::WaitingPermission;
        return true;
    }

    bool onTaskTerminal(const std::string& task_id, DeviceState state, const std::string& detail)
    {
        if (task_id != task_id_ || task_id.empty() || detail.size() > 512 ||
            (state != DeviceState::Completed && state != DeviceState::Cancelled && state != DeviceState::Failed)) {
            return false;
        }
        permission_id_.clear();
        permission_actionable_ = false;
        result_text_ = detail;
        state_ = state;
        return true;
    }

    bool onDispatchFailed(const std::string& command_id, const std::string& detail)
    {
        if (command_id.empty() || command_id != command_id_) {
            return false;
        }
        result_text_ = detail;
        state_ = DeviceState::Failed;
        return true;
    }

    void onControlError(const std::string& code, bool retryable = true)
    {
        setError(code, retryable);
    }

private:
    bool isTerminalState() const
    {
        return state_ == DeviceState::Completed || state_ == DeviceState::Cancelled || state_ == DeviceState::Failed;
    }

    void clearControlDisplay()
    {
        command_id_.clear();
        task_id_.clear();
        permission_id_.clear();
        permission_actionable_ = false;
        result_text_.clear();
    }
    void setError(const std::string& code, bool retryable)
    {
        result_text_.clear();
        error_code_ = code;
        retryable_error_ = retryable;
        state_ = DeviceState::Error;
    }

    DeviceState state_ = DeviceState::Connecting;
    bool connected_ = false;
    std::string active_id_;
    std::string result_text_;
    std::string error_code_;
    bool retryable_error_ = false;
    std::string command_id_;
    std::string task_id_;
    std::string permission_id_;
    bool permission_actionable_ = false;
};

}  // namespace stopwatch
