// Tab5 P0 bringup — smoke test for the dev toolchain + core peripherals.
// Gate (docs/proposals/tab5-buddy.md): builds, flashes over USB-C, and the
// screen / touch / mic / battery all demonstrably work. Nothing here is
// buddy logic — this file exists to prove the pioarduino + M5Unified stack
// before P1 builds the real dashboard on top.
#include <M5Unified.h>

static uint32_t lastStatusMs = 0;
static int16_t micBuf[256];
static int micPeak = 0;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  M5.Display.setRotation(3);  // landscape 1280x720 (3, not 1 — 1 is upside-down on this unit)
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(24, 24);
  M5.Display.printf("Tab5 bringup — board=%d  %dx%d\n",
                    (int)M5.getBoard(), M5.Display.width(), M5.Display.height());
  M5.Display.setCursor(24, 64);
  M5.Display.printf("PSRAM %u MB   Flash %u MB",
                    (unsigned)(ESP.getPsramSize() >> 20),
                    (unsigned)(ESP.getFlashChipSize() >> 20));
  M5.Display.setCursor(24, 660);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.print("touch to paint  |  speak for VU  |  status refreshes 1s");

  M5.Mic.begin();
  Serial.printf("[tab5] board=%d display=%dx%d psram=%u mic=%d\n",
                (int)M5.getBoard(), M5.Display.width(), M5.Display.height(),
                (unsigned)ESP.getPsramSize(), (int)M5.Mic.isEnabled());
}

void loop() {
  M5.update();

  auto t = M5.Touch.getDetail();
  if (t.isPressed()) {
    M5.Display.fillCircle(t.x, t.y, 10, TFT_CYAN);
  }

  if (M5.Mic.record(micBuf, 256, 16000)) {
    int peak = 0;
    for (auto s : micBuf) { int a = s < 0 ? -s : s; if (a > peak) peak = a; }
    micPeak = peak > micPeak ? peak : micPeak - (micPeak >> 3);
  }

  uint32_t now = millis();
  if (now - lastStatusMs >= 1000) {
    lastStatusMs = now;
    int batt = M5.Power.getBatteryLevel();
    int vu = micPeak * 400 / 32767;
    M5.Display.fillRect(24, 120, 460, 80, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(24, 120);
    M5.Display.printf("up %lus   batt %d%%", (unsigned long)(now / 1000), batt);
    M5.Display.drawRect(24, 164, 404, 24, TFT_DARKGREY);
    M5.Display.fillRect(26, 166, vu, 20, vu > 340 ? TFT_RED : TFT_GREEN);
    Serial.printf("[tab5] up=%lus batt=%d micPeak=%d touch=%d\n",
                  (unsigned long)(now / 1000), batt, micPeak,
                  (int)M5.Touch.getCount());
  }
}
