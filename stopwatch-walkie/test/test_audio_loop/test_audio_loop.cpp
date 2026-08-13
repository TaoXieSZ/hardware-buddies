#include "audio_loop.h"
#include "audio_queue.h"
#include "control_protocol.h"
#include "protocol.h"

#include <unity.h>

using namespace stopwatch;

void test_state_machine_happy_path()
{
    AudioLoop loop;
    TEST_ASSERT_EQUAL(DeviceState::Connecting, loop.state());
    TEST_ASSERT_EQUAL(LoopAction::None, loop.onConnected());
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
    TEST_ASSERT_EQUAL(LoopAction::StartUtterance, loop.onKeyADown("utt-1"));
    TEST_ASSERT_EQUAL(DeviceState::Recording, loop.state());
    TEST_ASSERT_EQUAL_STRING("utt-1", loop.activeId().c_str());
    TEST_ASSERT_EQUAL(LoopAction::EndUtterance, loop.onKeyAUp());
    TEST_ASSERT_EQUAL(DeviceState::Transcribing, loop.state());
    TEST_ASSERT_TRUE(loop.onTranscript("utt-1", "hello"));
    TEST_ASSERT_EQUAL(DeviceState::Result, loop.state());
    TEST_ASSERT_EQUAL_STRING("hello", loop.resultText().c_str());
}

void test_cancel_during_recording_discards_active_id()
{
    AudioLoop loop;
    loop.onConnected();
    TEST_ASSERT_EQUAL(LoopAction::StartUtterance, loop.onKeyADown("utt-2"));
    TEST_ASSERT_EQUAL(LoopAction::CancelUtterance, loop.onKeyBCancel());
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
    TEST_ASSERT_FALSE(loop.active());
}

void test_disconnected_press_does_not_start()
{
    AudioLoop loop;
    TEST_ASSERT_EQUAL(LoopAction::None, loop.onKeyADown("utt-3"));
    TEST_ASSERT_EQUAL(DeviceState::Error, loop.state());
    TEST_ASSERT_EQUAL_STRING("connection_unavailable", loop.errorCode().c_str());
}

void test_late_transcript_is_rejected()
{
    AudioLoop loop;
    loop.onConnected();
    loop.onKeyADown("utt-4");
    loop.onKeyAUp();
    TEST_ASSERT_FALSE(loop.onTranscript("utt-old", "stale"));
    TEST_ASSERT_EQUAL(DeviceState::Transcribing, loop.state());
    TEST_ASSERT_TRUE(loop.onTranscript("utt-4", ""));
    TEST_ASSERT_EQUAL(DeviceState::Result, loop.state());
    TEST_ASSERT_TRUE(loop.retryableError());
}

void test_late_error_is_rejected_when_no_active_utterance()
{
    AudioLoop loop;
    loop.onConnected();
    TEST_ASSERT_FALSE(loop.onError("utt-old", "asr_timeout", true));
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());

    loop.onKeyADown("utt-6");
    loop.onKeyAUp();
    TEST_ASSERT_FALSE(loop.onError("utt-old", "asr_timeout", true));
    TEST_ASSERT_EQUAL(DeviceState::Transcribing, loop.state());
    TEST_ASSERT_TRUE(loop.onError("utt-6", "asr_timeout", true));
    TEST_ASSERT_EQUAL(DeviceState::Error, loop.state());
}

void test_audio_queue_bounds_and_high_water()
{
    AudioQueue queue;
    int16_t samples[kPcmSamplesPerChunk]{};
    for (size_t i = 0; i < queue.capacity(); ++i) {
        TEST_ASSERT_TRUE(queue.push(samples, kPcmSamplesPerChunk));
    }
    TEST_ASSERT_EQUAL(queue.capacity(), queue.high_water());
    TEST_ASSERT_FALSE(queue.push(samples, kPcmSamplesPerChunk));
    TEST_ASSERT_TRUE(queue.overflowed());

    PcmChunk chunk;
    TEST_ASSERT_TRUE(queue.pop(chunk));
    TEST_ASSERT_EQUAL(kPcmSamplesPerChunk, chunk.sample_count);
    queue.clear();
    TEST_ASSERT_EQUAL_UINT32(0, queue.size());
    TEST_ASSERT_FALSE(queue.overflowed());
}

void test_protocol_messages_are_versioned_and_correlated()
{
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"hello\",\"protocol\":1,\"device_id\":\"dev\\\"1\"}",
                             helloMessage("dev\"1").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"utterance.start\",\"id\":\"utt-5\",\"audio\":{\"rate\":16000,\"bits\":16,\"channels\":1,"
        "\"encoding\":\"pcm_s16le\"}}",
        utteranceStartMessage("utt-5").c_str());
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"utterance.end\",\"id\":\"utt-5\"}", utteranceEndMessage("utt-5").c_str());
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"utterance.cancel\",\"id\":\"utt-5\"}",
                             utteranceCancelMessage("utt-5").c_str());
    TEST_ASSERT_EQUAL_STRING("dev-00000042", makeUtteranceId("dev", 42).c_str());
}

void test_protocol_v2_fixed_inputs_and_v1_fallback_remain_stable()
{
    TEST_ASSERT_EQUAL_UINT8(1, kProtocolV1);
    TEST_ASSERT_EQUAL_UINT8(2, kProtocolV2);
    TEST_ASSERT_EQUAL_UINT32(32, kControlSecretBytes);
    TEST_ASSERT_EQUAL_STRING(
        "walkie-v2\nd2b\nsession-test-001\n1\neyJ0eXBlIjoiY29tbWFuZC5kZWNpc2lvbiIsImNvbW1hbmRfaWQiOiJjbWQtMSIsImRlY2lzaW9uIjoiYXBwcm92ZSJ9",
        envelopeMacInput("d2b", "session-test-001", 1,
                         "eyJ0eXBlIjoiY29tbWFuZC5kZWNpc2lvbiIsImNvbW1hbmRfaWQiOiJjbWQtMSIsImRlY2lzaW9uIjoiYXBwcm92ZSJ9").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "walkie-v2-auth\nbridge\nwatch-test\ndGVzdC1kZXZpY2Utbm9uY2U\ndGVzdC1icmlkZ2Utbm9uY2U\nsession-test-001",
        authMacInput("bridge", "watch-test", "dGVzdC1kZXZpY2Utbm9uY2U",
                     "dGVzdC1icmlkZ2Utbm9uY2U", "session-test-001").c_str());
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"hello\",\"protocol\":1,\"device_id\":\"watch-test\"}",
                             helloMessage("watch-test").c_str());
}

void test_control_state_machine_buttons_are_state_specific()
{
    AudioLoop loop;
    loop.onConnected();
    loop.onKeyADown("utt-control");
    loop.onKeyAUp();
    TEST_ASSERT_TRUE(loop.onTranscript("utt-control", "codex run tests"));
    TEST_ASSERT_TRUE(loop.onProposal("cmd-1", "run tests"));
    TEST_ASSERT_EQUAL(DeviceState::ProposalReview, loop.state());
    TEST_ASSERT_EQUAL(LoopAction::ApproveProposal, loop.onKeyADown("ignored"));
    TEST_ASSERT_EQUAL(DeviceState::Dispatching, loop.state());
    TEST_ASSERT_TRUE(loop.onTaskAccepted("cmd-1", "task-1"));
    TEST_ASSERT_EQUAL(DeviceState::Running, loop.state());
    TEST_ASSERT_EQUAL(LoopAction::None, loop.onKeyBCancel());
    TEST_ASSERT_EQUAL(DeviceState::Running, loop.state());
    TEST_ASSERT_TRUE(loop.onPermission("task-1", "perm-1", "shell", true));
    TEST_ASSERT_EQUAL(LoopAction::DenyPermission, loop.onKeyBCancel());
    TEST_ASSERT_EQUAL(DeviceState::Running, loop.state());
    TEST_ASSERT_TRUE(loop.onTaskTerminal("task-1", DeviceState::Completed, "done"));
    TEST_ASSERT_EQUAL(LoopAction::Acknowledge, loop.onKeyADown("must-not-record"));
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
    TEST_ASSERT_EQUAL(LoopAction::StartUtterance, loop.onKeyADown("new-utterance"));
}

void test_reject_and_non_actionable_permission_never_approve()
{
    AudioLoop loop;
    loop.onConnected();
    loop.onKeyADown("u");
    loop.onKeyAUp();
    loop.onTranscript("u", "command");
    loop.onProposal("cmd", "command");
    TEST_ASSERT_EQUAL(LoopAction::RejectProposal, loop.onKeyBCancel());
    TEST_ASSERT_EQUAL(DeviceState::Cancelled, loop.state());
    loop.onKeyBCancel();
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());

    loop.onKeyADown("u2");
    loop.onKeyAUp();
    loop.onTranscript("u2", "command");
    loop.onProposal("cmd2", "command");
    loop.onKeyADown("ignored");
    loop.onTaskAccepted("cmd2", "task2");
    TEST_ASSERT_TRUE(loop.onPermission("task2", "perm2", "answer in terminal", false));
    TEST_ASSERT_EQUAL(LoopAction::None, loop.onKeyADown("must-not-record"));
    TEST_ASSERT_EQUAL(LoopAction::Acknowledge, loop.onKeyBCancel());
    TEST_ASSERT_EQUAL(DeviceState::Running, loop.state());
}

void test_late_control_events_and_reconnect_do_not_redispatch()
{
    AudioLoop loop;
    loop.onConnected();
    TEST_ASSERT_FALSE(loop.onTaskRunning("old"));
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
    loop.onDisconnected();
    TEST_ASSERT_EQUAL(DeviceState::Connecting, loop.state());
    TEST_ASSERT_EQUAL(LoopAction::None, loop.onConnected());
    TEST_ASSERT_EQUAL(DeviceState::Ready, loop.state());
}

void test_control_sequence_window_rejects_replay_and_out_of_order()
{
    SequenceWindow window;
    TEST_ASSERT_TRUE(window.accept(1));
    TEST_ASSERT_FALSE(window.accept(1));
    TEST_ASSERT_TRUE(window.accept(3));
    TEST_ASSERT_FALSE(window.accept(2));
    TEST_ASSERT_EQUAL_UINT64(3, window.last());
    window.reset();
    TEST_ASSERT_TRUE(window.accept(1));
}

void test_control_fields_are_bounded_before_storage()
{
    AudioLoop loop;
    loop.onConnected();
    loop.onKeyADown("u");
    loop.onKeyAUp();
    loop.onTranscript("u", "command");
    TEST_ASSERT_FALSE(loop.onProposal(std::string(65, 'c'), "preview"));
    TEST_ASSERT_FALSE(loop.onProposal("cmd", std::string(241, 'p')));
    TEST_ASSERT_TRUE(loop.onProposal("cmd", "preview"));
    loop.onKeyADown("ignored");
    TEST_ASSERT_FALSE(loop.onTaskAccepted("cmd", std::string(65, 't')));
    TEST_ASSERT_TRUE(loop.onTaskAccepted("cmd", "task"));
    TEST_ASSERT_FALSE(loop.onPermission("task", "permission", std::string(161, 'h'), true));
    TEST_ASSERT_FALSE(loop.onTaskTerminal("task", DeviceState::Completed, std::string(513, 's')));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_state_machine_happy_path);
    RUN_TEST(test_cancel_during_recording_discards_active_id);
    RUN_TEST(test_disconnected_press_does_not_start);
    RUN_TEST(test_late_transcript_is_rejected);
    RUN_TEST(test_late_error_is_rejected_when_no_active_utterance);
    RUN_TEST(test_audio_queue_bounds_and_high_water);
    RUN_TEST(test_protocol_messages_are_versioned_and_correlated);
    RUN_TEST(test_protocol_v2_fixed_inputs_and_v1_fallback_remain_stable);
    RUN_TEST(test_control_state_machine_buttons_are_state_specific);
    RUN_TEST(test_reject_and_non_actionable_permission_never_approve);
    RUN_TEST(test_late_control_events_and_reconnect_do_not_redispatch);
    RUN_TEST(test_control_sequence_window_rejects_replay_and_out_of_order);
    RUN_TEST(test_control_fields_are_bounded_before_storage);
    return UNITY_END();
}
