// RoverC Pro self-test for M5StickC Plus 1.1.
//
// Press BtnA to step through: x+ / y+ / z+ (each axis at 100, 1s) and
// servo open/close. Step stays put until next press so you can place the
// rover safely. Stop is implicit between steps.
//
// I2C: SDA=G0, SCL=G26, addr 0x38 (per upstream M5-RoverC).

#include <M5Unified.h>
#include "M5_RoverC.h"

static M5_RoverC roverc;

struct Step {
  const char* label;
  void (*run)();
};

static void s_stop()       { roverc.setSpeed(0, 0, 0); }
static void s_x_plus()     { roverc.setSpeed(100, 0, 0); }
static void s_y_plus()     { roverc.setSpeed(0, 100, 0); }
static void s_z_plus()     { roverc.setSpeed(0, 0, 100); }
static void s_x_minus()    { roverc.setSpeed(-100, 0, 0); }
static void s_y_minus()    { roverc.setSpeed(0, -100, 0); }
static void s_z_minus()    { roverc.setSpeed(0, 0, -100); }
static void s_servo_open() { roverc.setServoAngle(0, 180); roverc.setServoAngle(1, 180); }
static void s_servo_close(){ roverc.setServoAngle(0, 0);   roverc.setServoAngle(1, 0); }

static const Step kSteps[] = {
  {"stop",        s_stop},
  {"x = +100",    s_x_plus},
  {"x = -100",    s_x_minus},
  {"y = +100",    s_y_plus},
  {"y = -100",    s_y_minus},
  {"z = +100",    s_z_plus},
  {"z = -100",    s_z_minus},
  {"servo open",  s_servo_open},
  {"servo close", s_servo_close},
};
static constexpr size_t kNumSteps = sizeof(kSteps) / sizeof(kSteps[0]);

static size_t g_idx = 0;
static bool   g_begin_ok = false;

static void drawScreen() {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(2, 2);
  M5.Lcd.printf("RoverC %s", g_begin_ok ? "OK" : "FAIL");
  M5.Lcd.setCursor(2, 24);
  M5.Lcd.printf("[%u/%u]", (unsigned)(g_idx + 1), (unsigned)kNumSteps);
  M5.Lcd.setCursor(2, 46);
  M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Lcd.println(kSteps[g_idx].label);
  M5.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(2, 70);
  M5.Lcd.print("BtnA: next");
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[roverc] selftest boot");

  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(TFT_BLACK);

  g_begin_ok = roverc.begin();
  Serial.printf("[roverc] begin: %s (0x38 on SDA=G0 SCL=G26)\n",
                g_begin_ok ? "OK" : "NACK");
  s_stop();
  drawScreen();
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    g_idx = (g_idx + 1) % kNumSteps;
    Serial.printf("[roverc] step %u: %s\n", (unsigned)g_idx, kSteps[g_idx].label);
    kSteps[g_idx].run();
    drawScreen();
  }
  delay(10);
}
