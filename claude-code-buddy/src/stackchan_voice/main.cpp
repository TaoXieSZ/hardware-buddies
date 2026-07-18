// StackChan 语音助手固件入口（openspec change cores3-voice-assistant）。
//
// 独立产品：CoreS3 直连 DashScope Realtime (qwen-audio-3.0-realtime-flash)，
// PTT 按住说话。本文件替代 buddy 的 src/stackchan/main.cpp（无 BLE 桥、无
// daemon 心跳），复用 character_chan/motion/sound/settings 模块。
//
// 当前进度（tasks.md 任务组 3）：本地音频回环 demo——按住屏幕录音（16k），
// 松开原样播回，验证 采集→半双工切换→流播 整条本地链。WSS 会话在任务组 4-5
// 替换掉回环逻辑。
//
// 初始化顺序 verbatim 抄自 src/stackchan/main.cpp::setup()（M5.begin →
// settingsInit → characterInit → soundInit → motionInit，顺序注释见原文件）。

#include <M5Unified.h>

#include "../stackchan/character_chan.h"
#include "../stackchan/motion.h"
#include "../stackchan/settings.h"
#include "../stackchan/sound.h"
#include "audio_io.h"

namespace {
enum DemoState : uint8_t { DEMO_IDLE, DEMO_RECORDING, DEMO_PLAYING };
DemoState s_demo = DEMO_IDLE;

void setFace(uint8_t char_state, const char* subtitle) {
  characterSetState(char_state);
  motionSetState(char_state);
  characterSetSubtitle(subtitle);
}

bool touchPressed() {
  return M5.Touch.getCount() > 0 && M5.Touch.getDetail(0).isPressed();
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] StackChan voice-assistant firmware");

  // Settings come from NVS (Preferences). Load early so brightness +
  // volume are correct from the first paint/play.
  settingsInit();

  // Character pack pick order: NVS-stored name → build-flag default →
  // autodetect first /characters/<dir>.
  const char* char_name = settingsGetCharName();
  if (!char_name || !*char_name) {
#ifdef BUDDY_DEFAULT_CHAR
    char_name = BUDDY_DEFAULT_CHAR;
#else
    char_name = nullptr;
#endif
  }
  characterInit(char_name);

  // 语音桌宠 UI：全屏角色 + 底部字幕滚条（复用 Path A2 的 voice mode）。
  // 任务组 3 回环 demo 直接进 IDLE；SLEEP/唤醒逻辑在任务组 5 接入。
  characterSetVoiceMode(true);
  characterSetState(CHAR_IDLE);
  characterSetSubtitle("回环测试：按住屏幕说话，松开回放");

  // Speaker + preloaded WAV clips. Must come after M5.begin (speaker)
  // and after characterInit (which mounts LittleFS — soundInit reuses
  // that mount, so order matters).
  soundInit();

  // NVS 音量是 buddy 固件的历史值（实测低到 12/255，语音回放听不见）。
  // 语音桌宠的下限夹到 96 —— 只改 RAM 不写 NVS，不污染 buddy 的设置。
  if (M5.Speaker.getVolume() < 96) M5.Speaker.setVolume(96);

  // Body servos via StackChan-BSP; conservative speeds, USB-only budget.
  motionInit();
  motionSetTilt(settingsGetTilt());  // before enable so initial park uses correct Y
  motionSetEnabled(settingsGetMotionEnabled());
  motionSetIdleWiggle(settingsGetIdleWiggleEnabled());
  motionSetState(CHAR_IDLE);

  if (!audioIoInit()) {
    characterSetSubtitle("PSRAM 音频缓冲分配失败");
  }

#ifdef STACKCHAN_DASHSCOPE_KEY
  if (sizeof(STACKCHAN_DASHSCOPE_KEY) <= 1 ||
      strncmp(STACKCHAN_DASHSCOPE_KEY, "REPLACE_ME", 10) == 0) {
    Serial.println("[warn] dashscope_key 是占位符 — 语音功能将不可用"
                   "（wifi_secrets.ini 填入真实 key 后重编译）");
  }
#endif
}

void loop() {
  M5.update();
  characterTick();
  motionTick();

  switch (s_demo) {
    case DEMO_IDLE:
      if (touchPressed()) {
        if (audioMicStart()) {
          s_demo = DEMO_RECORDING;
          setFace(CHAR_ATTENTION, "在听……松开回放");
          Serial.println("[loop] mic start");
        } else {
          Serial.println("[err] mic start failed");
          setFace(CHAR_DIZZY, "麦克风启动失败");
        }
      }
      break;

    case DEMO_RECORDING: {
      size_t bytes = audioMicPump();
      if (!touchPressed() || bytes >= 16000 * 2 * 10) {  // 松手或触到 10s 上限
        // 采集统计（任务 3.1 验证依据）：时长 + 峰值幅度
        const int16_t* smp = (const int16_t*)audioMicData();
        size_t n = audioMicBytes() / 2;
        int peak = 0;
        for (size_t i = 0; i < n; ++i) {
          int v = smp[i] < 0 ? -smp[i] : smp[i];
          if (v > peak) peak = v;
        }
        Serial.printf("[rec] %.2fs %u samples peak=%d\n", n / 16000.0f, (unsigned)n, peak);

        audioSpkStart();                       // 半双工切换：mic → speaker
        voicePlayStart(16000);                 // 回环按采集采样率播
        voicePlayFeed(audioMicData(), audioMicBytes());
        s_demo = DEMO_PLAYING;
        setFace(CHAR_CELEBRATE, "回放中……");
        Serial.println("[loop] playback start");
      }
      break;
    }

    case DEMO_PLAYING:
      voicePlayPump();
      if (!voicePlayActive()) {
        s_demo = DEMO_IDLE;
        setFace(CHAR_IDLE, "回环测试：按住屏幕说话，松开回放");
        Serial.println("[loop] playback done");
      }
      break;
  }
  delay(1);
}
