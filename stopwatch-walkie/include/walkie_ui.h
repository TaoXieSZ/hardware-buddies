#pragma once

#include "audio_loop.h"

#include <M5Unified.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#if defined(WALKIE_MUSE_DIRECT)
#include "muse_crab.h"
#endif

namespace stopwatch {

struct UiModel {
    DeviceState state = DeviceState::Connecting;
    const char* detail = nullptr;
    uint16_t audio_level = 0;
    bool retryable = false;
    bool showcase = false;
    bool actionable = false;
    // Direct-to-Muse build: reading pages (confirm, answer, reminder, error) are drawn full screen in large type.
    const char* label = nullptr;   // one line above the text, e.g. "问：…", "提醒 1/3", "2 秒后发送"
    bool label_quiet = false;      // grey label (the heard question) instead of the state colour
    const char* title = nullptr;   // big word on the illustrated pages (home, listening, thinking)
    const char* hint_a = nullptr;  // action word next to the KEYA chip
    const char* hint_b = nullptr;  // action word next to the KEYB chip
    int progress = -1;             // confirm countdown 0-100 drawn as the ring; -1 = full ring
    bool reminder = false;         // a Result page that shows a reminder card (the crab waves it)
    bool asleep = false;           // long idle on home: dimmed, the crab sleeps, fewer frames
    int unread = 0;                // badge beside the crab on home
};

class WalkieUi {
public:
    bool begin();
    void render(const UiModel& model, uint32_t now_ms);

private:
    void drawFrame(M5Canvas& canvas, const UiModel& model, uint32_t now_ms);
#if defined(WALKIE_MUSE_DIRECT)
    void renderMuse(const UiModel& model, uint32_t now_ms);

    std::string muse_static_key_;
    CrabAct crab_act_ = CrabAct::Connecting;
    uint32_t crab_act_started_ms_ = 0;
    uint32_t muse_last_frame_ms_ = 0;
    std::vector<CrabRect> crab_rects_;
#endif
    void drawFallback(const UiModel& model);

    std::unique_ptr<M5Canvas> canvas_;
    bool sprite_ready_ = false;
    DeviceState rendered_asset_state_ = DeviceState::Connecting;
    bool rendered_asset_ready_ = false;
};

}  // namespace stopwatch
