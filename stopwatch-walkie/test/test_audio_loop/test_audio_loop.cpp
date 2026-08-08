#include "audio_loop.h"
#include "audio_queue.h"
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
    return UNITY_END();
}
