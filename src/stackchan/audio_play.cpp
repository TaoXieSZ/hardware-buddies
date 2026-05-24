// Streamed PCM playback over UDP — see audio_play.h.

#include "audio_play.h"

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "audio_packet.h"
#include "audio_ringbuf.h"
#include "character_chan.h"
#include "wifi_stream.h"

#ifndef STACKCHAN_AUDIO_PORT
#define STACKCHAN_AUDIO_PORT 5005
#endif

namespace {

// 16 kHz * 2 bytes = 32000 B/s, so 32 KiB ~= 1 s of jitter headroom. Lives in
// .bss; CoreS3 has PSRAM and this is small enough for internal RAM.
AudioRingBuffer<32768> s_buf;

WiFiUDP s_udp;
bool s_open = false;
uint32_t s_last_audio_ms = 0;

// Speaker feed: one dedicated channel, double-buffered by M5.Speaker (each
// channel holds 2 wav slots). M5.Speaker reads the source pointer during DMA
// mixing — it does NOT copy — so each in-flight chunk needs its own live
// buffer. A 4-buffer pool with a 2-deep queue guarantees a buffer is never
// reused while still playing (it's 4 enqueues old, only 2 can be active).
constexpr int      AUDIO_CH = 0;
constexpr size_t   CHUNK_SAMPLES = 512;            // 1024 B = 32 ms @ 16 kHz
constexpr size_t   CHUNK_BYTES = CHUNK_SAMPLES * 2;
constexpr size_t   POOL = 4;
int16_t  s_pool[POOL][CHUNK_SAMPLES];
size_t   s_pool_idx = 0;

constexpr uint32_t HANGOVER_MS = 250;

// Datagram scratch: header (4) + max payload (640).
uint8_t s_pkt[AUDIO_HEADER_LEN + AUDIO_MAX_PAYLOAD];

void drainUdp() {
    int sz;
    while ((sz = s_udp.parsePacket()) > 0) {
        int n = s_udp.read(s_pkt, sizeof(s_pkt));
        if (n < (int)TEXT_HEADER_LEN || s_pkt[0] != AUDIO_MAGIC0) continue;

        if (s_pkt[1] == AUDIO_MAGIC1 && n >= (int)AUDIO_HEADER_LEN) {
            // PCM datagram → jitter buffer.
            s_buf.push(s_pkt + AUDIO_HEADER_LEN, n - AUDIO_HEADER_LEN);
        } else if (s_pkt[1] == TEXT_MAGIC1) {
            // Subtitle datagram → on-device scrolling ticker (UTF-8).
            char txt[256];
            int len = n - (int)TEXT_HEADER_LEN;
            if (len < 0) len = 0;
            if (len > (int)sizeof(txt) - 1) len = sizeof(txt) - 1;
            memcpy(txt, s_pkt + TEXT_HEADER_LEN, len);
            txt[len] = 0;
            characterSetSubtitle(txt);
        }
    }
}

void feedSpeaker() {
    // Keep the 2-slot queue full while we have whole chunks buffered.
    while (M5.Speaker.isPlaying(AUDIO_CH) < 2 && s_buf.available() >= CHUNK_BYTES) {
        int16_t* dst = s_pool[s_pool_idx];
        s_buf.pop(reinterpret_cast<uint8_t*>(dst), CHUNK_BYTES);
        s_pool_idx = (s_pool_idx + 1) % POOL;
        // playRaw(int16 samples, count, rate, stereo, repeat, channel, stop)
        M5.Speaker.playRaw(dst, CHUNK_SAMPLES, 16000, false, 1, AUDIO_CH, false);
        s_last_audio_ms = millis();
    }
}

}  // namespace

bool audioPlayInit() {
    if (s_open) return true;
    if (!wifiStreamCredsAvailable()) {
        M5_LOGW("audioPlay: wifi_secrets.ini placeholders unset, audio disabled");
        return false;
    }
    if (!wifiEnsureUp()) {
        M5_LOGW("audioPlay: WiFi not up, audio disabled (will not retry this boot)");
        return false;
    }
    if (!s_udp.begin(STACKCHAN_AUDIO_PORT)) {
        M5_LOGW("audioPlay: UDP begin(%d) failed", (int)STACKCHAN_AUDIO_PORT);
        return false;
    }
    s_open = true;
    M5_LOGI("audioPlay: listening for PCM on udp/%d", (int)STACKCHAN_AUDIO_PORT);
    return true;
}

void audioPlayPump() {
    if (!s_open) return;
    drainUdp();
    feedSpeaker();
}

bool audioPlayIsActive() {
    return s_open && (millis() - s_last_audio_ms) < HANGOVER_MS;
}
