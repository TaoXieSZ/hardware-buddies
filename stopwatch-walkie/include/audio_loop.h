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
};

enum class LoopAction {
    None,
    StartUtterance,
    EndUtterance,
    CancelUtterance,
    DropPartial,
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

    LoopAction onConnected()
    {
        connected_ = true;
        active_id_.clear();
        result_text_.clear();
        error_code_.clear();
        retryable_error_ = false;
        state_ = DeviceState::Ready;
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
        if (state_ == DeviceState::Recording || state_ == DeviceState::Transcribing) {
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

private:
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
};

}  // namespace stopwatch
