// FeedUITab5 — Agent Farm trigger feed + pet mood for the Tab5 1280x720 LCD.
// Reuses the shared AgentFarmClient transport; only the rendering differs from
// the Cardputer UI (much bigger screen, more feed rows, larger pet).
#pragma once

#include <Arduino.h>
#include <vector>
#include "agentfarm_feed/serial_feed_client.h"
#include "agentfarm_feed/trigger_log.h"

class FeedUITab5 {
 public:
  void begin();
  void onNewEntries(const std::vector<TriggerLog>& fresh);
  void tick(const SerialFeedClient& client);

 private:
  enum class Mood { Idle, Happy, Worried, Sleep };

  struct Rect { int x = 0, y = 0, w = 0, h = 0; };

  void render(const SerialFeedClient& client);
  void drawHeader(const SerialFeedClient& client);
  void drawPet(int x, int y, int w, int h);
  void drawVolume();        // − / + touch buttons + level bar in the sidebar
  void drawFeed(const SerialFeedClient& client, int x, int y, int w, int h);
  bool handleTouch();       // map taps to volume +/-; true if state changed
  void wake();

  Mood mood_ = Mood::Idle;
  Rect volMinus_;
  Rect volPlus_;
  AFStatus lastStatus_ = AFStatus::Connecting;
  bool dirty_ = true;
  uint8_t curBright_ = kBrightOn;  // last value pushed to the backlight
  uint32_t lastEventMs_ = 0;
  uint32_t happyUntilMs_ = 0;

  static const uint32_t kSleepAfterMs = 120000;  // doze after 2 min quiet
  static const uint8_t kBrightOn = 255;
  // Tab5's GPIO22 PWM backlight (Light_PWM) is effectively on/off: on-device
  // probing showed duty 4/10/24/64 all look full-bright and only 0 goes dark
  // (LED-driver current floor). So "napping" turns the backlight fully off;
  // any tap or new trigger wakes it back to kBrightOn.
  static const uint8_t kBrightDim = 0;
};
