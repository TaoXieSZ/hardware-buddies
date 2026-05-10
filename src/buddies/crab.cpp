#include "../buddy.h"
#include "../buddy_common.h"
#include <M5Unified.h>
#include <string.h>

extern M5Canvas spr;

namespace crab {

// Claude crab mascot — orange shell, eye stalks, claws to the sides, six
// legs below. ASCII art only (no extended UTF-8) so the renderer's bitmap
// font handles every glyph cleanly.
//
// Layout: 5 lines tall, ~14 chars wide, centered around BUDDY_X_CENTER.
// Row 0: eye stalk tips                   o    o
// Row 1: stalks                           |    |
// Row 2: face (mood goes here)          ( -.- )
// Row 3: claws + body                  d( o o )b
// Row 4: legs                            '/|||\\'

// ─── SLEEP ───  eyes shut, slow breathing, no claws raised
static void doSleep(uint32_t t) {
  static const char* const REST_A[5] = {
    "              ",
    "              ",
    "   .zZz..     ",
    "  ( -.- )     ",
    "   '/|||\\'   ",
  };
  static const char* const REST_B[5] = {
    "              ",
    "              ",
    "   ..zZ.      ",
    "  ( -.- )_    ",
    "   '/|||\\'   ",
  };
  const char* const* P = (t / 8) & 1 ? REST_B : REST_A;
  buddyPrintSprite(P, 5, 0, BUDDY_DIM);
}

// ─── IDLE ───  alert, eyes peering, claws relaxed by sides
static void doIdle(uint32_t t) {
  static const char* const POSE_A[5] = {
    "    o    o    ",
    "    |    |    ",
    "   ( o.o )    ",
    " d-( <_> )-b  ",
    "   '/|||\\'   ",
  };
  static const char* const POSE_B[5] = {
    "    o    o    ",
    "    |    |    ",
    "   ( -.o )    ",  // wink
    " d-( <_> )-b  ",
    "   '/|||\\'   ",
  };
  static const char* const POSE_C[5] = {
    "    o    o    ",
    "    |    |    ",
    "   ( o.- )    ",
    " d-( <_> )-b  ",
    "   '/|||\\'   ",
  };
  const char* const* SEQ[] = { POSE_A, POSE_A, POSE_A, POSE_B, POSE_A, POSE_C };
  const char* const* P = SEQ[(t / 6) % (sizeof(SEQ)/sizeof(SEQ[0]))];
  buddyPrintSprite(P, 5, 0, 0xDBAA);  // Claude orange (#D97757 → RGB565)
}

// ─── BUSY (thinking) ───  eyes closed, claws snipping, looks studious
static void doBusy(uint32_t t) {
  static const char* const SNIP_A[5] = {
    "    o    o    ",
    "    |    |    ",
    "   ( -.- )    ",
    " <d( ___ )b>  ",   // claws closed, calm
    "   '/|||\\'   ",
  };
  static const char* const SNIP_B[5] = {
    "    o    o    ",
    "    |    |    ",
    "   ( -.- )    ",
    " <D( ___ )B>  ",   // claws open (D / B)
    "   '/|||\\'   ",
  };
  const char* const* P = (t / 4) & 1 ? SNIP_B : SNIP_A;
  buddyPrintSprite(P, 5, 0, 0xDBAA);
  // little "thinking" dots above
  if (((t / 4) & 3) == 0) {
    buddySetColor(BUDDY_DIM);
    buddySetCursor(BUDDY_X_CENTER + 32, BUDDY_Y_BASE - 4);
    buddyPrint(".");
  } else if (((t / 4) & 3) == 1) {
    buddySetColor(BUDDY_DIM);
    buddySetCursor(BUDDY_X_CENTER + 32, BUDDY_Y_BASE - 4);
    buddyPrint("..");
  } else if (((t / 4) & 3) == 2) {
    buddySetColor(BUDDY_DIM);
    buddySetCursor(BUDDY_X_CENTER + 32, BUDDY_Y_BASE - 4);
    buddyPrint("...");
  }
}

// ─── ATTENTION ───  claws raised straight up — "wait, listen!"
static void doAttention(uint32_t t) {
  // Two-frame attention: claws fully raised, then slightly lower
  static const char* const RAISE_HI[5] = {
    "  Y o  o Y    ",
    "  | |  | |    ",
    "   ( O O )    ",
    "  ( |   | )   ",
    "   '/|||\\'   ",
  };
  static const char* const RAISE_LO[5] = {
    "  y o  o y    ",
    "  | |  | |    ",
    "   ( O O )    ",
    "  ( |   | )   ",
    "   '/|||\\'   ",
  };
  const char* const* P = (t / 3) & 1 ? RAISE_LO : RAISE_HI;
  buddyPrintSprite(P, 5, 0, BUDDY_YEL);
  // Exclamation mark popping in/out above
  if ((t / 3) & 1) {
    buddySetColor(BUDDY_RED);
    buddySetCursor(BUDDY_X_CENTER - 2, BUDDY_Y_BASE - 12);
    buddyPrint("!");
  }
}

// ─── CELEBRATE ───  bouncing + claws waving high, confetti above
static void doCelebrate(uint32_t t) {
  static const char* const WAVE_L[5] = {
    "    o    o    ",
    "    |    |    ",
    "   ( ^.^ )    ",
    " /( <_> )b    ",  // left claw raised
    "   '/|||\\'   ",
  };
  static const char* const WAVE_R[5] = {
    "    o    o    ",
    "    |    |    ",
    "   ( ^.^ )    ",
    "  d( <_> )\\  ",  // right claw raised
    "   '/|||\\'   ",
  };
  // Bounce: yOffset oscillates -2 .. 0
  int bounce = -((t / 2) & 1) * 2;
  const char* const* P = (t / 2) & 1 ? WAVE_R : WAVE_L;
  buddyPrintSprite(P, 5, bounce, BUDDY_GREEN);
  // Confetti above
  static const char* CONFETTI = "*o.*o.*";
  for (int i = 0; i < 5; i++) {
    int x = BUDDY_X_CENTER - 28 + i * 12 + ((t + i) & 3);
    int y = BUDDY_Y_BASE - 18 - ((t + i * 3) & 7);
    buddySetColor(i & 1 ? BUDDY_YEL : BUDDY_HEART);
    buddySetCursor(x, y);
    char c[2] = { CONFETTI[(t + i) % 7], 0 };
    buddyPrint(c);
  }
}

// ─── DIZZY ───  spiral eyes (X), wobbling side to side
static void doDizzy(uint32_t t) {
  static const char* const WOBBLE_L[5] = {
    "    x    x    ",
    "    |    |    ",
    "   ( @_@ )    ",
    "  ~( <_> )    ",
    "  ~/|||\\     ",
  };
  static const char* const WOBBLE_R[5] = {
    "    x    x    ",
    "    |    |    ",
    "   ( @_@ )    ",
    "   ( <_> )~   ",
    "    /|||\\~   ",
  };
  int xShift = ((t / 2) & 1) ? 2 : -2;
  const char* const* P = (t / 2) & 1 ? WOBBLE_R : WOBBLE_L;
  buddyPrintSprite(P, 5, 0, BUDDY_PURPLE, xShift);
}

// ─── HEART ───  blushing, hearts floating up
static void doHeart(uint32_t t) {
  static const char* const BLUSH[5] = {
    "    o    o    ",
    "    |    |    ",
    "   ( ^_^ )    ",
    " d-( <3> )-b  ",   // heart in body
    "   '/|||\\'   ",
  };
  buddyPrintSprite(BLUSH, 5, 0, BUDDY_HEART);
  // Floating hearts rising
  buddySetColor(BUDDY_HEART);
  for (int i = 0; i < 4; i++) {
    int phase = (t + i * 5) % 18;
    int y = BUDDY_Y_BASE - 8 - phase;
    if (y < -2) continue;
    int x = BUDDY_X_CENTER - 18 + i * 10 + ((phase >> 1) & 1) * 2;
    buddySetCursor(x, y);
    buddyPrint("v");   // tiny heart-shape lookalike
  }
}

}  // namespace crab

extern const Species CRAB_SPECIES = {
  "crab",
  0xDBAA,   // Claude orange (#D97757)
  { crab::doSleep, crab::doIdle, crab::doBusy, crab::doAttention,
    crab::doCelebrate, crab::doDizzy, crab::doHeart }
};
