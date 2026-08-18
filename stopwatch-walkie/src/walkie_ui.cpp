#include "walkie_ui.h"
#include "ui_assets.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace stopwatch {
namespace {

constexpr int kCenter = 233;
constexpr int kCanvasSize = 466;
constexpr int kRingRadius = 216;
constexpr uint16_t kBackground = 0x0000;
constexpr uint16_t kSurface = 0x10A2;
constexpr uint16_t kMuted = 0x7BEF;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kAmber = 0xFDC9;
constexpr uint16_t kMint = 0x47B6;
constexpr uint16_t kCoral = 0xFA6D;
constexpr uint16_t kCyan = 0x563F;
constexpr uint16_t kError = 0xFBAA;
constexpr uint16_t kKeyA = 0xFE20;
constexpr uint16_t kKeyB = 0x3D9F;
constexpr float kPi = 3.14159265358979323846F;

struct StateCopy {
    const char* eyebrow;
    const char* title;
    const char* detail;
    uint16_t color;
};

StateCopy stateCopy(DeviceState state, bool retryable)
{
    switch (state) {
        case DeviceState::Connecting:
            return {"STOPWATCH WALKIE", "CONNECTING", "Looking for your Mac", kAmber};
        case DeviceState::Ready:
            return {"STOPWATCH WALKIE", "READY", "Hold KEYA to talk", kMint};
        case DeviceState::Recording:
            return {"LIVE MICROPHONE", "LISTENING", "Release KEYA to send", kCoral};
        case DeviceState::Transcribing:
            return {"ON YOUR MAC", "THINKING", "Turning speech into text", kCyan};
        case DeviceState::Result:
            return {"MESSAGE RECEIVED", "DONE", "Transcript is ready", kMint};
        case DeviceState::Error:
            return {retryable ? "TRY AGAIN" : "NEEDS ATTENTION", "NOT SENT", "Check the connection", kError};
        case DeviceState::ProposalReview:
            return {"PHYSICAL CONFIRMATION", "REVIEW", "Approve exact command?", kAmber};
        case DeviceState::Dispatching:
            return {"LOCAL CONTROL PLANE", "SENDING", "Revalidating target", kCyan};
        case DeviceState::Running:
            return {"AGENT SESSION", "RUNNING", "Watching live session", kCyan};
        case DeviceState::WaitingPermission:
            return {"AGENT NEEDS INPUT", "PERMISSION", "Review requested action", kAmber};
        case DeviceState::Completed:
            return {"AGENT SESSION", "COMPLETE", "Pane snapshot received", kMint};
        case DeviceState::Cancelled:
            return {"NOT DISPATCHED", "CANCELLED", "No command was sent", kMuted};
        case DeviceState::Failed:
            return {"CONTROL PLANE", "FAILED", "Command was not completed", kError};
    }
    return {"STOPWATCH WALKIE", "UNKNOWN", "", kWhite};
}

const char* safeDetail(const UiModel& model, const StateCopy& copy)
{
    return model.detail != nullptr && model.detail[0] != '\0' ? model.detail : copy.detail;
}

void drawMic(M5Canvas& canvas, uint16_t color)
{
    canvas.fillRoundRect(kCenter - 25, 119, 50, 82, 25, color);
    canvas.fillRoundRect(kCenter - 10, 130, 20, 58, 10, kBackground);
    canvas.drawArc(kCenter, 174, 48, 43, 0, 180, color);
    canvas.fillRect(kCenter - 4, 214, 8, 24, color);
    canvas.fillRoundRect(kCenter - 28, 234, 56, 8, 4, color);
}

void drawWaveform(M5Canvas& canvas, uint16_t color, uint16_t audio_level, uint32_t now_ms)
{
    constexpr int kBars = 15;
    constexpr int kGap = 12;
    const int base = std::max<int>(10, std::min<int>(90, audio_level * 90 / 1000));
    for (int i = 0; i < kBars; ++i) {
        const float phase = static_cast<float>(now_ms) * 0.012F + static_cast<float>(i) * 0.75F;
        const float shape = 0.34F + 0.66F * std::fabs(std::sin(phase));
        const int distance = std::abs(i - kBars / 2);
        const int envelope = 100 - distance * 7;
        const int height = std::max(8, base * envelope / 100 * static_cast<int>(shape * 100.0F) / 100);
        const int x = kCenter + (i - kBars / 2) * kGap;
        canvas.fillRoundRect(x - 3, 183 - height / 2, 7, height, 3, color);
    }
}

void drawConnecting(M5Canvas& canvas, uint16_t color, uint32_t now_ms)
{
    const float angle = static_cast<float>((now_ms / 12) % 360) * kPi / 180.0F;
    canvas.drawCircle(kCenter, 173, 58, kSurface);
    canvas.drawCircle(kCenter, 173, 40, kSurface);
    canvas.fillCircle(kCenter, 173, 8, color);
    for (int i = 0; i < 3; ++i) {
        const float a = angle + static_cast<float>(i) * 2.094395F;
        const int x = kCenter + static_cast<int>(std::cos(a) * 54.0F);
        const int y = 173 + static_cast<int>(std::sin(a) * 54.0F);
        canvas.fillCircle(x, y, 7 - i, color);
    }
}

void drawThinking(M5Canvas& canvas, uint16_t color, uint32_t now_ms)
{
    const float angle = static_cast<float>((now_ms / 10) % 360) * kPi / 180.0F;
    canvas.drawCircle(kCenter, 173, 67, kSurface);
    canvas.fillCircle(kCenter, 173, 28, kSurface);
    for (int i = 0; i < 3; ++i) {
        const float a = angle + static_cast<float>(i) * 2.094395F;
        const int x = kCenter + static_cast<int>(std::cos(a) * 67.0F);
        const int y = 173 + static_cast<int>(std::sin(a) * 67.0F);
        canvas.fillCircle(x, y, 10, color);
    }
}

void drawResult(M5Canvas& canvas, uint16_t color)
{
    canvas.drawCircle(kCenter, 173, 62, color);
    canvas.drawCircle(kCenter, 173, 61, color);
    canvas.drawLine(kCenter - 29, 174, kCenter - 8, 195, color);
    canvas.drawLine(kCenter - 28, 175, kCenter - 7, 196, color);
    canvas.drawLine(kCenter - 8, 195, kCenter + 34, 150, color);
    canvas.drawLine(kCenter - 7, 196, kCenter + 35, 151, color);
}

void drawError(M5Canvas& canvas, uint16_t color)
{
    canvas.drawCircle(kCenter, 173, 62, color);
    canvas.fillRoundRect(kCenter - 7, 129, 14, 66, 7, color);
    canvas.fillCircle(kCenter, 216, 8, color);
}

void drawButtonHint(M5Canvas& canvas, int x, const char* key, const char* label, uint16_t color)
{
    canvas.fillCircle(x, 376, 18, color);
    canvas.setTextDatum(middle_center);
    canvas.setTextFont(2);
    canvas.setTextColor(kBackground, color);
    canvas.drawString(key, x, 375);
    canvas.setTextDatum(top_center);
    canvas.setTextFont(2);
    canvas.setTextColor(kMuted, kBackground);
    canvas.drawString(label, x, 400);
}

void drawOuterRing(M5Canvas& canvas, DeviceState state, uint16_t color, uint32_t now_ms)
{
    canvas.drawCircle(kCenter, kCenter, kRingRadius, kSurface);
    canvas.drawCircle(kCenter, kCenter, kRingRadius - 1, kSurface);
    if (state == DeviceState::Connecting || state == DeviceState::Transcribing ||
        state == DeviceState::Dispatching || state == DeviceState::Running) {
        const float start = static_cast<float>((now_ms / 8) % 360);
        canvas.drawArc(kCenter, kCenter, kRingRadius, kRingRadius - 5, start, start + 72.0F, color);
        return;
    }
    if (state == DeviceState::Recording) {
        const int pulse = 2 + static_cast<int>((std::sin(static_cast<float>(now_ms) * 0.008F) + 1.0F) * 2.0F);
        canvas.drawArc(kCenter, kCenter, kRingRadius, kRingRadius - 4 - pulse, 0, 360, color);
        return;
    }
    canvas.drawArc(kCenter, kCenter, kRingRadius, kRingRadius - 4, 0, 360, color);
}

}  // namespace

bool WalkieUi::begin()
{
    canvas_.reset(new M5Canvas(&M5.Display));
    canvas_->setColorDepth(16);
    canvas_->setPsram(true);
    sprite_ready_ = canvas_->createSprite(kCanvasSize, kCanvasSize) != nullptr;
    Serial.printf("[ui] %s %dx%d canvas on %dx%d display\n", sprite_ready_ ? "psram" : "direct", kCanvasSize,
                  kCanvasSize, M5.Display.width(), M5.Display.height());

    if (sprite_ready_) {
        constexpr DeviceState kStates[] = {DeviceState::Connecting, DeviceState::Ready, DeviceState::Recording,
                                           DeviceState::Transcribing, DeviceState::Result, DeviceState::Error};
        size_t decoded = 0;
        for (const DeviceState state : kStates) {
            const UiAsset asset = uiAssetFor(state);
            canvas_->fillSprite(kBackground);
            decoded += asset.data != nullptr && canvas_->drawPng(asset.data, asset.size, 0, 0) ? 1 : 0;
        }
        canvas_->fillSprite(kBackground);
        Serial.printf("[ui] preview assets verified=%u/%u\n", static_cast<unsigned>(decoded),
                      static_cast<unsigned>(sizeof(kStates) / sizeof(kStates[0])));
    }

    return sprite_ready_;
}

void WalkieUi::render(const UiModel& model, uint32_t now_ms)
{
    if (!sprite_ready_) {
        drawFallback(model);
        return;
    }

    const bool state_changed = !rendered_asset_ready_ || rendered_asset_state_ != model.state;
    if (state_changed) {
        const UiAsset asset = uiAssetFor(model.state);
        canvas_->fillSprite(kBackground);
        if (asset.data != nullptr && canvas_->drawPng(asset.data, asset.size, 0, 0)) {
            rendered_asset_state_ = model.state;
            rendered_asset_ready_ = true;
            Serial.printf("[ui] hd asset state=%s bytes=%u\n", stateName(model.state),
                          static_cast<unsigned>(asset.size));
        } else {
            rendered_asset_ready_ = false;
            // States without an hd asset intentionally fall back to the drawn
            // frame; only log real decode failures (asset present but broken).
            if (asset.data != nullptr) {
                Serial.printf("[ui] hd asset decode failed state=%s\n", stateName(model.state));
            }
        }
    }

    if (!rendered_asset_ready_) {
        drawFrame(*canvas_, model, now_ms);
    } else if (!model.showcase) {
        if (model.state == DeviceState::Recording) {
            canvas_->fillRect(130, 105, 206, 136, kBackground);
            drawWaveform(*canvas_, stateCopy(model.state, model.retryable).color, model.audio_level, now_ms);
        }

        if (state_changed) {
            const StateCopy copy = stateCopy(model.state, model.retryable);
            if (model.state == DeviceState::Result || model.state == DeviceState::Error) {
                canvas_->fillRect(70, 306, 326, 42, kBackground);
                canvas_->setTextDatum(top_center);
                canvas_->setFont(&fonts::efontCN_16);
                canvas_->setTextColor(copy.color, kBackground);
                canvas_->drawString(safeDetail(model, copy), kCenter, 318);
                canvas_->setTextFont(2);
            }

            canvas_->fillRect(118, 350, 230, 75, kBackground);
            if (model.state == DeviceState::Ready) {
                drawButtonHint(*canvas_, kCenter, "A", "HOLD TO TALK", kKeyA);
            } else if (model.state == DeviceState::Recording) {
                drawButtonHint(*canvas_, 154, "A", "RELEASE", kKeyA);
                drawButtonHint(*canvas_, 312, "B", "CANCEL", kKeyB);
            }
        }
    }

    if (rendered_asset_ready_ && !state_changed && model.state != DeviceState::Recording) {
        return;
    }
    const int offset_x = (M5.Display.width() - kCanvasSize) / 2;
    const int offset_y = (M5.Display.height() - kCanvasSize) / 2;
    canvas_->pushSprite(&M5.Display, offset_x, offset_y);
}

void WalkieUi::drawFrame(M5Canvas& canvas, const UiModel& model, uint32_t now_ms)
{
    const StateCopy copy = stateCopy(model.state, model.retryable);
    canvas.fillSprite(kBackground);
    drawOuterRing(canvas, model.state, copy.color, now_ms);

    canvas.setTextDatum(top_center);
    canvas.setTextFont(2);
    canvas.setTextColor(kMuted, kBackground);
    canvas.drawString(copy.eyebrow, kCenter, 61);

    switch (model.state) {
        case DeviceState::Connecting:
            drawConnecting(canvas, copy.color, now_ms);
            break;
        case DeviceState::Ready:
            drawMic(canvas, copy.color);
            break;
        case DeviceState::Recording:
            drawWaveform(canvas, copy.color, model.audio_level, now_ms);
            break;
        case DeviceState::Transcribing:
            drawThinking(canvas, copy.color, now_ms);
            break;
        case DeviceState::Result:
            drawResult(canvas, copy.color);
            break;
        case DeviceState::Error:
            drawError(canvas, copy.color);
            break;
        case DeviceState::ProposalReview:
            drawResult(canvas, copy.color);
            break;
        case DeviceState::Dispatching:
        case DeviceState::Running:
            drawThinking(canvas, copy.color, now_ms);
            break;
        case DeviceState::WaitingPermission:
            drawError(canvas, copy.color);
            break;
        case DeviceState::Completed:
            drawResult(canvas, copy.color);
            break;
        case DeviceState::Cancelled:
        case DeviceState::Failed:
            drawError(canvas, copy.color);
            break;
    }

    canvas.setTextDatum(top_center);
    canvas.setTextFont(4);
    canvas.setTextColor(kWhite, kBackground);
    canvas.drawString(copy.title, kCenter, 272);
    canvas.setTextFont(2);
    canvas.setTextColor(copy.color, kBackground);
    canvas.drawString(safeDetail(model, copy), kCenter, 318);

    if (model.showcase) {
        drawButtonHint(canvas, 154, "A", "NEXT", kKeyA);
        drawButtonHint(canvas, 312, "B", "BACK", kKeyB);
    } else if (model.state == DeviceState::Ready) {
        drawButtonHint(canvas, kCenter, "A", "HOLD TO TALK", kKeyA);
    } else if (model.state == DeviceState::Recording) {
        drawButtonHint(canvas, 154, "A", "RELEASE", kKeyA);
        drawButtonHint(canvas, 312, "B", "CANCEL", kKeyB);
    } else if (model.state == DeviceState::ProposalReview ||
               (model.state == DeviceState::WaitingPermission && model.actionable)) {
        drawButtonHint(canvas, 154, "A", "APPROVE", kKeyA);
        drawButtonHint(canvas, 312, "B", "REJECT", kKeyB);
    } else if (model.state == DeviceState::Completed || model.state == DeviceState::Cancelled ||
               model.state == DeviceState::Failed) {
        drawButtonHint(canvas, kCenter, "A", "DONE", kKeyA);
    }
}

void WalkieUi::drawFallback(const UiModel& model)
{
    const StateCopy copy = stateCopy(model.state, model.retryable);
    M5.Display.clear(kBackground);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextFont(4);
    M5.Display.setTextColor(copy.color, kBackground);
    M5.Display.drawString(copy.title, kCenter, kCenter - 18);
    M5.Display.setTextFont(2);
    M5.Display.setTextColor(kWhite, kBackground);
    M5.Display.drawString(safeDetail(model, copy), kCenter, kCenter + 28);
}

}  // namespace stopwatch
