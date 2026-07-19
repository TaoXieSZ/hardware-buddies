#pragma once
#include <stdint.h>

// Persistent runtime settings for StackChan buddy, mirrored on NVS so
// dashboard changes survive reboots. All getters return the current
// (in-RAM) value; setters update RAM, save to NVS, and call into the
// owning subsystem (sound / motion / lcd / character) to apply.
//
// Default values match the hardcoded behaviour from the initial firmware:
//   volume      = 96   (M5.Speaker volume 0-255)
//   brightness  = 200  (M5.Lcd brightness 0-255)
//   char_name   = ""   (autodetect — falls back to BUDDY_DEFAULT_CHAR)
//   motion      = true
//   idle_wiggle = true

void     settingsInit();                // call after M5.begin + LittleFS mount

uint8_t  settingsGetVolume();
void     settingsSetVolume(uint8_t v);

uint8_t  settingsGetBrightness();
void     settingsSetBrightness(uint8_t v);

const char* settingsGetCharName();      // empty string if unset
void        settingsSetCharName(const char* name);

bool     settingsGetMotionEnabled();
void     settingsSetMotionEnabled(bool on);

bool     settingsGetIdleWiggleEnabled();
void     settingsSetIdleWiggleEnabled(bool on);

// Head-up tilt in degrees (0..90). Default 65 (~head-up but well clear
// of the mechanical stop). Persisted to NVS, applied via motionSetTilt.
uint8_t  settingsGetTilt();
void     settingsSetTilt(uint8_t deg);

// Screen-off delay in seconds after entering SLEEP state. 0 = never
// blank (always-on, runs hot). Default 60s. main.cpp polls this in
// loop() and drops backlight to 0 once exceeded; first non-SLEEP state
// restores settingsGetBrightness().
uint16_t settingsGetSleepAfter();
void     settingsSetSleepAfter(uint16_t sec);

// --- 语音助手固件（cores3-stackchan-voice）专用；buddy 固件不读 ---
// 懒连接空闲断开秒数（README/spec 默认 300s，范围 30..3600）。
uint16_t settingsGetVoiceIdleSec();
void     settingsSetVoiceIdleSec(uint16_t sec);
// 同一连接内对话轮数上限，达到即重建 session 防上下文累积计费
// （默认 20，范围 1..100）。
uint8_t  settingsGetVoiceTurnLimit();
void     settingsSetVoiceTurnLimit(uint8_t n);
