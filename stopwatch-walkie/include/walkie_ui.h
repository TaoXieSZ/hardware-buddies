#pragma once

#include "audio_loop.h"

#include <M5Unified.h>
#include <cstdint>
#include <memory>

namespace stopwatch {

struct UiModel {
    DeviceState state = DeviceState::Connecting;
    const char* detail = nullptr;
    uint16_t audio_level = 0;
    bool retryable = false;
    bool showcase = false;
};

class WalkieUi {
public:
    bool begin();
    void render(const UiModel& model, uint32_t now_ms);

private:
    void drawFrame(M5Canvas& canvas, const UiModel& model, uint32_t now_ms);
    void drawFallback(const UiModel& model);

    std::unique_ptr<M5Canvas> canvas_;
    bool sprite_ready_ = false;
#if defined(WALKIE_UI_DEMO)
    DeviceState rendered_asset_state_ = DeviceState::Connecting;
    bool rendered_asset_ready_ = false;
#endif
};

}  // namespace stopwatch
