// Native (off-device) unit tests for src/stackchan/audio_packet.h.
// Run with:  pio test -e native
//
// The audio-relay wire format (Path A2: Mac -> relay -> UDP -> StackChan)
// prefixes each PCM datagram with a 4-byte header: magic 0xA5 0xC3 + a
// uint16-LE sequence. The build/parse helpers are pure logic — no Arduino,
// no WiFi — so they live in a header and are host-testable. The Python relay
// must emit a byte-identical header (see tools/audio-relay/relay.py).

#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "audio_packet.h"

void setUp() {}
void tearDown() {}

// ─── audioWriteHeader ─────────────────────────────────────────────────

void test_writeHeader_magic_bytes() {
    uint8_t buf[4] = {0};
    audioWriteHeader(0, buf);
    TEST_ASSERT_EQUAL_UINT8(0xA5, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xC3, buf[1]);
}

void test_writeHeader_returns_len() {
    uint8_t buf[4] = {0};
    TEST_ASSERT_EQUAL_UINT32((uint32_t)AUDIO_HEADER_LEN,
                             (uint32_t)audioWriteHeader(0, buf));
}

void test_writeHeader_seq_little_endian() {
    uint8_t buf[4] = {0};
    audioWriteHeader(0x1234, buf);
    TEST_ASSERT_EQUAL_UINT8(0x34, buf[2]);  // low byte first
    TEST_ASSERT_EQUAL_UINT8(0x12, buf[3]);  // high byte
}

void test_writeHeader_seq_max() {
    uint8_t buf[4] = {0};
    audioWriteHeader(0xFFFF, buf);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[3]);
}

// ─── audioParseHeader ─────────────────────────────────────────────────

void test_parse_roundtrip() {
    uint8_t buf[8] = {0};
    audioWriteHeader(0xBEEF, buf);
    uint16_t seq = 0;
    TEST_ASSERT_TRUE(audioParseHeader(buf, sizeof(buf), &seq));
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, seq);
}

void test_parse_zero_seq() {
    uint8_t buf[4] = {0};
    audioWriteHeader(0, buf);
    uint16_t seq = 0xAAAA;
    TEST_ASSERT_TRUE(audioParseHeader(buf, 4, &seq));
    TEST_ASSERT_EQUAL_UINT16(0, seq);
}

void test_parse_rejects_bad_magic() {
    uint8_t buf[4] = {0xA5, 0x00, 0x01, 0x00};  // second magic byte wrong
    uint16_t seq = 0;
    TEST_ASSERT_FALSE(audioParseHeader(buf, 4, &seq));
}

void test_parse_rejects_short() {
    uint8_t buf[3] = {0xA5, 0xC3, 0x00};
    uint16_t seq = 0;
    TEST_ASSERT_FALSE(audioParseHeader(buf, 3, &seq));
}

void test_parse_rejects_null() {
    uint16_t seq = 0;
    TEST_ASSERT_FALSE(audioParseHeader(nullptr, 4, &seq));
}

void test_parse_null_seq_out_ok() {
    uint8_t buf[4] = {0};
    audioWriteHeader(7, buf);
    // Passing nullptr for seq_out must still validate the header.
    TEST_ASSERT_TRUE(audioParseHeader(buf, 4, nullptr));
}

// ─── entry point ──────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_writeHeader_magic_bytes);
    RUN_TEST(test_writeHeader_returns_len);
    RUN_TEST(test_writeHeader_seq_little_endian);
    RUN_TEST(test_writeHeader_seq_max);
    RUN_TEST(test_parse_roundtrip);
    RUN_TEST(test_parse_zero_seq);
    RUN_TEST(test_parse_rejects_bad_magic);
    RUN_TEST(test_parse_rejects_short);
    RUN_TEST(test_parse_rejects_null);
    RUN_TEST(test_parse_null_seq_out_ok);
    return UNITY_END();
}
