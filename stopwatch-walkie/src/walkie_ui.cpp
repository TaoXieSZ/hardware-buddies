#include "walkie_ui.h"
#include "ui_assets.h"

#if defined(WALKIE_MUSE_DIRECT)
#include "muse_fonts.h"
#include "muse_layout.h"
#endif

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
[[maybe_unused]] constexpr uint16_t kViolet = 0xB47F;  // #B78CFF, Muse confirm step
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
#if defined(WALKIE_MUSE_DIRECT)
            return {"OPENMUSE", "CONFIRM", "", kViolet};
#else
            return {"PHYSICAL CONFIRMATION", "REVIEW", "Approve exact command?", kAmber};
#endif
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

#if defined(WALKIE_MUSE_DIRECT)
// "small line\nmain line" (heard question + answer, reminder title + entry, heard + countdown).
void drawMuseDetail(M5Canvas& canvas, const char* detail, uint16_t color)
{
    canvas.setTextDatum(top_center);
    const char* split = std::strchr(detail, '\n');
    if (split == nullptr) {
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(color, kBackground);
        canvas.drawString(detail, kCenter, 318);
        return;
    }
    const std::string first(detail, split);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(kMuted, kBackground);
    canvas.drawString(first.c_str(), kCenter, 308);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(color, kBackground);
    canvas.drawString(split + 1, kCenter, 326);
}

// Reading-page geometry; tools/muse-fonts/preview_reading.py mirrors these numbers.
constexpr int kBodyPx = 32;
constexpr int kSmallPx = 24;
constexpr int kLabelTop = 76;
constexpr int kLabelRadius = 206;
constexpr int kBandTop = 112;
constexpr int kBandBottom = 366;
constexpr int kHintTop = 376;
constexpr int kChip = 26;

bool isReadingState(DeviceState state)
{
    return state == DeviceState::ProposalReview || state == DeviceState::Result || state == DeviceState::Error;
}

int museAdvanceForLayout(int px, uint32_t cp)
{
    const lgfx::VLWfont* font = museFont(px);
    if (cp == ' ' && font != nullptr) return font->spaceWidth;  // lgfx draws a space at spaceWidth
    return museAdvance(px, cp);
}

/** One line of anti-aliased text with its box top at `top`, horizontally centred on `cx`. */
void drawSmoothLine(M5Canvas& canvas, const std::string& text, int px, int width, int cx, int top, uint16_t color)
{
    const lgfx::VLWfont* font = museFont(px);
    if (font == nullptr || text.empty()) return;
    canvas.setFont(font);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(color);  // no background fill: glyph edges blend with what is already drawn
    canvas.drawString(text.c_str(), cx - width / 2, top + (museLineHeight(px) - font->yAdvance) / 2);
}

void drawClippedLine(M5Canvas& canvas, const char* text, int px, int limit, int top, uint16_t color)
{
    if (text == nullptr || text[0] == '\0') return;
    int width = 0;
    const std::string line = museClipLine(text, px, limit, museAdvanceForLayout, &width);
    drawSmoothLine(canvas, line, px, width, kCenter, top, color);
}

/** "(A) 再问   (B) 好的" centred on one row; a null or empty word leaves that key out. */
void drawHintRow(M5Canvas& canvas, const char* hint_a, const char* hint_b, int top)
{
    struct Hint {
        const char* key;
        const char* word;
        uint16_t color;
        int width;
    };
    Hint hints[2] = {{"A", hint_a, kKeyA, 0}, {"B", hint_b, kKeyB, 0}};
    constexpr int kGapLabel = 6;
    constexpr int kGapGroup = 20;
    int total = 0;
    int shown = 0;
    for (Hint& h : hints) {
        if (h.word == nullptr || h.word[0] == '\0') continue;
        int word_width = 0;
        museClipLine(h.word, kSmallPx, 200, museAdvanceForLayout, &word_width);
        h.width = kChip + kGapLabel + word_width;
        total += h.width + (shown++ > 0 ? kGapGroup : 0);
    }
    int x = kCenter - total / 2;
    const int lh = museLineHeight(kSmallPx);
    for (const Hint& h : hints) {
        if (h.width == 0) continue;
        canvas.fillCircle(x + kChip / 2, top + lh / 2, kChip / 2, h.color);
        drawSmoothLine(canvas, h.key, kSmallPx, museAdvanceForLayout(kSmallPx, h.key[0]), x + kChip / 2, top,
                       kBackground);
        const int word_width = h.width - kChip - kGapLabel;
        drawSmoothLine(canvas, h.word, kSmallPx, word_width, x + kChip + kGapLabel + word_width / 2, top, kMuted);
        x += h.width + kGapGroup;
    }
}

void drawStatusRing(M5Canvas& canvas, uint16_t color, int progress)
{
    canvas.drawCircle(kCenter, kCenter, kRingRadius, kSurface);
    canvas.drawCircle(kCenter, kCenter, kRingRadius - 1, kSurface);
    if (progress < 0) {
        canvas.drawArc(kCenter, kCenter, kRingRadius, kRingRadius - 4, 0, 360, color);
        return;
    }
    // Clockwise from 12 o'clock, shrinking as the countdown runs out.
    const float end = 270.0F + 360.0F * static_cast<float>(std::min(progress, 100)) / 100.0F;
    if (end <= 360.0F) {
        canvas.drawArc(kCenter, kCenter, kRingRadius, kRingRadius - 4, 270, end, color);
    } else {
        canvas.drawArc(kCenter, kCenter, kRingRadius, kRingRadius - 4, 270, 360, color);
        canvas.drawArc(kCenter, kCenter, kRingRadius, kRingRadius - 4, 0, end - 360.0F, color);
    }
}

/** Confirm, answer, reminder and error: a label line, the text as large as fits, and the key hints. */
void drawReadingPage(M5Canvas& canvas, const UiModel& model, uint16_t color)
{
    canvas.fillSprite(kBackground);
    drawStatusRing(canvas, color, model.progress);
    const int label_top = kLabelTop;
    drawClippedLine(canvas, model.label, kSmallPx, museChordWidth(MuseRound{233, 233, kLabelRadius}, label_top,
                                                                  label_top + museLineHeight(kSmallPx)),
                    label_top, model.label_quiet ? kMuted : color);
    const MuseTextBlock block = museLayout(model.detail != nullptr ? model.detail : "", {kBodyPx, kSmallPx},
                                           kBandTop, kBandBottom, MuseRound(), museAdvanceForLayout);
    for (size_t i = 0; i < block.lines.size(); ++i) {
        drawSmoothLine(canvas, block.lines[i], block.px, block.widths[i], kCenter,
                       block.top + static_cast<int>(i) * block.line_height, kWhite);
    }
    drawHintRow(canvas, model.hint_a, model.hint_b, kHintTop);
}

constexpr uint16_t kCrabBody = 0xDBAA;  // #D97757
constexpr uint16_t kCrabEye = 0x18C2;   // #1E1A17
constexpr int kTitleTop = 256;
constexpr int kDetailTop = 300;
constexpr int kStageTop = 86;
constexpr int kStageBottom = 246;
constexpr uint32_t kMuseFrameMs = 80;
constexpr uint32_t kMuseAsleepFrameMs = 400;

CrabAct crabActFor(const UiModel& model)
{
    switch (model.state) {
        case DeviceState::Connecting:
            return CrabAct::Connecting;
        case DeviceState::Recording:
            return CrabAct::Listening;
        case DeviceState::Transcribing:
            return CrabAct::Thinking;
        case DeviceState::ProposalReview:
            return CrabAct::Confirm;
        case DeviceState::Result:
            return model.reminder ? CrabAct::Reminder : CrabAct::Answer;
        case DeviceState::Error:
            return CrabAct::Error;
        default:
            return model.asleep ? CrabAct::Asleep : CrabAct::Home;
    }
}

/** Big pages (home, listening, thinking, connecting): title, one status line and the key hints. */
void drawCrabPageText(M5Canvas& canvas, const UiModel& model, uint16_t color)
{
    drawClippedLine(canvas, model.title, kBodyPx, 260, kTitleTop, kWhite);
    drawClippedLine(canvas, model.detail, kSmallPx, 300, kDetailTop, color);
    drawHintRow(canvas, model.hint_a, model.hint_b, kHintTop);
}

/** Microphone level as bars on both sides of the crab (listening). */
void drawSideWaves(M5Canvas& canvas, uint16_t color, uint16_t level, uint32_t now_ms)
{
    static constexpr int kHeights[2][4] = {{22, 40, 58, 30}, {30, 58, 40, 22}};
    const float lvl = static_cast<float>(std::min<uint16_t>(level, 1000)) / 1000.0F;
    const int cy = kCrabBigFeet - 5 * kCrabBigU;
    for (int side = 0; side < 2; ++side) {
        for (int i = 0; i < 4; ++i) {
            const int gap = 8 * kCrabBigU + 12 + i * 12;
            const int x = side == 0 ? kCenter - gap - 7 : kCenter + gap;
            const float shape =
                0.6F + 0.4F * std::fabs(std::sin(static_cast<float>(now_ms) * 0.012F + static_cast<float>(i) * 0.75F +
                                                 static_cast<float>(side) * 1.3F));
            const int h = std::max(8, static_cast<int>(static_cast<float>(kHeights[side][i]) * (0.3F + 0.7F * lvl) * shape));
            canvas.fillRoundRect(x, cy - h / 2, 7, h, 3, color);
        }
    }
}

/** Three dots circling above the crab's head (thinking). */
void drawThoughtDots(M5Canvas& canvas, uint16_t color, uint32_t now_ms)
{
    const int cy = kCrabBigFeet - 10 * kCrabBigU - 26;
    for (int k = 0; k < 3; ++k) {
        const float a = static_cast<float>(now_ms) * 0.004F + static_cast<float>(k) * 2.0944F;
        canvas.fillCircle(kCenter + static_cast<int>(30.0F * std::cos(a)), cy + static_cast<int>(7.0F * std::sin(a)), 6,
                          color);
    }
}

void drawUnreadBadge(M5Canvas& canvas, int unread)
{
    if (unread <= 0) return;
    const int bx = kCenter + 8 * kCrabBigU + 8;
    const int by = kCrabBigFeet - 10 * kCrabBigU - 8;
    canvas.fillCircle(bx, by, 13, kCoral);
    const std::string n = unread > 9 ? "9+" : std::to_string(unread);
    int width = 0;
    museClipLine(n, kSmallPx, 40, museAdvanceForLayout, &width);
    drawSmoothLine(canvas, n, kSmallPx, width, bx, by - museLineHeight(kSmallPx) / 2, kBackground);
}

void drawCrab(M5Canvas& canvas, const std::vector<CrabRect>& rects)
{
    for (const CrabRect& r : rects) canvas.fillRect(r.x, r.y, r.w, r.h, r.eye ? kCrabEye : kCrabBody);
}

/** Where the moving ring head is, for the crab's eyes (lgfx degrees: 0 = 3 o'clock, clockwise). */
float ringHeadDeg(CrabAct act, const UiModel& model, uint32_t now_ms)
{
    if (act == CrabAct::Connecting) return static_cast<float>((now_ms / 8) % 360) + 72.0F;
    if (act == CrabAct::Confirm && model.progress >= 0) return 270.0F + 3.6F * static_cast<float>(model.progress);
    return 270.0F;
}

/** Everything in a Muse page's text layer, as one comparable string. */
std::string museContentKey(const UiModel& model)
{
    std::string key(1, static_cast<char>(model.state));
    for (const char* part : {model.detail, model.label, model.title, model.hint_a, model.hint_b}) {
        key += '\x1f';
        if (part != nullptr) key += part;
    }
    return key;  // the countdown ring is redrawn every frame, so `progress` is not part of the text layer
}
#endif

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

#if defined(WALKIE_MUSE_DIRECT)
    if (museFont(kBodyPx) != nullptr) {
        renderMuse(model, now_ms);
        return;
    }
    // No fonts partition: the illustrated PNG pages with the small bitmap font, as before.
    static std::string last_content;
    const std::string content = museContentKey(model);
    const bool detail_changed = content != last_content;
    last_content = content;
#else
    const bool detail_changed = false;
#endif
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
            Serial.printf("[ui] hd asset decode failed state=%s\n", stateName(model.state));
        }
    }

    if (!rendered_asset_ready_) {
        drawFrame(*canvas_, model, now_ms);
    } else if (!model.showcase) {
        if (model.state == DeviceState::Recording) {
            canvas_->fillRect(130, 105, 206, 136, kBackground);
            drawWaveform(*canvas_, stateCopy(model.state, model.retryable).color, model.audio_level, now_ms);
        }

        if (state_changed || detail_changed) {
            const StateCopy copy = stateCopy(model.state, model.retryable);
#if defined(WALKIE_MUSE_DIRECT)
            if (model.state == DeviceState::Ready) {
                canvas_->fillRect(70, 306, 326, 42, kBackground);
                if (model.detail != nullptr && model.detail[0] != '\0') {
                    drawMuseDetail(*canvas_, model.detail, copy.color);
                }
            }
            if (model.state == DeviceState::Result || model.state == DeviceState::Error) {
                canvas_->fillRect(70, 306, 326, 42, kBackground);
                drawMuseDetail(*canvas_, safeDetail(model, copy), copy.color);
                canvas_->setTextFont(2);
            }
            canvas_->fillRect(118, 350, 230, 75, kBackground);
            if (model.state == DeviceState::Ready) {
                drawButtonHint(*canvas_, kCenter, "A", "HOLD TO TALK", kKeyA);
            } else if (model.state == DeviceState::Recording) {
                drawButtonHint(*canvas_, 154, "A", "RELEASE", kKeyA);
                drawButtonHint(*canvas_, 312, "B", "CANCEL", kKeyB);
            }
#else
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
#endif
        }
    }

    if (rendered_asset_ready_ && !state_changed && !detail_changed && model.state != DeviceState::Recording) {
        return;
    }
    const int offset_x = (M5.Display.width() - kCanvasSize) / 2;
    const int offset_y = (M5.Display.height() - kCanvasSize) / 2;
    canvas_->pushSprite(&M5.Display, offset_x, offset_y);
}

#if defined(WALKIE_MUSE_DIRECT)
// Every Muse page is drawn here: the text layer only when the content changes, the crab, ring and their effects
// every frame (12.5 fps; 2.5 fps while asleep). Spec: openmuse-device-interaction "The crab acts, the ring tells".
void WalkieUi::renderMuse(const UiModel& model, uint32_t now_ms)
{
    const CrabAct act = crabActFor(model);
    if (act != crab_act_) {
        crab_act_ = act;
        crab_act_started_ms_ = now_ms;
    }
    const std::string key = museContentKey(model) + static_cast<char>(act);
    const bool changed = key != muse_static_key_;
    const uint32_t frame_ms = model.asleep ? kMuseAsleepFrameMs : kMuseFrameMs;
    if (!changed && now_ms - muse_last_frame_ms_ < frame_ms) return;
    muse_last_frame_ms_ = now_ms;

    const uint16_t color = stateCopy(model.state, model.retryable).color;
    const bool reading = isReadingState(model.state);
    if (changed) {
        muse_static_key_ = key;
        rendered_asset_ready_ = false;  // the canvas no longer holds a PNG illustration
        if (reading) {
            drawReadingPage(*canvas_, model, color);
        } else {
            canvas_->fillSprite(kBackground);
            drawCrabPageText(*canvas_, model, color);
        }
    }

    const CrabPose pose = crabPose(act, now_ms - crab_act_started_ms_, now_ms, model.audio_level,
                                   ringHeadDeg(act, model, now_ms));
    if (reading) {
        const CrabRect box = crabBounds(pose);
        canvas_->fillRect(box.x, box.y, box.w, box.h, kBackground);
    } else {
        canvas_->fillRect(90, kStageTop, 286, kStageBottom - kStageTop, kBackground);
    }
    canvas_->fillArc(kCenter, kCenter, kRingRadius + 1, kRingRadius - 12, 0, 360, kBackground);
    if (reading) {
        drawStatusRing(*canvas_, color, model.progress);
    } else {
        drawOuterRing(*canvas_, model.state, color, now_ms);
        if (act == CrabAct::Listening) drawSideWaves(*canvas_, color, model.audio_level, now_ms);
        if (act == CrabAct::Thinking) drawThoughtDots(*canvas_, color, now_ms);
        if (act == CrabAct::Home || act == CrabAct::Asleep) drawUnreadBadge(*canvas_, model.unread);
    }
    crabRects(pose, crab_rects_);
    drawCrab(*canvas_, crab_rects_);
    canvas_->pushSprite(&M5.Display, (M5.Display.width() - kCanvasSize) / 2, (M5.Display.height() - kCanvasSize) / 2);
}
#endif

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
#if defined(WALKIE_MUSE_DIRECT)
    drawMuseDetail(canvas, safeDetail(model, copy), copy.color);
    canvas.setTextFont(2);
#else
    canvas.setTextFont(2);
    canvas.setTextColor(copy.color, kBackground);
    canvas.drawString(safeDetail(model, copy), kCenter, 318);
#endif

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
#if defined(WALKIE_MUSE_DIRECT)
        drawButtonHint(canvas, 154, "A", "SEND", kKeyA);
        drawButtonHint(canvas, 312, "B", "CANCEL", kKeyB);
#else
        drawButtonHint(canvas, 154, "A", "APPROVE", kKeyA);
        drawButtonHint(canvas, 312, "B", "REJECT", kKeyB);
#endif
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
