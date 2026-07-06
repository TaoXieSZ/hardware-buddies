// 本机语音笔记录播器实现。
// Ground truth 逐字抄 upstream（见 recorder.h 头注引用的两个 .ino 示例）：
//   SD 引脚/挂载 ← Basic/sdcard/sdcard.ino
//   WAV header / Mic.begin/record/end ← Basic/mic_wav_record/mic_wav_record.ino
// 与示例不同处：示例一次性录满整块再存盘 / 整文件读到 RAM 再播（阻塞），本实现分帧——
// 每次 tick() 只抓/播一小段（RECORD_LEN 样本，示例同款 240），避免冻住 clawd GIF / BLE。
// 回放：file.seek(44) 跳 WAV header → 每帧 read(RECORD_LEN)→Speaker.playRaw，
// 靠 Speaker.isPlaying() 节流，不把整文件读进 RAM。抄 mic_wav_record.ino:328-367 的
// Speaker.playRaw 调用范式，但改成分块流式，不用 do{}while(isPlaying()) 阻塞抽干。
#include "recorder.h"
#include "sound_player.h"
#include "M5Cardputer.h"
#include <SPI.h>
#include <SD.h>

namespace {
// SD SPI 引脚（Cardputer-ADV）——逐字抄 sdcard.ino:21-24 / mic_wav_record.ino:18-21
#define SD_SPI_SCK_PIN  (40)
#define SD_SPI_MISO_PIN (39)
#define SD_SPI_MOSI_PIN (14)
#define SD_SPI_CS_PIN   (12)

// 采样参数与每帧粒度（录音 240≈15ms；回放 960≈60ms——块太小间隙大→不连贯）
static constexpr size_t   RECORD_LEN        = 240;
static constexpr size_t   PLAYBACK_LEN      = 960;
static constexpr uint32_t RECORD_SAMPLERATE = 16000;

// WAV 文件头（逐字抄 mic_wav_record.ino:39-53，sampleRate 固定 16000/mono/16bit）
struct WAVHeader {
    char riff[4]           = {'R', 'I', 'F', 'F'};
    uint32_t fileSize      = 0;
    char wave[4]           = {'W', 'A', 'V', 'E'};
    char fmt[4]            = {'f', 'm', 't', ' '};
    uint32_t fmtSize       = 16;
    uint16_t audioFormat   = 1;
    uint16_t numChannels   = 1;
    uint32_t sampleRate    = RECORD_SAMPLERATE;
    uint32_t byteRate      = RECORD_SAMPLERATE * sizeof(int16_t);
    uint16_t blockAlign    = sizeof(int16_t);
    uint16_t bitsPerSample = 16;
    char data[4]           = {'d', 'a', 't', 'a'};
    uint32_t dataSize      = 0;
};

bool     g_sdMounted  = false;    // SD 挂载成功标志（懒加载后缓存，避免重复 begin）
bool     g_recording  = false;
File     g_file;                  // 录音写入中 / 回放读取中的 WAV 文件（录音和回放互斥，共用句柄）
uint32_t g_dataBytes  = 0;        // 已写入的音频数据字节数（回填 header 用）
uint32_t g_startMs    = 0;        // 起录 / 起播 millis()（录音和回放互斥，共用）
int16_t  g_buf[RECORD_LEN];       // 每帧采集缓冲（录音用）
int16_t  g_pbBuf[2][PLAYBACK_LEN]; // 回放双缓冲：一个在播、另一个预读
int32_t  g_pbSamples[2] = {0, 0}; // 各缓冲的有效样本数（0=空，-1=EOF标记）
int      g_pbActive = 0;           // 当前正在播哪个（0 或 1）

// ── 回放状态 ──
bool     g_playing    = false;
uint32_t g_playBytes  = 0;        // 回放已播字节数（用于 elapsed 估算）
uint8_t  g_savedVol   = 0;        // 回放前的硬件音量：playNote 保存，stopPlayback 恢复

// 懒加载挂载 SD（第一次起录才做；SD 可能没插）。抄 sdcard.ino:52-54。
// 失败返回 false，绝不 while(1) 卡死（示例那样会挂整机）。
bool sdMount() {
    if (g_sdMounted) return true;
    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
        Serial.println("[rec] SD begin fail");
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        Serial.println("[rec] no SD card");
        return false;
    }
    g_sdMounted = true;
    return true;
}

// 扫根目录现有 note_*.wav，取最大号+1 → /note_%04d.wav。抄示例 file_counter 自增思路
// （改为扫盘取最大号，重启后不覆盖已有文件）。
void nextFreePath(char* out, size_t cap) {
    int maxN = -1;
    File dir = SD.open("/");
    if (dir) {
        while (File entry = dir.openNextFile()) {
            if (!entry.isDirectory()) {
                // name() 可能带前导 '/'；跳过后匹配 note_XXXX.wav
                const char* nm = entry.name();
                if (nm[0] == '/') nm++;
                int n = -1;
                if (sscanf(nm, "note_%d.wav", &n) == 1 && n > maxN) maxN = n;
            }
            entry.close();
        }
        dir.close();
    }
    snprintf(out, cap, "/note_%04d.wav", maxN + 1);
}

// 失败兜底：关文件（若开）、Mic.end()、归还 Speaker、清录音态。绝不留 Speaker 死掉。
void failSafe() {
    if (g_file) g_file.close();
    M5Cardputer.Mic.end();
    sound::resume();
    g_recording = false;
    g_dataBytes = 0;
}
}  // namespace

namespace recorder {

void begin() {
    // SD 懒加载、Mic 起录时才 begin——此处无需预初始化，仅保留钩子对齐 sound::begin()。
}

bool beginRecord() {
    if (g_recording) return true;
    if (g_playing) return false;                     // 互斥：回放中不录
    if (!sdMount()) return false;                    // SD 缺失/失败 → 不进录音态

    char path[24];
    nextFreePath(path, sizeof(path));
    g_file = SD.open(path, FILE_WRITE);
    if (!g_file) {
        Serial.printf("[rec] open fail %s\n", path);
        return false;
    }

    // 先写占位 header（size=0），停录时回填。抄示例 saveWAVToSD 的 header 写法。
    WAVHeader header;                                // size 字段默认 0 = 占位
    if (g_file.write((uint8_t*)&header, sizeof(WAVHeader)) != sizeof(WAVHeader)) {
        Serial.println("[rec] header write fail");
        g_file.close();
        return false;
    }

    // 让出 I2S：停当前提示音 + Speaker.end()，再 Mic.begin()。抄 mic_wav_record.ino:101-102。
    sound::releaseForMic();

    // 拉高 mic 输入增益：默认 magnification=16 录出来太轻，调到 48（3x）。
    // config 必须在 begin() 前设才生效（begin 读取 _cfg 初始化 I2S/ES8311）。
    // mic_config_t 见 M5Unified/src/utility/Mic_Class.hpp:74。
    {
        auto cfg = M5Cardputer.Mic.config();
        cfg.magnification = 48;
        M5Cardputer.Mic.config(cfg);
    }
    M5Cardputer.Mic.begin();

    g_dataBytes = 0;
    g_startMs   = millis();
    g_recording = true;
    Serial.printf("[rec] start %s\n", path);
    return true;
}

void tick() {
    if (!g_recording) return;
    // 每帧抓一小段（抄 mic_wav_record.ino:123 的 record 调用），追加写盘、累加 dataSize。
    if (M5Cardputer.Mic.record(g_buf, RECORD_LEN, RECORD_SAMPLERATE)) {
        size_t bytes = RECORD_LEN * sizeof(int16_t);
        if (g_file.write((uint8_t*)g_buf, bytes) != bytes) {   // 写失败（卡满/掉卡）→ 兜底停录
            Serial.println("[rec] data write fail");
            failSafe();
            return;
        }
        g_dataBytes += bytes;
    }
}

void endRecord() {
    if (!g_recording) return;
    M5Cardputer.Mic.end();                           // 先停 Mic 释放 I2S

    // 回填 header 真实 size（抄示例：fileSize=36+dataSize，dataSize=音频字节数），再 close。
    if (g_file) {
        WAVHeader header;
        header.fileSize = 36 + g_dataBytes;
        header.dataSize = g_dataBytes;
        g_file.seek(0);
        g_file.write((uint8_t*)&header, sizeof(WAVHeader));
        g_file.close();
    }

    sound::resume();                                 // 归还 Speaker（Speaker.begin()）
    g_recording = false;
    Serial.printf("[rec] stop %u bytes\n", (unsigned)g_dataBytes);
    g_dataBytes = 0;
}

bool isRecording() { return g_recording; }
uint32_t elapsedMs() { return g_recording ? (millis() - g_startMs) : 0; }

// ── 回放 ──

// 扫 SD 根目录 note_*.wav，收集文件名到 names[]，返回实际条数。
// 抄 mic_wav_record.ino scanAndDisplayWAVFiles:198-210 的 SD 遍历范式。
uint8_t listNotes(char names[][REC_NOTE_NAME_LEN], uint8_t maxN) {
    uint8_t n = 0;
    if (!g_sdMounted && !sdMount()) return 0;

    File dir = SD.open("/");
    if (!dir) return 0;

    while (File entry = dir.openNextFile()) {
        if (!entry.isDirectory()) {
            const char* nm = entry.name();
            if (nm[0] == '/') nm++;
            // 只收集 note_*.wav（本机所录），不混入 recorded*.wav 等 upstream 示例文件
            if (strncmp(nm, "note_", 5) == 0 && strlen(nm) > 4 &&
                strcmp(nm + strlen(nm) - 4, ".wav") == 0) {
                if (n < maxN) {
                    strncpy(names[n], nm, REC_NOTE_NAME_LEN - 1);
                    names[n][REC_NOTE_NAME_LEN - 1] = 0;
                }
                n++;
            }
        }
        entry.close();
    }
    dir.close();

    if (n > maxN) {
        Serial.printf("[rec] listNotes truncated: %u/%u\n", n, maxN);
        n = maxN;
    }
    return n;
}

// 打开 WAV 文件 → seek(44) 跳 header → 置 playing 态。录音中拒绝。
// 抄 mic_wav_record.ino playSelectedWAVFile:258-273 的打开+seek 范式。
bool playNote(const char* name) {
    if (g_recording) return false;           // 互斥：录音中不播
    if (g_playing) stopPlayback();

    if (!sdMount()) return false;

    char path[REC_NOTE_NAME_LEN + 4];
    snprintf(path, sizeof(path), "/%s", name);
    g_file = SD.open(path);
    if (!g_file) {
        Serial.printf("[rec] play open fail %s\n", path);
        return false;
    }

    g_file.seek(44);                         // 跳 WAV header（抄 upstream line 270）
    g_playing   = true;
    g_playBytes = 0;
    g_startMs   = millis();

    // 提升回放音量：保存原硬件音量 → 设到 ~78%（200/255）。sound_player 默认仅 25/255
    // 太轻听不清；播完 stopPlayback 再恢复原值。抄 mic_wav_record.ino:100 setVolume(255)
    g_savedVol = M5Cardputer.Speaker.getVolume();
    M5Cardputer.Speaker.setVolume(200);

    Serial.printf("[rec] play start %s\n", path);
    return true;
}

// 回放态每帧：双缓冲预读 + tight spin 排队喂 Speaker.playRaw。
// 在播 buffer 期间预读下一块到另一个 buffer → idle 瞬间无缝衔接。
// Tight spin（无 delay）绕过 ESP32 FreeRTOS tick 粒度（~10ms）延时——delay(1)
// 在 10ms tick 下实际是 delay(10)，造成每 chunk 间 10ms 死寂 → 人耳明显断续。
// 块取 960 样本（60ms@16k），spin 检测 isPlaying→false 仅 μs 级，间隙 → 0。
// 定期 taskYIELD 让 BLE / FreeRTOS 仍可调度，不饿死其他任务。
void tickPlayback() {
    if (!g_playing) return;

    uint32_t deadline = millis() + 120;   // 单帧最多忙等 120ms
    int iterCount = 0;

    while (g_playing && millis() < deadline) {
        int other = 1 - g_pbActive;

        if (M5Cardputer.Speaker.isPlaying()) {
            // 在播中：预读下一块到空闲缓冲（SD read 与播放重叠）
            if (g_pbSamples[other] == 0) {
                int32_t n = g_file.read((uint8_t*)g_pbBuf[other],
                                        PLAYBACK_LEN * sizeof(int16_t));
                if (n > 0) {
                    g_pbSamples[other] = n / sizeof(int16_t);
                } else {
                    g_pbSamples[other] = -1;            // EOF 标记
                }
            }
            // tight spin：不 delay，纯轮询。periodic yield 让 FreeRTOS 调度其他任务。
            if (++iterCount % 200 == 0) taskYIELD();
            continue;
        }

        iterCount = 0;

        // Speaker 空闲 — 立即提交预读缓冲（SD seek/read 已在在播期间完成，0 延迟）
        if (g_pbSamples[other] > 0) {
            M5Cardputer.Speaker.playRaw(g_pbBuf[other], g_pbSamples[other],
                                        RECORD_SAMPLERATE);
            g_playBytes += g_pbSamples[other] * sizeof(int16_t);
            g_pbSamples[other] = 0;
            g_pbActive = other;
        } else if (g_pbSamples[other] == -1) {
            stopPlayback();              // EOF
            return;
        } else {
            // 首次进回放：直接读第一块
            int32_t n = g_file.read((uint8_t*)g_pbBuf[0],
                                    PLAYBACK_LEN * sizeof(int16_t));
            if (n <= 0) { stopPlayback(); return; }
            g_pbSamples[0] = n / sizeof(int16_t);
            M5Cardputer.Speaker.playRaw(g_pbBuf[0], g_pbSamples[0],
                                        RECORD_SAMPLERATE);
            g_playBytes += g_pbSamples[0] * sizeof(int16_t);
            g_pbSamples[0] = 0;
            g_pbActive = 0;
        }
    }
}

void stopPlayback() {
    if (!g_playing) return;
    if (g_file) g_file.close();
    g_playing    = false;
    g_playBytes  = 0;
    g_pbSamples[0] = g_pbSamples[1] = 0;   // 清双缓冲
    g_pbActive   = 0;

    M5Cardputer.Speaker.setVolume(g_savedVol);  // 恢复回放前原音量
    Serial.printf("[rec] play stop %u bytes\n", (unsigned)g_playBytes);
}

bool isPlaying() { return g_playing; }
uint32_t playbackElapsedMs() {
    return g_playing ? (millis() - g_startMs) : 0;
}

// 删指定笔记文件。抄 mic_wav_record.ino:239 的 SD.remove 范式。
bool deleteNote(const char* name) {
    if (!name || !name[0]) return false;
    if (!sdMount()) return false;

    char path[REC_NOTE_NAME_LEN + 4];
    snprintf(path, sizeof(path), "/%s", name);
    if (!SD.remove(path)) {
        Serial.printf("[rec] delete fail %s\n", path);
        return false;
    }
    Serial.printf("[rec] deleted %s\n", path);
    return true;
}

// 回放中调硬件音量 ±25（-/= 键）。回放音量是 boost 过后的，所以直接调硬件值
// 并同步 g_savedVol，stopPlayback 恢复时就是新音量。非回放态不调（no-op，音量
// 由 sound_player 的 volumeUp/volumeDown 管）。
void adjVolume(int delta) {
    if (!g_playing) return;
    int v = (int)M5Cardputer.Speaker.getVolume() + delta;
    if (v > 255) v = 255;
    if (v < 0)   v = 0;
    g_savedVol = (uint8_t)v;
    M5Cardputer.Speaker.setVolume(g_savedVol);
}

// 存文本笔记到 SD：扫 /txt_*.txt 取最大号+1，写入，关闭。自增命名，重启不覆盖。
bool saveTextNote(const char* text) {
    if (!text || !text[0]) return false;
    if (!sdMount()) return false;

    // 扫盘取最大号（同 voice-note 自增逻辑）
    int maxN = -1;
    File dir = SD.open("/");
    if (dir) {
        while (File entry = dir.openNextFile()) {
            if (!entry.isDirectory()) {
                const char* nm = entry.name();
                if (nm[0] == '/') nm++;
                int n = -1;
                if (sscanf(nm, "txt_%d.txt", &n) == 1 && n > maxN) maxN = n;
            }
            entry.close();
        }
        dir.close();
    }

    char path[REC_NOTE_NAME_LEN + 4];
    snprintf(path, sizeof(path), "/txt_%04d.txt", maxN + 1);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[rec] txt save open fail %s\n", path);
        return false;
    }
    size_t len = strlen(text);
    if (f.write((const uint8_t*)text, len) != len) {
        Serial.printf("[rec] txt write fail %s\n", path);
        f.close();
        return false;
    }
    f.close();
    Serial.printf("[rec] txt saved %s (%u bytes)\n", path, (unsigned)len);
    return true;
}

}  // namespace recorder
