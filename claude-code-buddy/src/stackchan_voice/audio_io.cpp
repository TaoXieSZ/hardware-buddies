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
// 喂喇叭由独立 FreeRTOS 任务负责（10ms 周期）：主循环会被大 TLS 记录的
// 阻塞式读卡住 ~550ms（2026-07-18 underrun 账单实锤，当时缓冲躺着 4-8s
// 音频没人喂）。SPSC：主任务写 w，播放任务读 r，volatile + 发布屏障。
constexpr size_t VOICE_PLAY_CAP = 24000 * 2 * 30;
uint8_t* s_play = nullptr;
volatile size_t   s_play_w = 0;      // 已写入（仅主任务写）
volatile size_t   s_play_r = 0;      // 已播出（仅播放任务写）
volatile uint32_t s_play_rate = 24000;
volatile uint32_t s_last_feed_ms = 0;
volatile bool     s_primed = false;
volatile bool     s_eos = false;
// 卡顿量化：primed 后队列见底 = underrun（听感即一次咔哒）。记次数、
// 干涸总时长、每次发生在回复播放到第几秒——"微微卡"从此有数字。
volatile uint32_t s_underruns = 0;
volatile uint32_t s_dry_ms_total = 0;
uint32_t s_dry_since = 0;   // 0 = 当前未干涸（仅播放任务碰）

void playTask(void*) {
  // 只碰 voicePlayPump（r 侧）。10ms 周期 vs 85ms/块：即使连错过 7 拍
  // 队列都不会空；主循环卡死半秒也影响不到这里。
  for (;;) {
    voicePlayPump();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
// 起播预缓冲：0.5s 实测不够——设备侧 TLS+WS 供给在回复前几秒持续略低于
// 48KB/s 消耗，卡顿会一直吃穿小缓冲。抬到 1.5s，配合 EOS（response.done
// 后全量在本地，立即解锁起播）避免短回复干等。
constexpr size_t PRIME_BYTES = 72000;  // 1.5s @ 24k s16le

// 2 槽 playRaw 队列 + 4 缓冲池，照抄 src/stackchan/audio_play.cpp（M5.Speaker
// DMA 期间读源指针不拷贝，4 池深保证在飞的 2 槽永远用不到正被复用的缓冲）。
constexpr int    AUDIO_CH = 0;
// 2048 样本 = 85ms @24k，2 槽队列共 170ms 余量。512(21ms) 实测扛不住下载期
// 单个 25KB TLS 帧几十 ms 的主循环停顿 → 开头咔哒（2026-07-18 meter 定位：
// 供给 3x 实时充足，是队列深度问题）。
constexpr size_t CHUNK_SAMPLES = 2048;
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
  // 播放泵任务：core 0（主循环在 core 1）、优先级 3，避开主循环的一切停顿。
  xTaskCreatePinnedToCore(playTask, "voicePump", 4096, nullptr, 3, nullptr, 0);
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
  s_primed = false;
  s_eos = false;
  s_underruns = 0;
  s_dry_ms_total = 0;
  s_dry_since = 0;
  s_last_feed_ms = millis();
}

void voicePlayEos() { s_eos = true; }

void voicePlayGetStats(uint32_t* underruns, uint32_t* dry_ms) {
  if (underruns) *underruns = s_underruns;
  if (dry_ms) *dry_ms = s_dry_ms_total;
}

size_t voicePlayFeed(const uint8_t* pcm, size_t len) {
  if (!s_play || !pcm) return 0;
  size_t room = VOICE_PLAY_CAP - s_play_w;
  size_t n = (len < room) ? len : room;   // 满则截尾（不覆盖未播数据）
  memcpy(s_play + s_play_w, pcm, n);
  __sync_synchronize();     // 数据先落地，再发布 w（播放任务在另一核消费）
  s_play_w = s_play_w + n;
  s_last_feed_ms = millis();
  return n;
}

void voicePlayPump() {
  if (!M5.Speaker.isEnabled()) return;
  if (!s_primed) {
    // 只认两个起播条件：攒够 PRIME_BYTES，或 EOS（整段已在本地）。
    // 曾有第三个"静默 250ms 就播"的兜底——生成期 delta 间隙经常 >250ms，
    // 会在只攒了 0.4s 时提前起播，慢网（1.4x 实时）下头两秒反复见底
    // （2026-07-18 meter 实测）。短回复由 EOS 覆盖，无需它。
    bool enough = (s_play_w - s_play_r) >= PRIME_BYTES;
    if (!enough && !s_eos) return;
    s_primed = true;
  }
  // underrun 检测：已推过数据、还有存货、但队列空了 → 正在出声的间隙。
  if (s_play_r > 0 && s_play_w > s_play_r &&
      M5.Speaker.isPlaying(AUDIO_CH) == 0) {
    if (!s_dry_since) {
      s_dry_since = millis();
      ++s_underruns;
    }
  } else if (s_dry_since) {
    uint32_t gap = millis() - s_dry_since;
    s_dry_ms_total += gap;
    Serial.printf("[underrun] #%u gap=%ums at played=%.2fs\n",
                  (unsigned)s_underruns, (unsigned)gap, s_play_r / 48000.0f);
    s_dry_since = 0;
  }
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

size_t voicePlayBuffered() { return s_play_w - s_play_r; }
