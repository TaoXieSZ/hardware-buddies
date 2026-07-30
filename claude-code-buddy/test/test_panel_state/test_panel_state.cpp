// Native (off-device) unit tests for src/stackchan_voice/panel_state.h.
// Run with:  pio test -e native
//
// panel_state.h carries the two cross-task structures behind the 小咪 control
// panel: Pending (HTTP task stages a settings change, main loop applies it once)
// and Snapshot (main loop publishes state, HTTP task reads it). Both are pure
// logic — no Arduino — so the clamping rules and the take-once semantics are
// host-testable. Getting take-once wrong would re-apply the same settings write
// every loop tick, so it is worth pinning down here rather than on the device.

#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "panel_state.h"

void setUp() {}
void tearDown() {}

// --- 夹取规则：面板提交越界值时夹到边界，而不是拒绝或写坏 ---

void test_clamp_idle_sec_bounds() {
    TEST_ASSERT_EQUAL_UINT16(30, panel::clampIdleSec(0));
    TEST_ASSERT_EQUAL_UINT16(30, panel::clampIdleSec(29));
    TEST_ASSERT_EQUAL_UINT16(300, panel::clampIdleSec(300));
    TEST_ASSERT_EQUAL_UINT16(3600, panel::clampIdleSec(99999));
}

void test_clamp_turn_limit_bounds() {
    TEST_ASSERT_EQUAL_UINT8(1, panel::clampTurnLimit(0));
    TEST_ASSERT_EQUAL_UINT8(20, panel::clampTurnLimit(20));
    TEST_ASSERT_EQUAL_UINT8(100, panel::clampTurnLimit(999));
}

void test_clamp_volume_floor_is_audible() {
    // 音量下限 96：低于此值语音回放听不清（真机实测）。
    TEST_ASSERT_EQUAL_UINT8(96, panel::clampVolume(0));
    TEST_ASSERT_EQUAL_UINT8(96, panel::clampVolume(95));
    TEST_ASSERT_EQUAL_UINT8(160, panel::clampVolume(160));
    TEST_ASSERT_EQUAL_UINT8(255, panel::clampVolume(9999));
}

void test_clamp_brightness_and_tilt_bounds() {
    TEST_ASSERT_EQUAL_UINT8(20, panel::clampBrightness(0));
    TEST_ASSERT_EQUAL_UINT8(255, panel::clampBrightness(300));
    TEST_ASSERT_EQUAL_UINT8(0, panel::clampTilt(-5));
    TEST_ASSERT_EQUAL_UINT8(90, panel::clampTilt(120));
    TEST_ASSERT_EQUAL_UINT8(65, panel::clampTilt(65));
}

// --- Pending：部分更新 + 取走即清空 ---

void test_pending_starts_empty() {
    panel::Pending p;
    panel::Pending out;
    TEST_ASSERT_FALSE(p.any);
    TEST_ASSERT_FALSE(p.take(&out));   // 空的时候 take 必须返回 false
}

void test_pending_partial_update_only_marks_touched_fields() {
    panel::Pending p;
    p.setVolume(200);
    TEST_ASSERT_TRUE(p.any);
    TEST_ASSERT_TRUE(p.has_volume);
    TEST_ASSERT_EQUAL_UINT8(200, p.volume);
    // 没碰的字段不能被标记 —— 否则主循环会把默认值当成用户意图写进 NVS
    TEST_ASSERT_FALSE(p.has_brightness);
    TEST_ASSERT_FALSE(p.has_persona);
    TEST_ASSERT_FALSE(p.has_motion);
}

void test_pending_take_transfers_and_clears() {
    panel::Pending p;
    p.setVolume(120);
    p.setTilt(45);
    p.setDance(true);

    panel::Pending out;
    TEST_ASSERT_TRUE(p.take(&out));
    TEST_ASSERT_TRUE(out.has_volume);
    TEST_ASSERT_EQUAL_UINT8(120, out.volume);
    TEST_ASSERT_TRUE(out.has_tilt);
    TEST_ASSERT_EQUAL_UINT8(45, out.tilt);
    TEST_ASSERT_TRUE(out.has_dance);
    TEST_ASSERT_TRUE(out.dance);

    // 取走后源必须清空：同一批改动只能被应用一次
    TEST_ASSERT_FALSE(p.any);
    TEST_ASSERT_FALSE(p.has_volume);
    panel::Pending again;
    TEST_ASSERT_FALSE(p.take(&again));
}

void test_pending_clamps_on_set() {
    panel::Pending p;
    p.setIdleSec(5);        // 低于下限
    p.setTurnLimit(999);    // 高于上限
    TEST_ASSERT_EQUAL_UINT16(30, p.idle_sec);
    TEST_ASSERT_EQUAL_UINT8(100, p.turn_limit);
}

void test_pending_strings_truncate_not_overflow() {
    panel::Pending p;
    char big[panel::PERSONA_CAP + 200];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;

    p.setPersona(big);
    TEST_ASSERT_TRUE(p.has_persona);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(panel::PERSONA_CAP - 1), (uint32_t)strlen(p.persona));

    p.setVoice("longpaopao_v3.6");
    TEST_ASSERT_EQUAL_STRING("longpaopao_v3.6", p.voice);
}

void test_pending_action_is_take_once() {
    // 动作必须严格执行一次：重复执行会让小咪莫名其妙多跳一段舞或二次断连。
    panel::Pending p;
    p.setAction("dance");
    panel::Pending out;
    TEST_ASSERT_TRUE(p.take(&out));
    TEST_ASSERT_TRUE(out.has_action);
    TEST_ASSERT_EQUAL_STRING("dance", out.action);

    panel::Pending again;
    TEST_ASSERT_FALSE(p.take(&again));
    TEST_ASSERT_FALSE(p.has_action);
}

void test_pending_null_string_is_safe() {
    panel::Pending p;
    p.setVoice(nullptr);
    p.setPersona(nullptr);
    TEST_ASSERT_TRUE(p.has_voice);
    TEST_ASSERT_EQUAL_STRING("", p.voice);
    TEST_ASSERT_EQUAL_STRING("", p.persona);
}

// --- Snapshot：文本写入不溢出 ---

void test_snapshot_text_truncates() {
    panel::Snapshot s;
    char big[panel::SUBTITLE_CAP + 100];
    memset(big, 'y', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;

    s.setSubtitle(big);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(panel::SUBTITLE_CAP - 1), (uint32_t)strlen(s.subtitle));

    s.setReplyText("喵～");
    TEST_ASSERT_EQUAL_STRING("喵～", s.last_reply_text);
    s.setUserText(nullptr);
    TEST_ASSERT_EQUAL_STRING("", s.last_user_text);
}

void test_snapshot_defaults_are_sane() {
    panel::Snapshot s;
    TEST_ASSERT_EQUAL_INT8(-1, s.battery_pct);   // -1 = 未知，不是 0%
    TEST_ASSERT_FALSE(s.wifi_up);
    TEST_ASSERT_FALSE(s.ds_connected);
    TEST_ASSERT_EQUAL_STRING("", s.subtitle);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_clamp_idle_sec_bounds);
    RUN_TEST(test_clamp_turn_limit_bounds);
    RUN_TEST(test_clamp_volume_floor_is_audible);
    RUN_TEST(test_clamp_brightness_and_tilt_bounds);
    RUN_TEST(test_pending_starts_empty);
    RUN_TEST(test_pending_partial_update_only_marks_touched_fields);
    RUN_TEST(test_pending_take_transfers_and_clears);
    RUN_TEST(test_pending_clamps_on_set);
    RUN_TEST(test_pending_strings_truncate_not_overflow);
    RUN_TEST(test_pending_action_is_take_once);
    RUN_TEST(test_pending_null_string_is_safe);
    RUN_TEST(test_snapshot_text_truncates);
    RUN_TEST(test_snapshot_defaults_are_sane);
    return UNITY_END();
}
