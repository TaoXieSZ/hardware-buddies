// 音效播放器实现。
// 每段旋律存为 Note 数组（freq=0 表示序列结束），tick() 检测 isPlaying() 推进下一音符。
// tone(freq, ms) 由 M5Unified DMA 后台播放，不阻塞主循环。
#include "sound_player.h"
#include "M5Cardputer.h"
#include <LittleFS.h>
#include <string.h>

namespace {
struct Note { uint16_t freq; uint16_t ms; };

// 各事件旋律（音符频率 Hz + 持续 ms，freq=0 结束）
// A5=880 B5=988 C6=1047 D6=1175 E6=1319
static const Note SND_APPROVAL[]   = {{880,130},{1047,130},{1319,230},{0,0}};  // A5-C6-E6 警示三音
static const Note SND_DONE[]       = {{880,90},{1047,90},{1175,90},{1319,260},{0,0}}; // 完成四音上行
static const Note SND_TOOL[]       = {{1047,40},{0,0}};  // C6 短促点击
static const Note SND_STOP_FAIL[]  = {{1047,130},{880,130},{698,220},{0,0}};   // 下行三音
static const Note SND_CONNECT[]    = {{880,120},{1319,200},{0,0}};  // A5-E6 上行欢迎
static const Note SND_DISCONNECT[] = {{1319,120},{880,180},{0,0}};  // E6-A5 下行告别
static const Note SND_NUDGE[]      = {{1175,30},{0,0}};  // D6 触感

const Note* g_seq = nullptr;
int         g_idx = 0;
uint32_t    g_playAfterMs = 0;  // millis() 时间戳，到了才允许播放（开机延迟）

int      g_volume = 150;        // 当前音量 0-255（-/= 键调节，tone 与 wav 共用）
uint8_t* g_wavBuf = nullptr;    // wav 读入缓冲（复用；playWav 异步播放期间不可覆盖）
size_t   g_wavCap = 0;
}  // namespace

namespace sound {

void begin() {
    M5Cardputer.Speaker.setVolume(g_volume);
    g_seq = SND_CONNECT;
    g_idx = 0;
    g_playAfterMs = 800;  // 开机 800ms 后播（等 DAC+BLE 稳定）
}

void play(const char* name) {
    const Note* seq = nullptr;
    if      (strcmp(name, "approval")    == 0) seq = SND_APPROVAL;
    else if (strcmp(name, "done")        == 0) seq = SND_DONE;
    else if (strcmp(name, "tool")        == 0) seq = SND_TOOL;
    else if (strcmp(name, "stop_fail")   == 0) seq = SND_STOP_FAIL;
    else if (strcmp(name, "connect")     == 0) seq = SND_CONNECT;
    else if (strcmp(name, "disconnect")  == 0) seq = SND_DISCONNECT;
    else if (strcmp(name, "nudge")       == 0) seq = SND_NUDGE;
    if (!seq) return;
    M5Cardputer.Speaker.stop();
    g_seq = seq;
    g_idx = 0;
}

bool busy() { return g_seq != nullptr; }

// 播放 hook 事件 wav：从 LittleFS 读 /sounds/<name>.wav 到 RAM → playWav(异步)。
// 文件不存在则 no-op —— 只有关键事件放了 wav，bridge 发的其他 play 字段自动忽略。
void playEvent(const char* name) {
    if (!name || !name[0]) return;
    char path[48];
    snprintf(path, sizeof(path), "/sounds/%s.wav", name);
    File f = LittleFS.open(path, "r");
    if (!f) return;
    size_t len = f.size();
    if (len == 0 || len > 100 * 1024) { f.close(); return; }  // RAM 保护(空闲~142KB)
    if (len > g_wavCap) {                                       // 复用缓冲,只在变大时重分配
        free(g_wavBuf);
        g_wavBuf = (uint8_t*)malloc(len);
        g_wavCap = g_wavBuf ? len : 0;
    }
    if (!g_wavBuf) { f.close(); return; }
    f.read(g_wavBuf, len);
    f.close();
    M5Cardputer.Speaker.stop();
    M5Cardputer.Speaker.playWav(g_wavBuf, len);
}

void volumeUp()   { g_volume = g_volume + 25 > 255 ? 255 : g_volume + 25; M5Cardputer.Speaker.setVolume(g_volume); M5Cardputer.Speaker.tone(1175, 40); }
void volumeDown() { g_volume = g_volume - 25 < 0   ? 0   : g_volume - 25; M5Cardputer.Speaker.setVolume(g_volume); M5Cardputer.Speaker.tone(880, 40); }
int  volume()     { return g_volume; }

void tick() {
    if (g_playAfterMs > 0) {
        if (millis() < g_playAfterMs) return;
        g_playAfterMs = 0;
    }
    if (!g_seq) return;
    if (M5Cardputer.Speaker.isPlaying()) return;  // 等当前音符结束
    const Note& n = g_seq[g_idx];
    if (n.freq == 0) { g_seq = nullptr; return; }
    M5Cardputer.Speaker.tone(n.freq, n.ms);
    g_idx++;
}

}  // namespace sound
