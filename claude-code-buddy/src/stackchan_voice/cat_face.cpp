#include "cat_face.h"

#include <M5Unified.h>

#include "../stackchan/character_chan.h"   // CharState 枚举

namespace {

// ---- 布局：经典 StackChan 特写风——整屏即脸，只画大眼+嘴 ----
// （2026-07-19 用户定稿：不画完整猫头，白毛当背景，黑白斑从屏幕边缘溢出）
constexpr int SUB_H = 46;
constexpr int SUB_Y = 240 - SUB_H;
constexpr int SUB_W = 320;
constexpr int EYE_LX = 100, EYE_RX = 225, EYE_Y = 78;
constexpr int MOUTH_X = 160, MOUTH_Y = 148;

// ---- 配色（黑白美短，同 demo） ----
uint16_t C_BG, C_HEAD, C_PATCH, C_MASK, C_PINK_BLUSH, C_EYE, C_WHITE,
    C_WHISKER, C_MOUTH, C_TONGUE;

void initColors() {
  auto c = [](uint8_t r, uint8_t g, uint8_t b) { return M5.Lcd.color565(r, g, b); };
  C_BG         = c(0x0b, 0x0e, 0x12);   // 字幕带底色
  C_HEAD       = c(0xFC, 0xFC, 0xF8);   // 白毛=全屏背景
  C_PATCH      = c(0x3E, 0x41, 0x48);   // 深灰斑（屏幕边缘溢出）
  C_MASK       = c(0xD6, 0xD8, 0xDC);   // 右眼淡灰眼罩斑
  C_PINK_BLUSH = c(0xF2, 0xA8, 0xB0);
  C_EYE        = c(0x46, 0x28, 0x1C);   // 豆豆眼深棕
  C_WHITE      = c(0xFF, 0xFF, 0xFF);
  C_WHISKER    = c(0xC9, 0xCD, 0xD2);
  C_MOUTH      = c(0x5B, 0x32, 0x26);
  C_TONGUE     = c(0xF0, 0x8A, 0x7A);
}

uint8_t g_state = CHAR_SLEEP;
bool    g_talking = false;
bool    g_blinking = false;
uint32_t g_blink_ms = 0;
uint32_t g_mouth_ms = 0;

M5Canvas g_sub_spr(&M5.Lcd);
bool     g_sub_ok = false;
char     g_subtitle[384] = "";
int      g_sub_x = SUB_W;
int      g_sub_text_w = 0;
uint32_t g_sub_ms = 0;

// ---- 部件（特写比例） ----

// 眼区局部重绘：左眼底色是白毛，右眼底色是眼罩斑
void drawEyes(bool closed, bool big, bool up) {
  int y = EYE_Y - (up ? 10 : 0);
  M5.Lcd.fillCircle(EYE_LX, EYE_Y - 5, 40, C_HEAD);
  M5.Lcd.fillEllipse(EYE_RX, EYE_Y - 4, 62, 50, C_MASK);
  if (closed) {
    // 眯眯眼 ∩：粗上半圆环
    M5.Lcd.fillArc(EYE_LX, y + 16, 22, 30, 180, 360, C_EYE);
    M5.Lcd.fillArc(EYE_RX, y + 16, 22, 30, 180, 360, C_EYE);
  } else {
    int r = big ? 34 : 30;
    M5.Lcd.fillCircle(EYE_LX, y, r, C_EYE);
    M5.Lcd.fillCircle(EYE_RX, y, r, C_EYE);
    M5.Lcd.fillCircle(EYE_LX - 10, y - 10, 10, C_WHITE);
    M5.Lcd.fillCircle(EYE_RX - 10, y - 10, 10, C_WHITE);
    M5.Lcd.fillCircle(EYE_LX + 11, y + 8, 5, C_WHITE);
    M5.Lcd.fillCircle(EYE_RX + 11, y + 8, 5, C_WHITE);
  }
}

// 嘴区局部重绘。openness 0=ω，1..20=张嘴高度
void drawMouth(int openness) {
  M5.Lcd.fillRect(MOUTH_X - 40, MOUTH_Y - 18, 80, 46, C_HEAD);
  if (openness <= 0) {
    // 大号 ω：两个下半圆环并排
    M5.Lcd.fillArc(MOUTH_X - 14, MOUTH_Y, 9, 14, 0, 180, C_EYE);
    M5.Lcd.fillArc(MOUTH_X + 14, MOUTH_Y, 9, 14, 0, 180, C_EYE);
  } else {
    int h = openness;
    M5.Lcd.fillEllipse(MOUTH_X, MOUTH_Y + 6, 18, h, C_MOUTH);
    if (h >= 8) M5.Lcd.fillEllipse(MOUTH_X, MOUTH_Y + 6 + h / 2, 10, 4, C_TONGUE);
  }
}

void drawFull() {
  M5.Lcd.startWrite();
  M5.Lcd.fillRect(0, 0, SUB_W, SUB_Y, C_HEAD);
  // 花色斑从屏幕边缘溢出（特写视角只见一角）
  M5.Lcd.fillEllipse(28, -8, 96, 46, C_PATCH);     // 左上深斑
  M5.Lcd.fillEllipse(305, -12, 70, 38, C_PATCH);   // 右上小斑
  M5.Lcd.fillEllipse(EYE_RX, EYE_Y - 4, 62, 50, C_MASK);  // 右眼眼罩斑
  // 腮红 + 须（贴屏幕两侧）
  M5.Lcd.fillEllipse(46, 134, 26, 13, C_PINK_BLUSH);
  M5.Lcd.fillEllipse(274, 134, 26, 13, C_PINK_BLUSH);
  M5.Lcd.drawLine(0, 100, 38, 95, C_WHISKER);
  M5.Lcd.drawLine(0, 122, 40, 112, C_WHISKER);
  M5.Lcd.drawLine(319, 100, 281, 95, C_WHISKER);
  M5.Lcd.drawLine(319, 122, 279, 112, C_WHISKER);

  bool sleep = (g_state == CHAR_SLEEP);
  drawEyes(sleep, g_state == CHAR_ATTENTION, g_state == CHAR_BUSY);
  drawMouth(0);
  if (sleep) {
    M5.Lcd.setFont(&fonts::efontCN_16);
    M5.Lcd.setTextColor(C_PATCH, C_HEAD);
    M5.Lcd.drawString("z z", 284, 156);
  }
  M5.Lcd.endWrite();
}

void ensureSubSprite() {
  if (g_sub_ok) return;
  g_sub_spr.setColorDepth(16);
  g_sub_spr.setFont(&fonts::efontCN_24);
  g_sub_spr.setTextSize(1);
  if (g_sub_spr.createSprite(SUB_W, SUB_H)) g_sub_ok = true;
}

void drawSubtitleScroll() {
  uint32_t now = millis();
  if (now - g_sub_ms < 30) return;
  g_sub_ms = now;
  ensureSubSprite();
  if (!g_sub_ok) return;
  g_sub_spr.fillSprite(C_BG);
  if (g_subtitle[0]) {
    g_sub_spr.setTextColor(0xFFFF, C_BG);
    g_sub_spr.setTextDatum(middle_left);
    if (g_sub_text_w <= SUB_W - 8) {
      g_sub_spr.drawString(g_subtitle, 8, SUB_H / 2);   // 放得下就不滚
    } else {
      g_sub_spr.drawString(g_subtitle, g_sub_x, SUB_H / 2);
      g_sub_x -= 3;
      if (g_sub_x < -g_sub_text_w) g_sub_x = SUB_W;
    }
  }
  g_sub_spr.pushSprite(0, SUB_Y);
}

}  // namespace

void catFaceInit() {
  initColors();
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(C_BG);
  drawFull();
}

void catFaceSetState(uint8_t char_state) {
  if (char_state == g_state) return;
  g_state = char_state;
  g_blinking = false;
  if (g_state != CHAR_HEART) g_talking = false;
  drawFull();
}

void catFaceSetTalking(bool on) {
  if (on == g_talking) return;
  g_talking = on;
  if (!on) drawMouth(0);
}

void catFaceSetSubtitle(const char* text) {
  if (!text) text = "";
  strncpy(g_subtitle, text, sizeof(g_subtitle) - 1);
  g_subtitle[sizeof(g_subtitle) - 1] = 0;
  ensureSubSprite();
  g_sub_text_w = g_sub_ok ? g_sub_spr.textWidth(g_subtitle) : 0;
  g_sub_x = SUB_W;
}

void catFaceTick() {
  uint32_t now = millis();

  // 眨眼：常态/说话时每 ~3.4s 眯 130ms
  if (g_state == CHAR_IDLE || g_state == CHAR_HEART) {
    if (!g_blinking && now - g_blink_ms > 3400) {
      g_blinking = true;
      g_blink_ms = now;
      drawEyes(true, false, false);
    } else if (g_blinking && now - g_blink_ms > 130) {
      g_blinking = false;
      g_blink_ms = now;
      drawEyes(false, g_state == CHAR_ATTENTION, false);
    }
  }

  // 说话嘴型：90ms 换一次开口度
  if (g_talking && now - g_mouth_ms > 90) {
    g_mouth_ms = now;
    drawMouth(6 + (int)(esp_random() % 14));
  }

  drawSubtitleScroll();
}
