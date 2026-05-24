// Native (off-device) unit tests for src/stackchan/audio_ringbuf.h.
// Run with:  pio test -e native
//
// AudioRingBuffer<CAP> is the device-side jitter buffer for streamed PCM
// (Path A2 playback). It is pure logic — no Arduino — so it is host-testable.
// Overflow semantics are OVERWRITE-OLDEST so the UDP receiver never blocks.
// Tests use a tiny CAP=8 to make wraparound and overflow easy to assert.

#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "audio_ringbuf.h"

void setUp() {}
void tearDown() {}

static uint32_t u(size_t v) { return (uint32_t)v; }

void test_empty_initial_state() {
    AudioRingBuffer<8> rb;
    TEST_ASSERT_EQUAL_UINT32(0, u(rb.available()));
    TEST_ASSERT_EQUAL_UINT32(8, u(rb.capacity()));
    TEST_ASSERT_EQUAL_UINT32(8, u(rb.freeSpace()));
    TEST_ASSERT_TRUE(rb.empty());
}

void test_push_pop_fifo() {
    AudioRingBuffer<8> rb;
    uint8_t in[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_UINT32(4, u(rb.push(in, 4)));
    TEST_ASSERT_EQUAL_UINT32(4, u(rb.available()));
    TEST_ASSERT_EQUAL_UINT32(4, u(rb.freeSpace()));

    uint8_t out[4] = {0};
    TEST_ASSERT_EQUAL_UINT32(4, u(rb.pop(out, 4)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 4);
    TEST_ASSERT_TRUE(rb.empty());
}

void test_partial_pop_keeps_remainder() {
    AudioRingBuffer<8> rb;
    uint8_t in[5] = {10, 20, 30, 40, 50};
    rb.push(in, 5);

    uint8_t out2[2] = {0};
    TEST_ASSERT_EQUAL_UINT32(2, u(rb.pop(out2, 2)));
    TEST_ASSERT_EQUAL_UINT8(10, out2[0]);
    TEST_ASSERT_EQUAL_UINT8(20, out2[1]);
    TEST_ASSERT_EQUAL_UINT32(3, u(rb.available()));

    uint8_t out3[3] = {0};
    TEST_ASSERT_EQUAL_UINT32(3, u(rb.pop(out3, 3)));
    TEST_ASSERT_EQUAL_UINT8(30, out3[0]);
    TEST_ASSERT_EQUAL_UINT8(40, out3[1]);
    TEST_ASSERT_EQUAL_UINT8(50, out3[2]);
}

void test_wraparound() {
    AudioRingBuffer<8> rb;
    uint8_t a[6] = {1, 2, 3, 4, 5, 6};
    rb.push(a, 6);            // head=0, count=6
    uint8_t drain[4] = {0};
    rb.pop(drain, 4);         // head=4, count=2 (5,6 remain)

    uint8_t b[5] = {7, 8, 9, 10, 11};
    rb.push(b, 5);            // writes wrap across the physical end
    TEST_ASSERT_EQUAL_UINT32(7, u(rb.available()));

    uint8_t out[7] = {0};
    TEST_ASSERT_EQUAL_UINT32(7, u(rb.pop(out, 7)));
    uint8_t expect[7] = {5, 6, 7, 8, 9, 10, 11};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 7);
}

void test_overflow_drops_oldest() {
    AudioRingBuffer<8> rb;
    uint8_t full[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    rb.push(full, 8);                       // exactly full
    uint8_t more[4] = {9, 10, 11, 12};
    rb.push(more, 4);                       // oldest 4 (1..4) dropped
    TEST_ASSERT_EQUAL_UINT32(8, u(rb.available()));

    uint8_t out[8] = {0};
    rb.pop(out, 8);
    uint8_t expect[8] = {5, 6, 7, 8, 9, 10, 11, 12};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 8);
}

void test_push_larger_than_capacity_keeps_tail() {
    AudioRingBuffer<8> rb;
    uint8_t big[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    TEST_ASSERT_EQUAL_UINT32(8, u(rb.push(big, 12)));  // only last 8 kept
    TEST_ASSERT_EQUAL_UINT32(8, u(rb.available()));

    uint8_t out[8] = {0};
    rb.pop(out, 8);
    uint8_t expect[8] = {5, 6, 7, 8, 9, 10, 11, 12};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 8);
}

void test_clear_resets() {
    AudioRingBuffer<8> rb;
    uint8_t in[5] = {1, 2, 3, 4, 5};
    rb.push(in, 5);
    rb.clear();
    TEST_ASSERT_EQUAL_UINT32(0, u(rb.available()));
    TEST_ASSERT_TRUE(rb.empty());

    uint8_t out[4] = {0};
    TEST_ASSERT_EQUAL_UINT32(0, u(rb.pop(out, 4)));  // nothing to pop
}

void test_pop_more_than_available() {
    AudioRingBuffer<8> rb;
    uint8_t in[3] = {1, 2, 3};
    rb.push(in, 3);
    uint8_t out[8] = {0};
    TEST_ASSERT_EQUAL_UINT32(3, u(rb.pop(out, 8)));  // clamped to available
}

// ─── entry point ──────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_initial_state);
    RUN_TEST(test_push_pop_fifo);
    RUN_TEST(test_partial_pop_keeps_remainder);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_overflow_drops_oldest);
    RUN_TEST(test_push_larger_than_capacity_keeps_tail);
    RUN_TEST(test_clear_resets);
    RUN_TEST(test_pop_more_than_available);
    return UNITY_END();
}
