#include "audio_loop.h"
#include "muse_text.h"

#include <cstring>
#include <unity.h>

using namespace stopwatch;

void test_body_text_collapses_whitespace_and_bounds_length()
{
    TEST_ASSERT_EQUAL_STRING("a b", museBodyText("a\n\n b").c_str());
    const std::string answer = "明天上午九点有组会，十点前要交周报，下午三点体检。";
    TEST_ASSERT_EQUAL_STRING(answer.c_str(), museBodyText(answer).c_str());  // whole answer, no 18-char cut
    const std::string clipped = museBodyText(std::string(300, 'x'));
    TEST_ASSERT_EQUAL(kMuseBodyChars, utf8Length(clipped));
    TEST_ASSERT_TRUE(clipped.compare(clipped.size() - 3, 3, "…") == 0);
}

void test_wav_header_describes_16k_mono_pcm16()
{
    uint8_t h[44];
    museWavHeader(h, 32000);
    TEST_ASSERT_EQUAL_MEMORY("RIFF", h, 4);
    TEST_ASSERT_EQUAL_MEMORY("WAVEfmt ", h + 8, 8);
    TEST_ASSERT_EQUAL_MEMORY("data", h + 36, 4);
    TEST_ASSERT_EQUAL_UINT32(36 + 32000, h[4] | h[5] << 8 | h[6] << 16 | static_cast<uint32_t>(h[7]) << 24);
    TEST_ASSERT_EQUAL_UINT32(16000, h[24] | h[25] << 8 | h[26] << 16 | static_cast<uint32_t>(h[27]) << 24);
    TEST_ASSERT_EQUAL_UINT8(1, h[22]);   // mono
    TEST_ASSERT_EQUAL_UINT8(16, h[34]);  // bits
    TEST_ASSERT_EQUAL_UINT32(32000, h[40] | h[41] << 8 | h[42] << 16 | static_cast<uint32_t>(h[43]) << 24);
}

void test_request_id_is_a_version_4_uuid()
{
    uint8_t random[16];
    std::memset(random, 0xFF, sizeof(random));
    const std::string id = museRequestId(random);
    TEST_ASSERT_EQUAL(36, id.size());
    TEST_ASSERT_EQUAL_CHAR('-', id[8]);
    TEST_ASSERT_EQUAL_CHAR('4', id[14]);
    TEST_ASSERT_EQUAL_CHAR('b', id[19]);  // variant 10xx
}

void test_failure_reasons_are_plain_and_stage_specific()
{
    TEST_ASSERT_EQUAL_STRING("连不上个人云", museFailureReason(MuseStage::Status, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("设备需要重新配对", museFailureReason(MuseStage::Chat, 401).c_str());
    TEST_ASSERT_EQUAL_STRING("今天的转写次数用完了", museFailureReason(MuseStage::Transcribe, 429).c_str());
    TEST_ASSERT_EQUAL_STRING("今天的提问次数用完了", museFailureReason(MuseStage::Chat, 429).c_str());
    TEST_ASSERT_EQUAL_STRING("转写服务暂不可用", museFailureReason(MuseStage::Transcribe, 503).c_str());
    TEST_ASSERT_EQUAL_STRING("个人云出错 500", museFailureReason(MuseStage::Chat, 500).c_str());
}

void test_short_press_is_cancelled_back_to_ready()
{
    // Direct mode discards a <0.5 s press by cancelling while still recording.
    AudioLoop loop;
    loop.onConnected();
    TEST_ASSERT_EQUAL(LoopAction::StartUtterance, loop.onKeyADown("utt-1"));
    TEST_ASSERT_EQUAL(LoopAction::CancelUtterance, loop.onKeyBCancel());
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
    TEST_ASSERT_FALSE(loop.active());
}

void test_empty_transcript_becomes_a_retryable_error()
{
    AudioLoop loop;
    loop.onConnected();
    loop.onKeyADown("utt-1");
    loop.onKeyAUp();
    TEST_ASSERT_TRUE(loop.onError("utt-1", kMuseEmptyTranscript, true));
    TEST_ASSERT_EQUAL(DeviceState::Error, loop.state());
    TEST_ASSERT_EQUAL(LoopAction::Acknowledge, loop.onKeyADown("utt-2"));
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
}

void test_heard_line_prefixes_and_clips_the_question()
{
    TEST_ASSERT_EQUAL_STRING("问：我的订阅", museHeardLine("我的订阅").c_str());
    // The whole question is kept; the screen clips the label to its width when drawing.
    const std::string q = "我想知道最近这一个月里面我都订阅了哪些服务以及它们分别多少钱";
    TEST_ASSERT_EQUAL_STRING(("问：" + q).c_str(), museHeardLine(q).c_str());
    TEST_ASSERT_EQUAL(kMuseHeardChars, utf8Length(museHeardLine(std::string(100, 'x'))));
}

void test_level_reports_peak_rms_and_clipping()
{
    int16_t silence[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(museLevel(silence, 4).rms_dbfs < -100.0F);
    int16_t square[4] = {16384, -16384, 16384, -16384};  // half scale
    const MuseLevel half = museLevel(square, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, -6.02F, half.peak_dbfs);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, -6.02F, half.rms_dbfs);
    TEST_ASSERT_EQUAL_UINT32(0, half.clipped);
    int16_t loud[3] = {32767, -32768, 100};
    TEST_ASSERT_EQUAL_UINT32(2, museLevel(loud, 3).clipped);
}

void test_confirm_step_returns_to_thinking_then_answers()
{
    AudioLoop loop;
    loop.onConnected();
    loop.onKeyADown("utt-1");
    loop.onKeyAUp();
    TEST_ASSERT_TRUE(loop.onTranscript("utt-1", "问：我的订阅"));  // heard text
    TEST_ASSERT_TRUE(loop.onProposal("utt-1", "问：我的订阅"));    // confirm step
    TEST_ASSERT_EQUAL(DeviceState::ProposalReview, loop.state());
    TEST_ASSERT_FALSE(loop.onMuseConfirmed("other"));
    TEST_ASSERT_TRUE(loop.onMuseConfirmed("utt-1"));
    TEST_ASSERT_EQUAL(DeviceState::Transcribing, loop.state());
    TEST_ASSERT_TRUE(loop.onTranscript("utt-1", "问：我的订阅\n资料里有两个订阅。"));
    TEST_ASSERT_EQUAL(DeviceState::Result, loop.state());
}

void test_cancel_in_confirm_reaches_ready_without_dispatch()
{
    AudioLoop loop;
    loop.onConnected();
    loop.onKeyADown("utt-1");
    loop.onKeyAUp();
    loop.onTranscript("utt-1", "问：我的订阅");
    loop.onProposal("utt-1", "问：我的订阅");
    TEST_ASSERT_EQUAL(LoopAction::RejectProposal, loop.onKeyBCancel());
    TEST_ASSERT_EQUAL(LoopAction::Acknowledge, loop.onKeyBCancel());
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
    TEST_ASSERT_FALSE(loop.onMuseConfirmed("utt-1"));
}

void test_hold_on_result_starts_next_question()
{
    // Direct mode dismisses the result and immediately starts recording on the same press.
    AudioLoop loop;
    loop.onConnected();
    loop.onKeyADown("utt-1");
    loop.onKeyAUp();
    loop.onTranscript("utt-1", "问：x\n回答");
    TEST_ASSERT_EQUAL(LoopAction::Acknowledge, loop.onKeyADown("utt-2"));
    TEST_ASSERT_EQUAL(LoopAction::StartUtterance, loop.onKeyADown("utt-2"));
    TEST_ASSERT_EQUAL(DeviceState::Recording, loop.state());
}

void test_reminder_card_from_home()
{
    AudioLoop loop;
    TEST_ASSERT_FALSE(loop.onMuseReminder("提醒：x\ny"));  // not connected
    loop.onConnected();
    TEST_ASSERT_TRUE(loop.onMuseReminder(museBodyText("周五前回复 PlayStation 退款邮件")));
    TEST_ASSERT_EQUAL(DeviceState::Result, loop.state());
    TEST_ASSERT_EQUAL_STRING("周五前回复 PlayStation 退款邮件", loop.resultText().c_str());
    // The shared state machine ignores KEYB on a result; direct mode maps KEYB "back" to the acknowledge path.
    TEST_ASSERT_EQUAL(LoopAction::None, loop.onKeyBCancel());
    TEST_ASSERT_EQUAL(LoopAction::Acknowledge, loop.onKeyADown(""));
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
}

void test_abandon_while_thinking_ignores_the_late_reply()
{
    AudioLoop loop;
    loop.onConnected();
    loop.onKeyADown("utt-1");
    loop.onKeyAUp();
    TEST_ASSERT_EQUAL(DeviceState::Transcribing, loop.state());
    TEST_ASSERT_TRUE(loop.onMuseAbandon());
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
    TEST_ASSERT_FALSE(loop.onTranscript("utt-1", "late answer"));
    TEST_ASSERT_FALSE(loop.onError("utt-1", "late error", true));
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
    TEST_ASSERT_FALSE(loop.onMuseAbandon());  // only while thinking
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_body_text_collapses_whitespace_and_bounds_length);
    RUN_TEST(test_wav_header_describes_16k_mono_pcm16);
    RUN_TEST(test_request_id_is_a_version_4_uuid);
    RUN_TEST(test_failure_reasons_are_plain_and_stage_specific);
    RUN_TEST(test_short_press_is_cancelled_back_to_ready);
    RUN_TEST(test_empty_transcript_becomes_a_retryable_error);
    RUN_TEST(test_heard_line_prefixes_and_clips_the_question);
    RUN_TEST(test_level_reports_peak_rms_and_clipping);
    RUN_TEST(test_confirm_step_returns_to_thinking_then_answers);
    RUN_TEST(test_cancel_in_confirm_reaches_ready_without_dispatch);
    RUN_TEST(test_hold_on_result_starts_next_question);
    RUN_TEST(test_reminder_card_from_home);
    RUN_TEST(test_abandon_while_thinking_ignores_the_late_reply);
    return UNITY_END();
}
