// StackChan 语音助手固件入口（openspec change cores3-voice-assistant）。
//
// 独立产品：CoreS3 直连 DashScope Realtime (qwen-audio-3.0-realtime-flash)，
// PTT 按住说话。本文件替代 buddy 的 src/stackchan/main.cpp（无 BLE 桥、无
// daemon 心跳），复用 character_chan/motion/sound/settings 模块。
//
// 当前进度（tasks.md 任务组 2 骨架）：开机 → 语音模式大脸 + SLEEP 打盹 +
// 字幕提示。会话状态机 / 音频链路 / WSS 在任务组 3-5 逐步接入。
//
// 初始化顺序 verbatim 抄自 src/stackchan/main.cpp::setup()（M5.begin →
// settingsInit → characterInit → soundInit → motionInit，顺序注释见原文件）。

#include <M5Unified.h>

#include "../stackchan/character_chan.h"
#include "../stackchan/motion.h"
#include "../stackchan/settings.h"
#include "../stackchan/sound.h"

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
  // spec: 待机态 = SLEEP 打盹，触摸唤醒（唤醒逻辑在任务组 5 接入）。
  characterSetVoiceMode(true);
  characterSetState(CHAR_SLEEP);
  characterSetSubtitle("触摸唤醒");

  // Speaker + preloaded WAV clips. Must come after M5.begin (speaker)
  // and after characterInit (which mounts LittleFS — soundInit reuses
  // that mount, so order matters).
  soundInit();

  // Body servos via StackChan-BSP; conservative speeds, USB-only budget.
  motionInit();
  motionSetTilt(settingsGetTilt());  // before enable so initial park uses correct Y
  motionSetEnabled(settingsGetMotionEnabled());
  motionSetIdleWiggle(settingsGetIdleWiggleEnabled());
  motionSetState(CHAR_SLEEP);

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
  delay(1);
}
