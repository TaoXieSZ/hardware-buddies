#include "audio_io.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>

namespace {

// --- 采集（16k mono s16le；10s 上限 = 320 KB PSRAM） ---
constexpr uint32_t MIC_RATE = 16000;
constexpr size_t   MIC_CHUNK_SAMPLES = 320;   // 20 ms/chunk
constexpr size_t   VOICE_REC_CAP = MIC_RATE * 2 * 10;
uint8_t* s_rec = nullptr;
size_t   s_rec_bytes = 0;

// --- 播放（线性 PSRAM 缓冲，~30s @ 24k；见 audio_io.h 顶部注释） ---
constexpr size_t VOICE_PLAY_CAP = 24000 * 2 * 30;
uint8_t* s_play = nullptr;
size_t   s_play_w = 0;      // 已写入
size_t   s_play_r = 0;      // 已播出
uint32_t s_play_rate = 24000;
uint32_t s_last_feed_ms = 0;

// 2 槽 playRaw 队列 + 4 缓冲池，照抄 src/stackchan/audio_play.cpp（M5.Speaker
// DMA 期间读源指针不拷贝，4 池深保证在飞的 2 槽永远用不到正被复用的缓冲）。
constexpr int    AUDIO_CH = 0;
constexpr size_t CHUNK_SAMPLES = 512;
constexpr size_t CHUNK_BYTES = CHUNK_SAMPLES * 2;
constexpr size_t POOL = 4;
int16_t s_pool[POOL][CHUNK_SAMPLES];
size_t  s_pool_idx = 0;
constexpr uint32_t HANGOVER_MS = 250;

}  // namespace

bool audioIoInit() {
  s_rec = (uint8_t*)heap_caps_malloc(VOICE_REC_CAP, MALLOC_CAP_SPIRAM);
  s_play = (uint8_t*)heap_caps_malloc(VOICE_PLAY_CAP, MALLOC_CAP_SPIRAM);
  if (!s_rec || !s_play) {
    M5_LOGE("audio_io: PSRAM alloc failed (rec=%p play=%p)", s_rec, s_play);
    return false;
  }
  return true;
}

// --- 半双工切换（Microphone.ino 序列 verbatim） ---

bool audioMicStart() {
  M5.Speaker.end();
  M5.Mic.begin();
  s_rec_bytes = 0;
  return M5.Mic.isEnabled();
}

void audioSpkStart() {
  while (M5.Mic.isRecording()) { M5.delay(1); }
  M5.Mic.end();
  M5.Speaker.begin();
}

// --- PTT 采集 ---

size_t audioMicPump() {
  if (!M5.Mic.isEnabled() || !s_rec) return s_rec_bytes;
  // 每 tick 只录一块（Microphone.ino 同款节奏）。record() 在队列满时会阻塞
  // 等槽位（~20ms/块），贪心循环会把 loop() 冻住直到缓冲录满——实测就是
  // 这样把"松手检测"饿死的（2026-07-18 回环调试）。一块/tick 时 loop 以
  // ~20ms 节拍走，触摸释放最迟一块内被看到。
  if (s_rec_bytes + MIC_CHUNK_SAMPLES * 2 <= VOICE_REC_CAP) {
    auto* dst = (int16_t*)(s_rec + s_rec_bytes);
    if (M5.Mic.record(dst, MIC_CHUNK_SAMPLES, MIC_RATE)) {
      s_rec_bytes += MIC_CHUNK_SAMPLES * 2;
    }
  }
  return s_rec_bytes;
}

const uint8_t* audioMicData() { return s_rec; }
size_t audioMicBytes() { return s_rec_bytes; }

// --- 流式播放 ---

void voicePlayStart(uint32_t sample_rate) {
  s_play_rate = sample_rate;
  s_play_w = s_play_r = 0;
  s_last_feed_ms = millis();
}

size_t voicePlayFeed(const uint8_t* pcm, size_t len) {
  if (!s_play || !pcm) return 0;
  size_t room = VOICE_PLAY_CAP - s_play_w;
  size_t n = (len < room) ? len : room;   // 满则截尾（不覆盖未播数据）
  memcpy(s_play + s_play_w, pcm, n);
  s_play_w += n;
  s_last_feed_ms = millis();
  return n;
}

void voicePlayPump() {
  if (!M5.Speaker.isEnabled()) return;
  while (M5.Speaker.isPlaying(AUDIO_CH) < 2 && s_play_w - s_play_r >= CHUNK_BYTES) {
    int16_t* dst = s_pool[s_pool_idx];
    memcpy(dst, s_play + s_play_r, CHUNK_BYTES);
    s_play_r += CHUNK_BYTES;
    s_pool_idx = (s_pool_idx + 1) % POOL;
    M5.Speaker.playRaw(dst, CHUNK_SAMPLES, s_play_rate, false, 1, AUDIO_CH, false);
  }
  // 收尾：不足整 chunk 的尾巴在数据流停止 HANGOVER 后一次性放掉。
  size_t tail = s_play_w - s_play_r;
  if (tail > 0 && tail < CHUNK_BYTES &&
      millis() - s_last_feed_ms > HANGOVER_MS &&
      M5.Speaker.isPlaying(AUDIO_CH) < 2) {
    int16_t* dst = s_pool[s_pool_idx];
    memcpy(dst, s_play + s_play_r, tail);
    s_play_r += tail;
    s_pool_idx = (s_pool_idx + 1) % POOL;
    M5.Speaker.playRaw(dst, tail / 2, s_play_rate, false, 1, AUDIO_CH, false);
  }
}

bool voicePlayActive() {
  return M5.Speaker.isPlaying(AUDIO_CH) > 0 || (s_play_w - s_play_r) >= CHUNK_BYTES;
}
