// StackChan 语音助手固件入口（openspec change cores3-voice-assistant）。
//
// 独立产品：CoreS3 直连 DashScope Realtime (qwen-audio-3.0-realtime-flash)，
// PTT 按住说话，屏上是矢量猫脸"小咪"（cat_face.cpp，无 GIF/LittleFS 依赖）。
// 本文件替代 buddy 的 src/stackchan/main.cpp（无 BLE 桥），复用 motion/
// settings + wifi_stream 的 wifiEnsureUp。
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

#include "../stackchan/character_chan.h"   // 仅用 CharState 枚举（motion 同套）
#include "../stackchan/motion.h"
#include "../stackchan/settings.h"
#include "../stackchan/wifi_stream.h"
#include "audio_io.h"
#include "cat_face.h"
#include "panel_state.h"
#include "realtime_ws.h"
#include "web_panel.h"
#include "wifi_store.h"

#ifndef STACKCHAN_WIFI_SSID
#define STACKCHAN_WIFI_SSID ""
#endif
#ifndef STACKCHAN_WIFI_PASS
#define STACKCHAN_WIFI_PASS ""
#endif

#ifndef STACKCHAN_DASHSCOPE_KEY
#define STACKCHAN_DASHSCOPE_KEY ""
#endif

namespace {

enum State : uint8_t { ST_SLEEP, ST_CONNECTING, ST_READY, ST_LISTENING, ST_THINKING, ST_SPEAKING };
State s_state = ST_SLEEP;

// 空闲断开秒数与轮数上限走 settings（NVS 可调，键 vidle/vturns）。
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

// --- 控制面板相关 ---
uint32_t s_dance_until = 0;     // 面板触发的一次性跳舞截止时刻（0 = 未在跳）
bool     s_want_sleep = false;  // 面板请求断开，由状态机在安全处收尾
uint16_t s_last_tokens = 0;     // 上轮 usage total，供面板显示
uint16_t s_last_latency_ms = 0;
char     s_last_user[384] = "";   // 上轮用户说了什么（服务端 ASR 转写）
char     s_last_reply[384] = "";  // 上轮小咪答了什么
uint32_t s_panel_ms = 0;

void setFace(uint8_t char_state, const char* subtitle) {
  catFaceSetState(char_state);
  motionSetState(char_state);
  if (subtitle) catFaceSetSubtitle(subtitle);
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
    s_last_latency_ms = (uint16_t)(millis() - s_t_release);   // 面板显示用
    Serial.printf("[fsm] first audio +%.2fs\n", s_last_latency_ms / 1000.0f);
  }
  voicePlayFeed(pcm, len);
}

void onUserTranscript(const char* utf8, size_t len) {
  if (len >= sizeof(s_last_user)) len = sizeof(s_last_user) - 1;
  memcpy(s_last_user, utf8, len);
  s_last_user[len] = 0;
}
void onTranscript(const char* utf8, size_t len) {
  size_t room = sizeof(s_subtitle) - 1 - s_subtitle_len;
  if (len > room) len = room;
  memcpy(s_subtitle + s_subtitle_len, utf8, len);
  s_subtitle_len += len;
  s_subtitle[s_subtitle_len] = 0;
  catFaceSetSubtitle(s_subtitle);
}
void onDone(uint32_t total_tokens) {
  s_ev_done = true;
  s_last_tokens = (uint16_t)total_tokens;   // 面板显示本轮花费
  voicePlayEos();   // 整段回复已到齐 → 解锁预缓冲（短回复不用干等攒 1.5s）
}
void onError(const char*) { s_ev_error = true; }

// 把当前设置推给面板（GET /api/settings 的数据源）。启动时与每次应用改动后调用。
void publishSettingsToPanel() {
  webPanelPublishSettings(M5.Speaker.getVolume(), settingsGetBrightness(),
                          settingsGetVoiceIdleSec(), settingsGetVoiceTurnLimit(),
                          settingsGetMotionEnabled(), settingsGetIdleWiggleEnabled(),
                          settingsGetTilt(), settingsGetVoiceDance(),
                          settingsGetVoiceName(), settingsGetVoicePersona());
}

// 应用面板暂存的改动。只在主循环调用 —— 外设与 NVS 都不能从 HTTP 任务碰。
void applyPanelPending() {
  panel::Pending p;
  if (!webPanelTakePending(&p)) return;

  if (p.has_volume)      settingsSetVolume(p.volume);
  if (p.has_brightness)  settingsSetBrightness(p.brightness);
  if (p.has_idle_sec)    settingsSetVoiceIdleSec(p.idle_sec);
  if (p.has_turn_limit)  settingsSetVoiceTurnLimit(p.turn_limit);
  if (p.has_tilt)        settingsSetTilt(p.tilt);
  if (p.has_motion)      settingsSetMotionEnabled(p.motion);
  if (p.has_idle_wiggle) settingsSetIdleWiggleEnabled(p.idle_wiggle);
  if (p.has_dance)       settingsSetVoiceDance(p.dance);
  // 音色与人设只落 NVS，不动当前会话（spec：下次建立会话才生效）。
  if (p.has_voice)       settingsSetVoiceName(p.voice);
  if (p.has_persona)     settingsSetVoicePersona(p.persona);

  if (p.has_action) {
    if (strcmp(p.action, "dance") == 0) {
      if (settingsGetMotionEnabled()) {
        motionSetState(CHAR_CELEBRATE);
        s_dance_until = millis() + 2500;   // 到点回到当前状态的动作
      }
    } else if (strcmp(p.action, "disconnect") == 0) {
      s_want_sleep = true;                 // 由状态机在安全处收尾
    }
  }
  publishSettingsToPanel();
}

// 发布状态快照给面板（HTTP 任务只读）。
void publishSnapshot() {
  panel::Snapshot s;
  s.state = (uint8_t)s_state;
  s.wifi_up = (WiFi.status() == WL_CONNECTED);
  s.ds_connected = rtSessionReady();
  s.turns = (uint8_t)s_turns;
  s.turn_limit = settingsGetVoiceTurnLimit();
  s.last_latency_ms = s_last_latency_ms;
  s.last_tokens = s_last_tokens;
  uint32_t ur = 0, dry = 0;
  voicePlayGetStats(&ur, &dry);
  s.underruns = (uint16_t)ur;
  s.dry_ms = (uint16_t)dry;
  // 电量走 I2C(慢)，缓存 30s 读一次，别在快照热路径每次戳。
  static int8_t s_bat = -1;
  static uint32_t s_bat_ms = 0;
  if (s_bat < 0 || millis() - s_bat_ms > 30000) {
    s_bat = (int8_t)M5.Power.getBatteryLevel();
    s_bat_ms = millis();
  }
  s.battery_pct = s_bat;
  if (s_state == ST_READY) {
    uint32_t used = millis() - s_t_activity;
    uint32_t total = settingsGetVoiceIdleSec() * 1000UL;
    s.idle_remain_sec = used < total ? (total - used) / 1000 : 0;
  }
  s.setSubtitle(s_subtitle_len ? s_subtitle : "");
  s.setUserText(s_last_user);
  s.setReplyText(s_last_reply);
  webPanelPublish(s);
}

// --- 配网（xiaomi-wifi-provisioning） ---
constexpr const char* AP_SSID = "小咪-setup";
constexpr const char* AP_PASS = "zhumi-setup";
constexpr uint32_t SETUP_TIMEOUT_MS = 10 * 60 * 1000;   // 10 分钟没人配就收摊
uint32_t s_setup_until = 0;

// 连接进度 → 屏幕字幕（用户仅凭设备屏幕就知道发生了什么）
void wifiProgress(const char* human) { catFaceSetSubtitle(human); }

void enterSetupMode() {
  const char* ip = webPanelStartApMode(AP_SSID, AP_PASS);
  if (!ip) {
    enterState(ST_SLEEP, CHAR_DIZZY, "配网热点开不起来，摸我重试");
    return;
  }
  s_setup_until = millis() + SETUP_TIMEOUT_MS;
  char msg[128];
  // 热点名和密码都得显示 —— 用户要照着这个用手机连上来
  snprintf(msg, sizeof(msg), "手机连热点 %s  密码 %s  然后按提示配网",
           AP_SSID, AP_PASS);
  enterState(ST_SLEEP, CHAR_ATTENTION, msg);
  Serial.printf("[setup] 配网模式：SSID=%s PASS=%s 门户=http://%s/\n",
                AP_SSID, AP_PASS, ip);
}

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

  // WiFi 凭据表：编译期凭据只作为"出厂种子"灌一次，此后以 NVS 为准。
  // 已在用的设备升级后照常联网；用户删掉种子网络也不会被复活。
  wifi_store::deviceLoad(STACKCHAN_WIFI_SSID, STACKCHAN_WIFI_PASS);

  // 小咪的脸：纯矢量绘制，不依赖 GIF 角色包/LittleFS（uploadfs 都免了）。
  catFaceInit();

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
  catFaceTick();
  motionTick();
  rtLoop();
  if (s_state == ST_SPEAKING) catFaceSetTalking(voicePlayActive());
  // 喂喇叭已移交独立播放任务（audio_io.cpp playTask，10ms 周期）——主循环
  // 被 TLS 阻塞读卡多久都不再影响出声。这里只负责多抽水加速下载。
  if (s_state == ST_THINKING || s_state == ST_SPEAKING) {
    for (int i = 0; i < 3; ++i) rtLoop();
    static uint32_t s_meter_ms = 0;
    if (millis() - s_meter_ms > 1000) {          // 供需仪表：1 行/秒
      s_meter_ms = millis();
      Serial.printf("[meter] buffered=%.2fs heap=%uKB(min %uKB)\n",
                    voicePlayBuffered() / 48000.0f,
                    (unsigned)(ESP.getFreeHeap() / 1024),
                    (unsigned)(ESP.getMinFreeHeap() / 1024));
    }
  }

  // --- 配网模式：等用户在手机上配好，或超时收摊 ---
  if (webPanelInApMode()) {
    if (webPanelTakeProvisioned()) {
      Serial.println("[setup] 收到新凭据，关热点去连");
      webPanelStopApMode();
      setFace(CHAR_ATTENTION, "配好啦，这就去连……");
      // 不重启，直接用新凭据连（spec：配网成功即用）
      if (wifi_store::deviceConnect(15000, wifiProgress)) {
        webPanelStart();
        publishSettingsToPanel();
        enterState(ST_SLEEP, CHAR_IDLE, "连上啦，摸我开始聊天");
      } else {
        enterSetupMode();      // 密码错之类 → 回配网模式让用户重来
      }
    } else if (millis() > s_setup_until) {
      Serial.println("[setup] 超时无人配网，关热点");
      webPanelStopApMode();
      enterState(ST_SLEEP, CHAR_SLEEP, "摸我唤醒");
    }
    delay(1);
    return;                    // 配网期间不跑对话状态机
  }

  // --- 控制面板 ---
  // 关键：说话/思考期间**不**跑面板的重活。音频下载(WSS 收包+解密+喂缓冲)在主
  // 循环里，被面板抢锁/发快照(还带慢速 I2C 读电量)挤慢就会饿死播放 → 卡顿+字幕
  // 卡+心跳断线(2026-07-22 回归)。一轮对话独占主循环，面板延后到 READY 才更新。
  const bool inTurn = (s_state == ST_THINKING || s_state == ST_SPEAKING);
  if (!inTurn) applyPanelPending();
  if (s_dance_until && millis() > s_dance_until) {   // 一次性跳舞结束，动作归位
    s_dance_until = 0;
    motionSetState(s_state == ST_SPEAKING ? CHAR_HEART
                                          : (s_state == ST_SLEEP ? CHAR_SLEEP : CHAR_IDLE));
  }
  if (s_want_sleep) {                                 // 面板请求断开
    s_want_sleep = false;
    if (s_state != ST_SLEEP) goSleep("面板让我去打盹啦");
  }
  if (!inTurn && millis() - s_panel_ms > 200) {       // 快照 5Hz，够面板 1s 轮询用
    s_panel_ms = millis();
    publishSnapshot();
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
        // 多网络：扫描现场自动连已存网络里信号最强的那个（家/公司各存一次即可）。
        setFace(CHAR_ATTENTION, "找网络中……");
        if (!wifi_store::deviceConnect(15000, wifiProgress)) {
          // 连不上任何已知网络 → 进配网模式（开热点让手机来配）。
          enterSetupMode();
          break;
        }
        // WiFi 起来了就把面板拉起来（幂等）。注意打盹不会断 WiFi，所以此后
        // 面板一直可用，包括小咪睡着时——spec"打盹期间仍可调参"即此。
        if (webPanelStart()) {
          publishSettingsToPanel();
          static bool announced = false;
          if (!announced) {
            announced = true;
            char msg[64];
            snprintf(msg, sizeof(msg), "面板 http://%s/",
                     WiFi.localIP().toString().c_str());
            catFaceSetSubtitle(msg);
            Serial.printf("[panel] %s\n", msg);
            delay(1200);          // 让用户来得及看清地址
          }
        }
        catFaceSetSubtitle("连服务器……");
        s_ev_ready = false;
        if (!rtBegin({onSessionReady, onAudio, onTranscript, onUserTranscript, onDone, onError},
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
      } else if (millis() - s_t_activity > settingsGetVoiceIdleSec() * 1000UL) {
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
        // 脸与动作解耦：脸始终用会眨眼的说话表情；开了"说话时来段舞"就把舵机
        // 换成更活泼的 CELEBRATE 摆动（总开关关闭时 motion 层自会不动）。
        if (settingsGetVoiceDance()) motionSetState(CHAR_CELEBRATE);
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
        // 留一份给面板的"最近一轮"（字幕会被下一轮清空，这里得自己存）
        strncpy(s_last_reply, s_subtitle, sizeof(s_last_reply) - 1);
        s_last_reply[sizeof(s_last_reply) - 1] = 0;
        if (s_turns >= (int)settingsGetVoiceTurnLimit()) {
          // 上下文逐轮累积计费（design 实测定案）：达上限断开重连拿新 session。
          Serial.printf("[fsm] turn limit %u -> rebuild session\n",
                        settingsGetVoiceTurnLimit());
          rtDisconnect();
          s_turns = 0;
          s_ev_ready = false;
          if (rtBegin({onSessionReady, onAudio, onTranscript, onUserTranscript, onDone, onError},
                      STACKCHAN_DASHSCOPE_KEY)) {
            enterState(ST_CONNECTING, CHAR_BUSY, "记性满啦，翻篇中……");
          } else {
            goSleep("重连失败，摸我重试");
          }
        } else {
          enterState(ST_READY, CHAR_IDLE, nullptr);     // 字幕留着最后一句
          s_t_activity = millis();
        }
      }
      break;
  }
  delay(1);
}
