// Pure-logic packet header for the StackChan audio-relay wire format
// (Path A2: Mac browser -> relay -> UDP -> StackChan speaker).
//
// Each UDP datagram is: <4-byte header><PCM payload>, where the header is
//   byte0 = 0xA5 (magic hi)
//   byte1 = 0xC3 (magic lo)
//   byte2..3 = uint16 little-endian sequence number (wraps at 65536)
// Payload is signed-16-bit-LE mono PCM at 16000 Hz, <= AUDIO_MAX_PAYLOAD bytes.
//
// The build/parse helpers live in this header so the native test env can
// compile them without Arduino / WiFi. The actual UDP socket + speaker
// playback live in audio_play.cpp. The relay side (tools/audio-relay) builds
// the identical header in Python — keep the two in lockstep.

#pragma once

#include <stddef.h>
#include <stdint.h>

static const uint8_t AUDIO_MAGIC0 = 0xA5;
static const uint8_t AUDIO_MAGIC1 = 0xC3;        // audio (PCM) datagram
// Subtitle/caption datagram: [0xA5, 0xC4] + UTF-8 text (no seq). Path A2
// sends agent/user transcript text on the same UDP port for the on-device
// scrolling ticker. Kept distinct from audio so one socket handles both.
static const uint8_t TEXT_MAGIC1  = 0xC4;
static const size_t  TEXT_HEADER_LEN = 2;
static const size_t  AUDIO_HEADER_LEN = 4;
// 320 samples * 2 bytes = 640 bytes = 20 ms @ 16 kHz mono. Keeps the whole
// datagram (644 bytes) well under a 1500-byte MTU so it never fragments.
static const size_t  AUDIO_MAX_PAYLOAD = 640;

// Write the 4-byte header for `seq` into out[0..3]. Returns AUDIO_HEADER_LEN.
inline size_t audioWriteHeader(uint16_t seq, uint8_t out[4]) {
    out[0] = AUDIO_MAGIC0;
    out[1] = AUDIO_MAGIC1;
    out[2] = static_cast<uint8_t>(seq & 0xFFu);         // LE low byte
    out[3] = static_cast<uint8_t>((seq >> 8) & 0xFFu);  // LE high byte
    return AUDIO_HEADER_LEN;
}

// Validate the header at buf[0..] and extract the sequence number.
// Returns true and sets *seq_out only when len >= 4 and the magic matches.
// A datagram that fails this check is dropped by the receiver (corrupt or
// from an unrelated sender on the port).
inline bool audioParseHeader(const uint8_t* buf, size_t len, uint16_t* seq_out) {
    if (buf == nullptr || len < AUDIO_HEADER_LEN) return false;
    if (buf[0] != AUDIO_MAGIC0 || buf[1] != AUDIO_MAGIC1) return false;
    if (seq_out != nullptr) {
        *seq_out = static_cast<uint16_t>(buf[2]) |
                   (static_cast<uint16_t>(buf[3]) << 8);
    }
    return true;
}
