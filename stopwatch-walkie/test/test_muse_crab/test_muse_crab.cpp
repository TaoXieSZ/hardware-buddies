#include "muse_crab.h"

#include <unity.h>

using namespace stopwatch;

namespace {

constexpr CrabAct kActs[] = {CrabAct::Connecting, CrabAct::Home,   CrabAct::Listening, CrabAct::Thinking, CrabAct::Confirm,
                             CrabAct::Answer,     CrabAct::Reminder, CrabAct::Error,   CrabAct::Asleep};

bool inside(const CrabRect& r, const CrabRect& box)
{
    return r.x >= box.x && r.y >= box.y && r.x + r.w <= box.x + box.w && r.y + r.h <= box.y + box.h;
}

}  // namespace

void test_every_pixel_stays_inside_the_cleared_box()
{
    // The firmware clears crabBounds() each frame; anything outside it would leave a trail.
    std::vector<CrabRect> rects;
    for (CrabAct act : kActs) {
        for (uint32_t t = 0; t < 3000; t += 20) {
            for (int level : {0, 1000}) {
                const CrabPose pose = crabPose(act, t, 5000 + t * 7, level, static_cast<float>(t % 360));
                crabRects(pose, rects);
                const CrabRect box = crabBounds(pose);
                for (const CrabRect& r : rects) {
                    TEST_ASSERT_TRUE_MESSAGE(inside(r, box), "crab pixel outside its bounds");
                    TEST_ASSERT_TRUE(r.w >= 1 && r.h >= 1);
                }
            }
        }
    }
}

void test_eyes_stay_on_the_body()
{
    std::vector<CrabRect> rects;
    for (CrabAct act : kActs) {
        for (uint32_t t = 0; t < 3000; t += 40) {
            const CrabPose pose = crabPose(act, t, 9000 + t, 500, static_cast<float>(t % 360));
            crabRects(pose, rects);
            const int top = rects[0].y;  // first rect: the body's top row, full width
            const int left = rects[0].x;
            const int right = rects[0].x + rects[0].w;
            for (const CrabRect& r : rects) {
                if (!r.eye) continue;
                TEST_ASSERT_TRUE(r.x >= left && r.x + r.w <= right);
                TEST_ASSERT_TRUE(r.y >= top && r.y + r.h <= top + 8 * pose.u);
            }
        }
    }
}

void test_big_and_small_sizes()
{
    const CrabPose home = crabPose(CrabAct::Home, 2000, 2000);
    TEST_ASSERT_EQUAL(kCrabBigU, home.u);
    TEST_ASSERT_EQUAL(kCrabBigFeet, home.y);
    const CrabPose answer = crabPose(CrabAct::Answer, 2000, 2000);
    TEST_ASSERT_EQUAL(kCrabSmallU, answer.u);
    // The small crab sits above the reading label (top 76) and below the ring (radius 216 around 233).
    const CrabRect box = crabBounds(answer);
    TEST_ASSERT_TRUE(box.y + box.h <= 76);
    TEST_ASSERT_TRUE(box.y >= 233 - 216 + 6);
}

void test_listening_claws_spring_up_after_a_crouch()
{
    const CrabPose crouch = crabPose(CrabAct::Listening, 40, 40);
    TEST_ASSERT_TRUE(crouch.lift < 0);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, crouch.claw_l);
    float peak = 0;
    for (uint32_t t = 90; t < 600; t += 10) peak = std::fmax(peak, crabPose(CrabAct::Listening, t, t).claw_l);
    TEST_ASSERT_TRUE(peak > 1.05F);  // overshoot
    TEST_ASSERT_FLOAT_WITHIN(0.03F, 1.0F, crabPose(CrabAct::Listening, 1500, 1500).claw_l);  // settles
}

void test_blink_is_brief_and_regular()
{
    int closed = 0;
    for (uint32_t now = 0; now < 12900; now += 10) closed += crabBlink(now) < 0.5F ? 1 : 0;
    TEST_ASSERT_TRUE(closed >= 30 && closed <= 60);  // four blinks of ~120 ms per 12.9 s
    TEST_ASSERT_EQUAL_FLOAT(1.0F, crabBlink(2000));
}

void test_rects_are_deterministic()
{
    std::vector<CrabRect> a;
    std::vector<CrabRect> b;
    crabRects(crabPose(CrabAct::Thinking, 777, 12345), a);
    crabRects(crabPose(CrabAct::Thinking, 777, 12345), b);
    TEST_ASSERT_EQUAL(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) TEST_ASSERT_EQUAL_MEMORY(&a[i], &b[i], sizeof(CrabRect));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_every_pixel_stays_inside_the_cleared_box);
    RUN_TEST(test_eyes_stay_on_the_body);
    RUN_TEST(test_big_and_small_sizes);
    RUN_TEST(test_listening_claws_spring_up_after_a_crouch);
    RUN_TEST(test_blink_is_brief_and_regular);
    RUN_TEST(test_rects_are_deterministic);
    return UNITY_END();
}
