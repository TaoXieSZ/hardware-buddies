// Tab5 P0/M0 bringup — smoke test for the dev toolchain + core peripherals.
// Gate (docs/proposals/tab5-buddy.md): builds, flashes over USB-C, and the
// screen / touch / mic / battery all demonstrably work. M0 adds the WiFi
// smoke test (docs/proposals/tab5-p1-dashboard.md): the P4 has no radio, so
// WiFi.begin() exercises the whole ESP-Hosted→C6 path — the #1 risk of the
// Arduino route. Nothing here is buddy logic.
// NOTE on the C6 co-processor firmware: the factory C6 ships esp-hosted
// slave 1.4.1, which the Arduino host stack (esp-hosted 2.8.x) cannot talk
// to — scans return 0/-2 and the in-firmware hosted OTA API can't reach it
// either (the 2.x OTA RPC times out against a 1.x slave). The one-shot
// ESP-IDF updater at tools/tab5-c6-updater/ (esp_hosted 1.4.0 host = the
// factory protocol generation) flashes esp32c6-v2.8.5 over SDIO; run it
// once per new unit. Do NOT re-add a hostedHasUpdate()-driven self-update
// here with a baked-in image: once the host stack moves past the embedded
// version it would reflash the stale image on every boot.
#include <M5Unified.h>
#include <WiFi.h>

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

  // M0: non-blocking join; the 1s status loop reports progress.
  // Tab5's P4↔C6 SDIO wiring — MUST be set before the first WiFi call. The
  // m5stack_tab5 variant only carries these pin macros on newer cores; on
  // 3.3.0 the hosted glue falls back to the EV-board pins (18/19/14-17/54)
  // and the P4 hard-crashes (Load access fault) right after hostedInit.
  WiFi.setPins(/*clk*/12, /*cmd*/13, /*d0*/11, /*d1*/10, /*d2*/9, /*d3*/8, /*rst*/15);
  WiFi.mode(WIFI_STA);    // first WiFi call brings up the ESP-Hosted link
  // One-shot scan diagnostic: distinguishes "C6 radio deaf" (0 networks =
  // antenna/RF path) from "our AP invisible" (networks found but ours
  // missing = wrong band — the C6 is 2.4 GHz only).
  int n = WiFi.scanNetworks();
  Serial.printf("[tab5] scan: %d networks\n", n);
  for (int i = 0; i < n && i < 15; i++) {
    bool ours = WiFi.SSID(i) == TAB5_WIFI_SSID;
    Serial.printf("[tab5]   ch%-2d %4ddBm %s%s\n", WiFi.channel(i), WiFi.RSSI(i),
                  ours ? ">>> " : "", WiFi.SSID(i).c_str());
  }
  WiFi.scanDelete();
  WiFi.begin(TAB5_WIFI_SSID, TAB5_WIFI_PASS);
  Serial.printf("[tab5] wifi: joining %s ...\n", TAB5_WIFI_SSID);
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

  // Rescan every 10s while unjoined — the boot-time scan can race the C6
  // slave coming up; a later scan succeeding would prove the radio is fine.
  static uint32_t lastScanMs = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastScanMs >= 10000) {
    lastScanMs = millis();
    int n = WiFi.scanNetworks();
    Serial.printf("[tab5] rescan: %d networks\n", n);
    for (int i = 0; i < n && i < 8; i++) {
      Serial.printf("[tab5]   ch%-2d %4ddBm %s\n", WiFi.channel(i), WiFi.RSSI(i),
                    WiFi.SSID(i).c_str());
    }
    WiFi.scanDelete();
    if (n > 0) WiFi.begin(TAB5_WIFI_SSID, TAB5_WIFI_PASS);
  }

  uint32_t now = millis();
  if (now - lastStatusMs >= 1000) {
    lastStatusMs = now;
    int batt = M5.Power.getBatteryLevel();
    int vu = micPeak * 400 / 32767;
    M5.Display.fillRect(24, 120, 700, 80, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(24, 120);
    bool up = WiFi.status() == WL_CONNECTED;
    if (up) {
      M5.Display.printf("up %lus   batt %d%%   wifi %s  %ddBm",
                        (unsigned long)(now / 1000), batt,
                        WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      M5.Display.printf("up %lus   batt %d%%   wifi joining... (st=%d)",
                        (unsigned long)(now / 1000), batt, (int)WiFi.status());
    }
    M5.Display.drawRect(24, 164, 404, 24, TFT_DARKGREY);
    M5.Display.fillRect(26, 166, vu, 20, vu > 340 ? TFT_RED : TFT_GREEN);
    Serial.printf("[tab5] up=%lus batt=%d micPeak=%d touch=%d wifi=%d ip=%s rssi=%d\n",
                  (unsigned long)(now / 1000), batt, micPeak,
                  (int)M5.Touch.getCount(), (int)WiFi.status(),
                  up ? WiFi.localIP().toString().c_str() : "-",
                  up ? WiFi.RSSI() : 0);
  }
}
