// StackChan 语音助手固件入口（openspec change cores3-voice-assistant）。
//
// 独立产品：CoreS3 直连 DashScope Realtime (qwen-audio-3.0-realtime-flash)，
// PTT 按住说话。本文件替代 buddy 的 src/stackchan/main.cpp（无 BLE 桥），
// 复用 character_chan/motion/settings/sound + wifi_stream 的 wifiEnsureUp。
//
// 会话状态机（spec cores3-voice-assistant）：
//   SLEEP --touch--> CONNECTING --session.updated--> READY --hold--> LISTENING
//   --release--> THINKING --first audio--> SPEAKING --done+播完--> READY
//   READY --idle 5min--> SLEEP（断开丢上下文）；错误 → SLEEP，下次触摸重试。
//
// 初始化顺序 verbatim 抄自 src/stackchan/main.cpp::setup()。
// 回环 demo（任务组 3）已由本 FSM 取代，git 历史可查。

#include <M5Unified.h>
#include <WiFi.h>

#include "../stackchan/character_chan.h"
#include "../stackchan/motion.h"
#include "../stackchan/settings.h"
#include "../stackchan/sound.h"
#include "../stackchan/wifi_stream.h"
#include "audio_io.h"
#include "realtime_ws.h"

#ifndef STACKCHAN_DASHSCOPE_KEY
#define STACKCHAN_DASHSCOPE_KEY ""
#endif

namespace {

enum State : uint8_t { ST_SLEEP, ST_CONNECTING, ST_READY, ST_LISTENING, ST_THINKING, ST_SPEAKING };
State s_state = ST_SLEEP;

constexpr uint32_t IDLE_TIMEOUT_MS = 5 * 60 * 1000;  // TODO(task 5.4): settings 可调
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t THINK_TIMEOUT_MS = 20000;
constexpr size_t   SEND_BLOCK = 3200;                // 100ms @16k（design 决策 5）

uint32_t s_t_state = 0;        // 当前状态进入时刻
uint32_t s_t_activity = 0;     // 最近一次交互（idle 超时基准）
size_t   s_sent_bytes = 0;     // 本轮已上行的采集字节
uint32_t s_t_release = 0;      // 松手时刻（首声延迟统计）
int      s_turns = 0;

// 回调置位的标志（rtLoop 同线程调用，无并发问题）
volatile bool s_ev_ready = false, s_ev_done = false, s_ev_error = false;
bool s_got_audio = false;

char s_subtitle[384];
size_t s_subtitle_len = 0;

void setFace(uint8_t char_state, const char* subtitle) {
  characterSetState(char_state);
  motionSetState(char_state);
  if (subtitle) characterSetSubtitle(subtitle);
}

bool touchPressed() {
  return M5.Touch.getCount() > 0 && M5.Touch.getDetail(0).isPressed();
}

void enterState(State st, uint8_t face, const char* subtitle) {
  s_state = st;
  s_t_state = millis();
  setFace(face, subtitle);
}

bool keyIsPlaceholder() {
  return sizeof(STACKCHAN_DASHSCOPE_KEY) <= 1 ||
         strncmp(STACKCHAN_DASHSCOPE_KEY, "REPLACE_ME", 10) == 0;
}

// --- RtCallbacks ---
void onSessionReady() { s_ev_ready = true; }
void onAudio(const uint8_t* pcm, size_t len) {
  if (!s_got_audio) {
    s_got_audio = true;
    Serial.printf("[fsm] first audio +%.2fs\n", (millis() - s_t_release) / 1000.0f);
  }
  voicePlayFeed(pcm, len);
}
void onTranscript(const char* utf8, size_t len) {
  size_t room = sizeof(s_subtitle) - 1 - s_subtitle_len;
  if (len > room) len = room;
  memcpy(s_subtitle + s_subtitle_len, utf8, len);
  s_subtitle_len += len;
  s_subtitle[s_subtitle_len] = 0;
  characterSetSubtitle(s_subtitle);
}
void onDone() {
  s_ev_done = true;
  voicePlayEos();   // 整段回复已到齐 → 解锁预缓冲（短回复不用干等攒 1.5s）
}
void onError(const char*) { s_ev_error = true; }

void goSleep(const char* subtitle) {
  rtDisconnect();
  if (M5.Mic.isEnabled()) audioSpkStart();   // 保证回到 speaker 态（半双工归位）
  enterState(ST_SLEEP, CHAR_SLEEP, subtitle);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] StackChan voice-assistant firmware");

  settingsInit();

  const char* char_name = settingsGetCharName();
  if (!char_name || !*char_name) {
#ifdef BUDDY_DEFAULT_CHAR
    char_name = BUDDY_DEFAULT_CHAR;
#else
    char_name = nullptr;
#endif
  }
  characterInit(char_name);
  characterSetVoiceMode(true);

  soundInit();
  // NVS 音量是 buddy 的历史值（实测低到 12/255），语音回放下限夹到 96。
  // 只改 RAM 不写 NVS，不污染 buddy 的设置。
  if (M5.Speaker.getVolume() < 96) M5.Speaker.setVolume(96);

  motionInit();
  motionSetTilt(settingsGetTilt());
  motionSetEnabled(settingsGetMotionEnabled());
  motionSetIdleWiggle(settingsGetIdleWiggleEnabled());

  if (!audioIoInit()) {
    enterState(ST_SLEEP, CHAR_DIZZY, "PSRAM 音频缓冲分配失败");
    return;
  }
  if (keyIsPlaceholder()) {
    Serial.println("[warn] dashscope_key 是占位符 — 语音功能不可用");
    enterState(ST_SLEEP, CHAR_SLEEP, "缺少 API key（见 wifi_secrets.ini）");
    return;
  }
  enterState(ST_SLEEP, CHAR_SLEEP, "摸我唤醒");
  s_t_activity = millis();
}

void loop() {
  M5.update();
  characterTick();
  motionTick();
  rtLoop();
  // 喂喇叭已移交独立播放任务（audio_io.cpp playTask，10ms 周期）——主循环
  // 被 TLS 阻塞读卡多久都不再影响出声。这里只负责多抽水加速下载。
  if (s_state == ST_THINKING || s_state == ST_SPEAKING) {
    for (int i = 0; i < 3; ++i) rtLoop();
    static uint32_t s_meter_ms = 0;
    if (millis() - s_meter_ms > 1000) {          // 供需仪表：1 行/秒
      s_meter_ms = millis();
      Serial.printf("[meter] buffered=%.2fs\n", voicePlayBuffered() / 48000.0f);
    }
  }

  // 全局错误（断链/服务端 error）：回 SLEEP，下次触摸重试（spec 故障场景）。
  if (s_ev_error) {
    s_ev_error = false;
    Serial.println("[fsm] error -> SLEEP");
    goSleep("连接断了，摸我重连");
  }

  switch (s_state) {
    case ST_SLEEP:
      if (touchPressed() && !keyIsPlaceholder()) {
        setFace(CHAR_ATTENTION, "连 WiFi……");
        if (!wifiEnsureUp()) {
          // wifi_stream 的 M5_LOG* 被 CORE_DEBUG_LEVEL=0 编译掉，这里必须自己发声
          Serial.printf("[fsm] wifi associate failed, status=%d ssid=%s\n",
                        (int)WiFi.status(), STACKCHAN_WIFI_SSID);
          enterState(ST_SLEEP, CHAR_SLEEP, "WiFi 连不上，检查 wifi_secrets.ini");
          break;
        }
        characterSetSubtitle("连服务器……");
        s_ev_ready = false;
        if (!rtBegin({onSessionReady, onAudio, onTranscript, onDone, onError},
                     STACKCHAN_DASHSCOPE_KEY)) {
          enterState(ST_SLEEP, CHAR_SLEEP, "连接初始化失败");
          break;
        }
        enterState(ST_CONNECTING, CHAR_ATTENTION, nullptr);
      }
      break;

    case ST_CONNECTING:
      if (s_ev_ready) {
        s_ev_ready = false;
        Serial.printf("[fsm] session ready +%.2fs\n", (millis() - s_t_state) / 1000.0f);
        enterState(ST_READY, CHAR_IDLE, "按住我说话");
        s_t_activity = millis();
      } else if (millis() - s_t_state > CONNECT_TIMEOUT_MS) {
        Serial.println("[fsm] connect timeout");
        goSleep("连接超时，摸我重试");
      }
      break;

    case ST_READY:
      if (touchPressed()) {
        if (audioMicStart()) {
          s_sent_bytes = 0;
          enterState(ST_LISTENING, CHAR_ATTENTION, "在听……");
        } else {
          setFace(CHAR_DIZZY, "麦克风启动失败");
        }
      } else if (millis() - s_t_activity > IDLE_TIMEOUT_MS) {
        Serial.println("[fsm] idle timeout -> SLEEP（丢弃上下文）");
        goSleep("摸我唤醒");
      }
      break;

    case ST_LISTENING: {
      size_t bytes = audioMicPump();
      // 边录边传：每满 100ms 块推一帧（design 决策 5，掩盖上行时延）。
      while (bytes - s_sent_bytes >= SEND_BLOCK) {
        rtAppendAudio(audioMicData() + s_sent_bytes, SEND_BLOCK);
        s_sent_bytes += SEND_BLOCK;
      }
      if (!touchPressed() || bytes >= 16000 * 2 * 10) {
        if (bytes > s_sent_bytes) {
          rtAppendAudio(audioMicData() + s_sent_bytes, bytes - s_sent_bytes);
          s_sent_bytes = bytes;
        }
        Serial.printf("[fsm] committed %.2fs audio\n", bytes / 32000.0f);
        audioSpkStart();               // 半双工：mic → speaker
        voicePlayStart(24000);
        s_got_audio = false;
        s_ev_done = false;
        s_subtitle_len = 0; s_subtitle[0] = 0;
        rtCommitAndRespond();
        s_t_release = millis();
        enterState(ST_THINKING, CHAR_BUSY, "思考中……");
      }
      break;
    }

    case ST_THINKING:
      if (s_got_audio) {
        enterState(ST_SPEAKING, CHAR_HEART, nullptr);   // 字幕由 transcript 增量刷
      } else if (millis() - s_t_state > THINK_TIMEOUT_MS) {
        Serial.println("[fsm] think timeout");
        enterState(ST_READY, CHAR_IDLE, "没等到回复，再试试？");
        s_t_activity = millis();
      }
      break;

    case ST_SPEAKING:
      if (s_ev_done && !voicePlayActive()) {
        s_ev_done = false;
        ++s_turns;
        uint32_t ur = 0, dry = 0;
        voicePlayGetStats(&ur, &dry);
        Serial.printf("[fsm] turn %d done  underruns=%u dry=%ums (目标 0/0)\n",
                      s_turns, (unsigned)ur, (unsigned)dry);
        // TODO(task 5.1): s_turns 达上限（默认 20）重建 session 防累积计费。
        enterState(ST_READY, CHAR_IDLE, nullptr);       // 字幕留着最后一句
        s_t_activity = millis();
      }
      break;
  }
  delay(1);
}
