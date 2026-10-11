#pragma once

// The Claude pixel crab for the direct-to-Muse build: one pose per mode as a pure function of time, and a pose
// as integer rectangles. Pure and native-testable; tools/muse-crab/dump_frames.cpp feeds the keyframe preview.
// Spec: openmuse-device-interaction "The crab acts, the ring tells"; DESIGN.md Visual language -> Crab.
// Grid ported from ui-preview/output/crab.html (itself from claude-video Crab.tsx): 18x5 half-block cells,
// each cell u px wide and 2u px tall; integer rectangles only, never rotated.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace stopwatch {

enum class CrabFace : uint8_t { Idle, Wow, Squint, Happy };
enum class CrabAct : uint8_t { Connecting, Home, Listening, Thinking, Confirm, Answer, Reminder, Error, Asleep };

constexpr int kCrabBigU = 9;       // home, listening, thinking, connecting: the crab is the page
constexpr int kCrabBigFeet = 236;
constexpr int kCrabSmallU = 3;     // reading pages: a status icon above the label
constexpr int kCrabSmallFeet = 70;

struct CrabPose {
    int x = 233;         // feet centre
    int y = kCrabBigFeet;
    int u = kCrabBigU;   // cell width in px
    float lift = 0;      // px above the feet line (negative = crouch)
    float claw_l = 0;    // 0 rest .. 1 raised
    float claw_r = 0;
    float snip = 0;      // claw tips opening 0..1
    CrabFace face = CrabFace::Idle;
    float ex = 0;        // eye offset in cells: |ex| <= 0.38, |ey| <= 0.42 keeps them in the sockets
    float ey = 0;
    float open = 1;      // eyelids 0..1
    float legs[4] = {0, 0, 0, 0};
};

struct CrabRect {
    int x;
    int y;
    int w;
    int h;
    bool eye;
};

constexpr float kCrabPi = 3.14159265F;

/** The one damped spring for every rebound: exp(-t/140 ms) * cos(0.0165 t). */
inline float crabSpring(float t_ms) { return std::exp(-t_ms / 140.0F) * std::cos(0.0165F * t_ms); }

/** 0 until `delay`, then up to 1 with a little overshoot (anticipation -> action -> follow-through). */
inline float crabSettle(uint32_t t, uint32_t delay)
{
    return t < delay ? 0.0F : 1.0F - crabSpring(static_cast<float>(t - delay));
}

/** Slow 0..1 wave used for breathing. */
inline float crabBreath(uint32_t now, uint32_t period_ms)
{
    return 0.5F - 0.5F * std::cos(2.0F * kCrabPi * static_cast<float>(now % period_ms) / static_cast<float>(period_ms));
}

/** Eyelids: a 120 ms blink every 4.3 s, a double blink every third time. */
inline float crabBlink(uint32_t now)
{
    const uint32_t phase = now % 12900;
    const uint32_t in_cycle = phase % 4300;
    if (in_cycle < 120) return 0.15F;
    if (phase >= 8600 && in_cycle >= 260 && in_cycle < 380) return 0.15F;
    return 1.0F;
}

/** Eyes looking toward a screen angle (degrees, 0 = 3 o'clock, clockwise like lgfx drawArc). */
inline void crabLook(CrabPose& pose, float angle_deg)
{
    const float a = angle_deg * kCrabPi / 180.0F;
    pose.ex = 0.38F * std::cos(a);
    pose.ey = 0.42F * std::sin(a);
}

/**
 * Pose for `act`, `t` ms after the act began, at absolute time `now`.
 * `level` is the microphone level 0..1000 (listening); `look_deg` is where the moving ring head is (connecting,
 * confirm). Big acts use u = 9 with the feet at y = 236; reading-page acts (confirm, answer, reminder, error) are
 * a small status icon at the top of the screen.
 */
inline CrabPose crabPose(CrabAct act, uint32_t t, uint32_t now, int level = 0, float look_deg = 270.0F)
{
    CrabPose p;
    const bool small = act == CrabAct::Confirm || act == CrabAct::Answer || act == CrabAct::Reminder ||
                       act == CrabAct::Error;
    if (small) {
        p.u = kCrabSmallU;
        p.y = kCrabSmallFeet;
    }
    const float scale = static_cast<float>(p.u) / 7.0F;
    p.open = crabBlink(now);
    switch (act) {
        case CrabAct::Home: {
            p.ex = -0.3F * crabSettle(t, 0);  // eyes drift down to KEYA: "hold A to talk"
            p.ey = 0.42F * crabSettle(t, 0);
            p.lift = 2.0F * crabBreath(now, 2600) + 5.0F * std::fmax(0.0F, crabSpring(static_cast<float>(t)));
            const uint32_t act_phase = now % 9000;  // a small act every 9 s: right claw snips twice
            if (act_phase < 700) {
                p.claw_r = 0.3F * std::sin(kCrabPi * static_cast<float>(act_phase) / 700.0F);
                p.snip = 0.5F - 0.5F * std::cos(4.0F * kCrabPi * static_cast<float>(act_phase) / 700.0F);
            }
            break;
        }
        case CrabAct::Listening: {
            p.face = CrabFace::Wow;
            p.open = 1.0F;
            const float up = crabSettle(t, 90);  // 90 ms crouch, then both claws spring up
            const float wiggle = 0.15F * static_cast<float>(level) / 1000.0F;
            p.claw_l = up + wiggle * std::sin(static_cast<float>(now) / 60.0F);
            p.claw_r = up + wiggle * std::sin(static_cast<float>(now) / 60.0F + 1.7F);
            p.lift = t < 90 ? -1.0F : 1.0F * crabBreath(now, 900);
            p.ey = -0.1F;
            p.legs[0] = p.legs[3] = 0.2F;
            break;
        }
        case CrabAct::Thinking:
            p.claw_l = 0.8F * crabSettle(t, 0);  // claw to chin, eyes up
            p.ex = -0.3F + 0.25F * std::sin(static_cast<float>(now) / 900.0F);
            p.ey = -0.42F;
            p.open = std::fmin(p.open, crabBlink(now + 2100));
            p.lift = 1.0F * crabBreath(now, 3200);
            break;
        case CrabAct::Connecting: {
            p.face = CrabFace::Squint;
            p.claw_r = 0.55F;  // shading its eyes, watching the ring head travel
            crabLook(p, look_deg);
            const bool step = (now / 220) % 2 == 0;
            p.legs[0] = p.legs[2] = step ? 0.4F : 0.0F;
            p.legs[1] = p.legs[3] = step ? 0.0F : 0.4F;
            break;
        }
        case CrabAct::Confirm:
            p.claw_r = 0.4F;
            crabLook(p, look_deg);  // watching the countdown run out
            break;
        case CrabAct::Answer:
            p.face = CrabFace::Happy;
            p.claw_l = p.claw_r = crabSettle(t, 80);
            p.lift = t < 80 ? -1.0F : (t < 340 ? 6.0F * std::sin(kCrabPi * static_cast<float>(t - 80) / 260.0F) : 0.0F);
            break;
        case CrabAct::Reminder:
            p.face = t < 600 ? CrabFace::Wow : CrabFace::Idle;
            p.claw_r = 0.6F + 0.25F * std::sin(static_cast<float>(now) / 180.0F);  // waving the card
            p.lift = t < 340 ? 4.0F * std::sin(kCrabPi * static_cast<float>(t) / 340.0F) : 0.0F;
            break;
        case CrabAct::Error:
            p.face = CrabFace::Squint;
            p.ey = 0.42F;
            p.lift = -1.0F + 0.5F * crabBreath(now, 3600);
            break;
        case CrabAct::Asleep:
            p.open = 0.18F;  // eyes shut, slow breathing
            p.ey = 0.3F;
            p.lift = 1.5F * crabBreath(now, 4800);
            break;
    }
    p.lift *= scale;
    return p;
}

/** The pose as integer rectangles (body, claws, legs, then eyes), exactly as crab.html draws it. */
inline void crabRects(const CrabPose& p, std::vector<CrabRect>& out)
{
    out.clear();
    const float u = static_cast<float>(p.u);
    const float fx = static_cast<float>(p.x);
    const float fy = static_cast<float>(p.y) - p.lift;
    auto round = [](float v) { return static_cast<int>(std::floor(v + 0.5F)); };  // Math.round
    auto rect = [&](float x, float y, float w, float h, bool eye) {
        out.push_back({round(fx + x), round(fy + y), std::max(1, round(w)), std::max(1, round(h)), eye});
    };
    auto cell = [&](int c, int r, int w, int h) {
        rect(static_cast<float>(c - 9) * u, -10.0F * u + static_cast<float>(r) * 2.0F * u, static_cast<float>(w) * u,
             static_cast<float>(h) * u, false);
    };
    // Body, with the eye sockets left open.
    cell(3, 0, 12, 2);
    cell(3, 1, 2, 2);
    cell(6, 1, 6, 2);
    cell(13, 1, 2, 2);
    cell(3, 2, 12, 2);
    cell(3, 3, 12, 2);
    // Claws: the inner cell stays, the tip column grows upward as it raises; the top cell splits to snip.
    auto claw = [&](int side, float raise) {
        const int base = side < 0 ? 2 : 15;
        const int tip = side < 0 ? 1 : 16;
        cell(base, 2, 1, 2);
        const float top = -6.0F * u - raise * 2.8F * u;
        const float total = 2.0F * u + raise * 2.8F * u;
        const float pin = 0.9F * u;
        const float gap = p.snip * 0.45F * u;
        rect(static_cast<float>(tip - 9) * u, top - gap, u, pin, false);
        rect(static_cast<float>(tip - 9) * u, top + pin, u, total - pin, false);
    };
    claw(-1, p.claw_l);
    claw(1, p.claw_r);
    // Legs reach up under the body so a bob never shows a gap.
    static constexpr int kLegCols[4] = {4, 6, 11, 13};
    for (int i = 0; i < 4; ++i) {
        rect(static_cast<float>(kLegCols[i] - 9) * u, -3.0F * u, u, (3.0F - p.legs[i] * 1.3F) * u, false);
    }
    // Eyes slide only inside their sockets.
    for (int c : {5, 12}) {
        const float cx = static_cast<float>(c - 9) * u + u / 2.0F + p.ex * u;
        const float cy = -10.0F * u + 3.0F * u + p.ey * u;
        if (p.face == CrabFace::Happy) {
            static constexpr float kCaret[3][2] = {{-0.55F, 0.15F}, {0.0F, -0.3F}, {0.55F, 0.15F}};
            for (const auto& d : kCaret) rect(cx + d[0] * u - 0.3F * u, cy + d[1] * u - 0.3F * u, 0.6F * u, 0.6F * u, true);
            continue;
        }
        const float w = p.face == CrabFace::Wow ? 1.35F : p.face == CrabFace::Squint ? 1.1F : 1.0F;
        const float h = p.face == CrabFace::Wow ? 2.5F : p.face == CrabFace::Squint ? 0.55F : 2.0F;
        const float hh = std::fmax(0.18F, h * p.open);
        rect(cx - w * u / 2.0F, cy - hh * u / 2.0F, w * u, hh * u, true);
    }
}

/** A box that always contains the crab for this u and feet position, lifts included (for clearing). */
inline CrabRect crabBounds(const CrabPose& p)
{
    const int pad = p.u * 2;
    return {p.x - 8 * p.u - 1, p.y - 10 * p.u - pad, 16 * p.u + 2, 10 * p.u + pad + p.u / 2 + 2, false};
}

}  // namespace stopwatch
