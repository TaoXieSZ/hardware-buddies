// StackChan (M5Stack CoreS3) — USB-serial agent client for personal-agent-runtime.
//
// No WiFi. The device is on your desk plugged into the Mac over USB anyway, so it
// talks to a Mac-side relay (tools/relay.py) over the USB serial line; the relay
// forwards to the local voice gateway (localhost:60630) and returns the reply.
// This sidesteps corporate WiFi entirely (no 802.1X / captive portal to fight).
//
// Wire protocol (line-based, newline-terminated, 115200 baud):
//   device -> Mac : "@ASK <prompt>\n"
//   Mac -> device : "@REPLY <text>\n"   (newlines in text are replaced by the relay)
//                   "@ERR <message>\n"
// Any line without an "@"-marker prefix is treated as noise and ignored on both
// ends (boot logs, etc.), so the debug console and the protocol can share the UART.
//
// Interaction: CoreS3 has no physical buttons — tap ANYWHERE on the touchscreen to
// send the current prompt; after each send the next preset is selected so repeated
// taps cycle the questions.
//
// Animations: face expressions (M5GFX primitives on LCD), servo head movements,
// and 12 RGB LED ring patterns — all non-blocking, driven by millis() timers.
//
// Servo hardware (StackChan BSP):
//   Yaw servo (ID 1): 360-degree continuous-rotation, -1280..+1280 position
//     range (= -128..+128 degrees). PWM/velocity mode available via rotateYaw().
//     We use position-mode moves for expressive sweeps and PWM/velocity mode
//     for joyful spins (REPLYING state).
//   Pitch servo (ID 2): feedback servo, 0..900 range (= 0..90 degrees, 0=down).

#include <M5Unified.h>
#include <M5StackChan.h>
#include <esp_random.h>

// ---- Prompts ----------------------------------------------------------------
static const char* PROMPTS[] = {
    "现在几点了？",
    "看一下项目状态",
    "给我讲个短笑话",
};
static const int NUM_PROMPTS = sizeof(PROMPTS) / sizeof(PROMPTS[0]);
static int g_promptIndex = 0;

static const unsigned long REPLY_TIMEOUT_MS = 60000;  // agent can take ~10-20s

// ---- Agent States -----------------------------------------------------------
enum AgentState {
    STATE_IDLE,
    STATE_THINKING,
    STATE_REPLYING,
    STATE_ERROR
};
static AgentState g_agentState = STATE_IDLE;

// ---- Colour Palette (RGB565) -------------------------------------------------
static constexpr uint16_t FACE_BG      = 0x10A2;  // dark grey background
static constexpr uint16_t FACE_WHITE   = 0xFFFF;  // pure white
static constexpr uint16_t FACE_BLACK   = 0x0000;  // pure black
static constexpr uint16_t FACE_GREY    = 0x7BEF;  // mid grey (eyelid, border)
static constexpr uint16_t FACE_PINK    = 0xF9BA;  // blush pink
static constexpr uint16_t FACE_RED     = 0xF800;  // error red
static constexpr uint16_t FACE_CYAN    = 0x07FF;  // sparkle/highlight
static constexpr uint16_t FACE_YELLOW  = 0xFFE0;  // ear inner
static constexpr uint16_t TEXT_BG      = 0x0000;  // text area: black

// ---- Layout -----------------------------------------------------------------
static constexpr int SCREEN_W       = 320;
static constexpr int SCREEN_H       = 240;
static constexpr int FACE_AREA_H    = 180;    // top 180px = face canvas
static constexpr int FACE_AREA_MID  = FACE_AREA_H / 2;   // 90
static constexpr int TEXT_AREA_Y    = 181;    // text band starts here
static constexpr int TEXT_AREA_H    = 59;

// --- Face frame ---
static constexpr int FRAME_L = 8,  FRAME_R = SCREEN_W - 8;
static constexpr int FRAME_T = 8,  FRAME_B = FACE_AREA_H - 8;

// --- Ears (robot cat-ears, triangles above frame) ---
static constexpr int EAR_LX = 60,  EAR_RX = 260, EAR_TY = 0, EAR_BY = 18;
// Outer triangle tip at top, base at FRAME_T level

// --- Eyes ---
static constexpr int EYE_LX     = 125, EYE_RX = 195, EYE_Y = 88;
static constexpr int EYE_R      = 20,  PUPIL_R = 8;

// --- Nose ---
static constexpr int NOSE_X = 160, NOSE_Y = 115;
static constexpr int NOSE_W = 8;

// --- Mouth ---
static constexpr int MOUTH_X = 160, MOUTH_Y = 140;

// --- Cheeks ---
static constexpr int CHEEK_LX = 95, CHEEK_RX = 225, CHEEK_Y = 108, CHEEK_R = 7;

// ---- Servo Patterns ---------------------------------------------------------
// Yaw servo is 360-degree continuous rotation, position range -1280..+1280.
// Pitch servo is 0..900 (0=down, 900=straight up). PITCH_BASELINE = 600 = 60deg.
//
// Each pattern step can be either a **position move** or a **rotation spin**:
//   rotateX=false: x is yaw angle (-1280..1280 tenths-of-deg), y is pitch delta
//                 from baseline. Motion.move(x, baseline+y, speed) is called.
//   rotateX=true:  x is rotation velocity (-1000..1000). Motion.rotateYaw(x)
//                 is called. The BSP switches the servo to PWM/velocity mode.
//                 The next non-rotate step switches it back to position mode.
//
// Sentinel (dwellMs==0 && speed==0): loop back to step 0.

static constexpr int16_t PITCH_BASELINE = 600;   // 60 degrees head-up

struct ServoStep {
    int16_t  x;         // yaw angle (position) or yaw velocity (rotation spin)
    int16_t  y;         // pitch delta from PITCH_BASELINE (ignored when rotateX)
    uint16_t speed;     // 0..1000 (for move() calls; ignored when rotateX)
    uint16_t dwellMs;   // ms to wait after issuing this step
    bool     rotateX;   // true → x is rotation velocity, use rotateYaw(x)
};

// --- IDLE: wide breathing sweeps with a slow rotation flourish every few cycles
// The 360-degree yaw can sweep ±20 degrees comfortably; the step table does a
// full left-to-right "looking around" motion with an occasional slow rotation
// segment so the head explores the full range.
static const ServoStep PAT_IDLE[] = {
    // Gentle sweep left -- use wider angles now
    { -200,  30, 120, 3000, false },   // look left + slight up, ~3s
    {    0,  20, 120, 2500, false },   // center, up
    {  200,  30, 120, 3000, false },   // look right + slight up, ~3s
    {    0,  20, 120, 2500, false },   // center, up
    // Slow rotation flourish -- uses full 360 capability
    {  250,   0,   0, 2000, true  },   // rotate CW for 2s (velocity 250)
    {    0,   0,   0,  500, false },   // pause after spin
    // Exhale: lower head slightly
    {    0, -15, 100, 3000, false },   // center, slight down
    { 0, 0, 0, 0, false }             // loop (~20s total cycle)
};

// --- THINKING: dramatic head tilt using full yaw and pitch range ---
// The thinking pose should look decidedly different from idle -- extreme yaw
// angles (50 degrees off-center) + high pitch (head tilted way up) reads
// as "pondering something profound."
static const ServoStep PAT_THINKING[] = {
    {  500, 180, 250, 3500, false },   // hard right + gaze way up (pitch 60+18=78deg)
    {    0, 200, 200, 1500, false },   // center, extremely up
    { -500, 180, 250, 3500, false },   // hard left + gaze way up
    {    0, 200, 200, 1500, false },   // center, extremely up
    { 0, 0, 0, 0, false }
};

// --- REPLYING: joyful spin dance + nod ---
// The BSP rotation (PWM/velocity mode) gives smooth continuous spinning. We
// alternate between a spin segment (CW, pause, CCW, pause) and a nodding
// segment. The step machine handles the mode switch transparently: rotateYaw()
// calls SwitchMode internally, and the next move() switches back.
static const ServoStep PAT_REPLYING[] = {
    // Spin segment -- joyful full rotation
    {  400,   0,   0, 1000, true  },   // spin CW at velocity 400 for 1s
    {    0,   0,   0,  200, false },   // brief pause (position-mode park at 0)
    { -400,   0,   0, 1000, true  },   // spin CCW at velocity 400 for 1s
    {    0,   0,   0,  200, false },   // brief pause
    // Nod segment
    {    0, 150, 300, 350, false },    // nod way up
    {    0, -20, 300, 350, false },    // nod down
    {    0, 150, 300, 350, false },
    {    0, -20, 300, 350, false },
    {    0,   0, 250, 600, false },    // settle at centre
    { 0, 0, 0, 0, false }
};

// --- ERROR: violent wide shake using the full speed and range ---
// ±300 = ±30 degrees at speed 600 is aggressive but well within the 360-degree
// servo's capability. Six fast swings make a clear "NO NO NO" head shake.
static const ServoStep PAT_ERROR[] = {
    {  300,  30, 600, 180, false },    // throw head right + slight up
    { -300,  30, 600, 180, false },    // throw head left
    {  300,  30, 600, 180, false },
    { -300,  30, 600, 180, false },
    {  300,  30, 600, 180, false },
    { -300,  30, 600, 180, false },
    {    0,   0, 400, 800, false },    // settle
    { 0, 0, 0, 0, false }
};
static const ServoStep* PATTERNS[] = {
    PAT_IDLE, PAT_THINKING, PAT_REPLYING, PAT_ERROR
};

// ---- Animation Timing Globals -----------------------------------------------
static uint32_t g_stateEntryMs   = 0;
static uint32_t g_lastFaceDrawMs = 0;
static uint32_t g_lastBlinkMs    = 0;    // start of current blink cycle (0 = not blinking)
static uint32_t g_nextBlinkMs    = 0;    // when next blink should fire
static uint32_t g_lastMouthMs    = 0;
static uint8_t  g_mouthShape     = 0;    // 0..4 for REPLYING

static uint32_t g_lastServoMs  = 0;
static uint8_t  g_servoStepIdx = 0;

static uint32_t g_lastLedMs    = 0;
static uint8_t  g_ledPos       = 0;
static bool     g_ledFlashOn   = false;

// ---- Text Overlay (shown in bottom band during REPLY / ERROR) ---------------
static String g_overlayLine1;
static String g_overlayLine2;
static uint16_t g_overlayColor2 = TFT_WHITE;

// ============================================================================
//  Drawing Helpers
// ============================================================================

static void clearFaceArea() {
    M5.Display.fillRect(0, 0, SCREEN_W, FACE_AREA_H, FACE_BG);
}
static void clearTextArea() {
    M5.Display.fillRect(0, TEXT_AREA_Y, SCREEN_W, TEXT_AREA_H, TEXT_BG);
}

static int16_t clampedPitch(int16_t base, int16_t delta) {
    int y = (int)base + (int)delta;
    if (y < 0)   y = 0;
    if (y > 900) y = 900;
    return (int16_t)y;
}

// ---- Shared face base: frame + ears + nose + cheeks --------------------------
// Called at the start of every face-draw function (after clearFaceArea).
static void drawFaceBase(int offsetY) {
    // --- Face frame (subtle rounded-rect border) ---
    M5.Display.fillRoundRect(FRAME_L, FRAME_T, FRAME_R - FRAME_L, FRAME_B - FRAME_T,
                             6, FACE_GREY);
    M5.Display.fillRoundRect(FRAME_L + 2, FRAME_T + 2, FRAME_R - FRAME_L - 4,
                             FRAME_B - FRAME_T - 4, 6, FACE_BG);

    // --- Robot cat-ears ---
    // Left ear: outer triangle (grey) + inner triangle (yellow)
    M5.Display.fillTriangle(EAR_LX, EAR_TY,
                            EAR_LX - 18, FRAME_T,
                            EAR_LX + 18, FRAME_T, FACE_GREY);
    M5.Display.fillTriangle(EAR_LX, EAR_TY + 3,
                            EAR_LX - 12, FRAME_T - 1,
                            EAR_LX + 12, FRAME_T - 1, FACE_YELLOW);
    // Right ear
    M5.Display.fillTriangle(EAR_RX, EAR_TY,
                            EAR_RX - 18, FRAME_T,
                            EAR_RX + 18, FRAME_T, FACE_GREY);
    M5.Display.fillTriangle(EAR_RX, EAR_TY + 3,
                            EAR_RX - 12, FRAME_T - 1,
                            EAR_RX + 12, FRAME_T - 1, FACE_YELLOW);

    // --- Nose (tiny triangle, centered) ---
    M5.Display.fillTriangle(NOSE_X, NOSE_Y + offsetY,
                            NOSE_X - NOSE_W/2, NOSE_Y + NOSE_W + offsetY,
                            NOSE_X + NOSE_W/2, NOSE_Y + NOSE_W + offsetY,
                            FACE_PINK);

    // --- Blush cheeks ---
    M5.Display.fillCircle(CHEEK_LX, CHEEK_Y + offsetY, CHEEK_R, FACE_PINK);
    M5.Display.fillCircle(CHEEK_RX, CHEEK_Y + offsetY, CHEEK_R, FACE_PINK);
    // Cheek highlights (tiny white dots)
    M5.Display.fillCircle(CHEEK_LX + 2, CHEEK_Y - 2 + offsetY, 2, FACE_WHITE);
    M5.Display.fillCircle(CHEEK_RX + 2, CHEEK_Y - 2 + offsetY, 2, FACE_WHITE);
}

// ---- Draw a single eye (left or right), with or without eyelid cover ----------
// eyelidPct: 0.0 = fully open, 1.0 = fully closed
static void drawEye(int cx, int cy, int r, int px, int py, float eyelidPct) {
    if (eyelidPct > 1.0f) eyelidPct = 1.0f;
    if (eyelidPct < 0.0f) eyelidPct = 0.0f;

    // Eye white
    M5.Display.fillCircle(cx, cy, r, FACE_WHITE);
    // Outline
    M5.Display.drawCircle(cx, cy, r, FACE_BLACK);

    // Upper eyelid line (small arc at top of eye)
    if (eyelidPct < 0.95f) {
        M5.Display.drawArc(cx, cy, r - 1, r, 200, 340, FACE_GREY);
    }

    if (eyelidPct > 0.0f) {
        int coverH = (int)((float)(r * 2 + 6) * eyelidPct);
        if (coverH > 0) {
            M5.Display.fillRect(cx - r - 3, cy - r - 3, r * 2 + 6, coverH, FACE_BG);
            M5.Display.drawCircle(cx, cy, r, FACE_BLACK);
            M5.Display.drawArc(cx, cy, r - 1, r, 200, 340, FACE_GREY);
        }
    }

    // Pupil (visible portion shrinks with eyelid)
    if (eyelidPct < 0.8f) {
        M5.Display.fillCircle(cx + px, cy + py, PUPIL_R, FACE_BLACK);
        // Two highlights (anime-style catchlights)
        M5.Display.fillCircle(cx + px + 3, cy + py - 4, 3, FACE_WHITE);
        M5.Display.fillCircle(cx + px - 2, cy + py - 2, 2, FACE_WHITE);
    }
}

// ---------------------------------------------------------------------------
//  IDLE face
// ---------------------------------------------------------------------------
static void drawIdleFace(float eyelidPct) {
    clearFaceArea();
    drawFaceBase(0);
    drawEye(EYE_LX, EYE_Y, EYE_R, 0, 0, eyelidPct);
    drawEye(EYE_RX, EYE_Y, EYE_R, 0, 0, eyelidPct);

    // --- Smile mouth (wide crescent) ---
    M5.Display.fillArc(MOUTH_X, MOUTH_Y, 24, 12, 0, 180, FACE_WHITE);
    M5.Display.fillArc(MOUTH_X, MOUTH_Y, 18, 6, 0, 180, FACE_BG);
}

// ---------------------------------------------------------------------------
//  THINKING face
// ---------------------------------------------------------------------------
static void drawThinkingFace(float bobY) {
    clearFaceArea();
    int by = (int)bobY;
    drawFaceBase(by);

    // --- Eyebrows: thick, angled lines ---
    for (int dx = -1; dx <= 1; dx++) {
        M5.Display.drawLine(EYE_LX - 22, EYE_Y - 28 + by + dx,
                            EYE_LX + 10, EYE_Y - 38 + by + dx, FACE_WHITE);
        M5.Display.drawLine(EYE_RX - 10, EYE_Y - 38 + by + dx,
                            EYE_RX + 22, EYE_Y - 28 + by + dx, FACE_WHITE);
    }

    // --- Eyes: pupils shifted up-right (looking up/thinking) ---
    drawEye(EYE_LX, EYE_Y + by, EYE_R, 4, -6, 0.0f);
    drawEye(EYE_RX, EYE_Y + by, EYE_R, 4, -6, 0.0f);

    // --- Pursed "thinking" mouth: small oval + two tiny vertical lines ---
    M5.Display.fillArc(MOUTH_X, MOUTH_Y + by, 5, 4, 0, 360, FACE_WHITE);
    M5.Display.drawLine(MOUTH_X - 8, MOUTH_Y - 7 + by, MOUTH_X - 8, MOUTH_Y - 2 + by, FACE_WHITE);
    M5.Display.drawLine(MOUTH_X + 8, MOUTH_Y - 7 + by, MOUTH_X + 8, MOUTH_Y - 2 + by, FACE_WHITE);

    // --- Thought-bubble dots ---
    uint32_t ms = millis();
    int dotCycle = (int)(((ms - g_stateEntryMs) / 400) % 4);
    if (dotCycle >= 1) M5.Display.fillCircle(145, 28 + by, 5, FACE_CYAN);
    if (dotCycle >= 2) M5.Display.fillCircle(160, 18 + by, 5, FACE_CYAN);
    if (dotCycle >= 3) M5.Display.fillCircle(175, 28 + by, 5, FACE_CYAN);
}

// ---------------------------------------------------------------------------
//  REPLYING face
// ---------------------------------------------------------------------------
static void drawReplyingFace(uint8_t mouthShape) {
    clearFaceArea();
    drawFaceBase(0);

    // 5 mouth shapes for smoother talking animation
    static constexpr int M_W[5] = { 20, 15, 10, 12, 18 };
    static constexpr int M_H[5] = { 14, 10,  5,  7, 12 };
    int mw = M_W[mouthShape % 5];
    int mh = M_H[mouthShape % 5];
    int mb = (mouthShape == 0) ? -3 : (mouthShape == 2) ? 3 : (mouthShape == 4) ? -1 : 0;

    // --- Eyes: large pupils + sparkle ---
    drawEye(EYE_LX, EYE_Y, EYE_R, 0, 0, 0.0f);
    M5.Display.fillCircle(EYE_LX, EYE_Y, 10, FACE_BLACK);
    M5.Display.fillCircle(EYE_LX + 3, EYE_Y - 4, 4, FACE_WHITE);
    M5.Display.fillCircle(EYE_LX - 4, EYE_Y - 5, 3, FACE_CYAN);

    drawEye(EYE_RX, EYE_Y, EYE_R, 0, 0, 0.0f);
    M5.Display.fillCircle(EYE_RX, EYE_Y, 10, FACE_BLACK);
    M5.Display.fillCircle(EYE_RX + 3, EYE_Y - 4, 4, FACE_WHITE);
    M5.Display.fillCircle(EYE_RX - 4, EYE_Y - 5, 3, FACE_CYAN);

    // --- Talking mouth ---
    M5.Display.fillArc(MOUTH_X, MOUTH_Y + mb, mw, mh, 0, 360, FACE_WHITE);
}

// ---------------------------------------------------------------------------
//  ERROR face
// ---------------------------------------------------------------------------
static void drawErrorFace() {
    clearFaceArea();
    drawFaceBase(0);

    // --- Red accent bars at upper corners ---
    M5.Display.fillRect(FRAME_L, FRAME_T, 30, 3, FACE_RED);
    M5.Display.fillRect(FRAME_R - 30, FRAME_T, 30, 3, FACE_RED);

    // --- X-shaped eyes: double lines for thickness ---
    for (int d = -1; d <= 1; d++) {
        M5.Display.drawLine(EYE_LX - 16 + d, EYE_Y - 16,
                            EYE_LX + 16 + d, EYE_Y + 16, FACE_RED);
        M5.Display.drawLine(EYE_LX + 16 + d, EYE_Y - 16,
                            EYE_LX - 16 + d, EYE_Y + 16, FACE_RED);
    }
    for (int d = -1; d <= 1; d++) {
        M5.Display.drawLine(EYE_RX - 16 + d, EYE_Y - 16,
                            EYE_RX + 16 + d, EYE_Y + 16, FACE_RED);
        M5.Display.drawLine(EYE_RX + 16 + d, EYE_Y - 16,
                            EYE_RX - 16 + d, EYE_Y + 16, FACE_RED);
    }

    // --- Frown mouth ---
    M5.Display.fillArc(MOUTH_X, MOUTH_Y + 10, 22, 10, 180, 360, FACE_RED);
    M5.Display.fillArc(MOUTH_X, MOUTH_Y + 10, 14, 4, 180, 360, FACE_BG);

    // --- Error blush (red tint) ---
    M5.Display.fillCircle(CHEEK_LX, CHEEK_Y, CHEEK_R, FACE_RED);
    M5.Display.fillCircle(CHEEK_RX, CHEEK_Y, CHEEK_R, FACE_RED);

    // --- Sweat drops (sliding down) ---
    uint32_t ms = millis();
    float dropPhase = fmodf((float)(ms - g_stateEntryMs) / 600.0f, 1.0f);
    int dropY1 = 40 + (int)(dropPhase * 40.0f);
    int dropY2 = 40 + (int)(fmodf(dropPhase + 0.33f, 1.0f) * 40.0f);

    auto drawDrop = [](int x, int y) {
        M5.Display.fillTriangle(x, y, x - 5, y + 10, x + 5, y + 10, FACE_CYAN);
    };
    drawDrop(265, dropY1);
    drawDrop(280, dropY2);
}

// ============================================================================
//  State Management & Animation Driver
// ============================================================================

static void setAgentState(AgentState next) {
    if (g_agentState == next) return;
    g_agentState   = next;
    g_stateEntryMs = millis();
    g_servoStepIdx = 0;
    g_lastServoMs  = 0;
    g_ledPos       = 0;
    g_lastLedMs    = 0;
    g_ledFlashOn   = false;
    g_lastMouthMs  = 0;
    g_mouthShape   = 0;
    if (next == STATE_IDLE) {
        g_lastBlinkMs = 0;
        g_nextBlinkMs = g_stateEntryMs + 3000 + esp_random() % 3000;
    }
    if (next != STATE_REPLYING && next != STATE_ERROR) {
        g_overlayLine1  = "";
        g_overlayLine2  = "";
    }
}

// ---- Servo step machine ----------------------------------------------------
// For position steps: calls Motion.move(x, pitch, speed).  The BSP handles
// servo mode switching internally -- if the previous step was a rotateYaw()
// (PWM/velocity mode), the next move() automatically switches back to position
// mode via SwitchMode() inside the ScsServo class.

static void stepServoPattern(AgentState state) {
    const ServoStep* pat = PATTERNS[(int)state];
    if (!pat) return;

    uint32_t now = millis();
    if (g_lastServoMs > 0 && now - g_lastServoMs < pat[g_servoStepIdx].dwellMs) {
        return;
    }

    const ServoStep& s = pat[g_servoStepIdx];
    if (s.dwellMs == 0 && s.speed == 0 && !s.rotateX) {
        // Sentinel: loop
        g_servoStepIdx = 0;
        g_lastServoMs  = 0;
        return;
    }

    if (s.rotateX) {
        // Rotation step: switch to PWM/velocity mode and rotate continuously
        // x is the velocity (-1000..+1000); BSP clamps and maps internally.
        M5StackChan.Motion.rotateYaw(s.x);
    } else {
        // Position step: standard move command
        M5StackChan.Motion.move(s.x, clampedPitch(PITCH_BASELINE, s.y), s.speed);
    }

    g_lastServoMs = now;
    g_servoStepIdx++;
}

// ---- LED step machine ------------------------------------------------------

static void stepLedPattern(AgentState state) {
    uint32_t now = millis();

    switch (state) {

    case STATE_IDLE: {
        if (now - g_lastLedMs < 80) return;
        g_lastLedMs = now;
        float t = (float)((now - g_stateEntryMs) % 3000) / 3000.0f;
        float b = (sinf(t * 2.0f * PI) * 0.4f + 0.6f) * 40.0f;
        for (int i = 0; i < 12; i++) {
            M5StackChan.setRgbColor(i, 0, 0, (uint8_t)b);
        }
        M5StackChan.refreshRgb();
        break;
    }

    case STATE_THINKING: {
        if (now - g_lastLedMs < 80) return;
        g_lastLedMs = now;
        for (int i = 0; i < 12; i++) {
            int dist = (i - g_ledPos + 12) % 12;
            if (dist == 0) {
                M5StackChan.setRgbColor(i, 80, 50, 0);
            } else if (dist <= 2) {
                uint8_t f = (uint8_t)(20 / dist);
                M5StackChan.setRgbColor(i, f, (uint8_t)(f * 5 / 8), 0);
            } else {
                M5StackChan.setRgbColor(i, 0, 0, 0);
            }
        }
        M5StackChan.refreshRgb();
        g_ledPos = (g_ledPos + 1) % 12;
        break;
    }

    case STATE_REPLYING: {
        if (now - g_lastLedMs < 250) return;
        g_lastLedMs = now;
        g_ledFlashOn = !g_ledFlashOn;
        uint8_t g = g_ledFlashOn ? 100 : 20;
        M5StackChan.showRgbColor(0, g, 0);
        break;
    }

    case STATE_ERROR: {
        if (now - g_lastLedMs < 300) return;
        g_lastLedMs = now;
        g_ledFlashOn = !g_ledFlashOn;
        uint8_t r = g_ledFlashOn ? 100 : 0;
        M5StackChan.showRgbColor(r, 0, 0);
        break;
    }
    }
}

// ---- Blink engine: returns eyelid position 0.0=open .. 1.0=closed ----------

static float computeBlink() {
    uint32_t now = millis();

    if (g_nextBlinkMs == 0) {
        g_nextBlinkMs = now + 3000 + esp_random() % 3000;
    }

    if (g_lastBlinkMs > 0) {
        uint32_t elapsed = now - g_lastBlinkMs;
        if (elapsed < 300) {
            float t = (float)elapsed / 300.0f;
            if (t < 0.33f)       return t / 0.33f;
            else if (t < 0.66f)  return 1.0f;
            else                 return 1.0f - (t - 0.66f) / 0.34f;
        } else {
            g_lastBlinkMs = 0;
            g_nextBlinkMs = now + 3000 + esp_random() % 3000;
            return 0.0f;
        }
    } else if (now >= g_nextBlinkMs) {
        g_lastBlinkMs = now;
        return 0.0f;
    }

    return 0.0f;
}

// ---- Face drawing dispatcher -------------------------------------------------

static void drawRobotFace(AgentState state) {
    uint32_t now = millis();
    if (g_lastFaceDrawMs > 0 && now - g_lastFaceDrawMs < 50) return;
    g_lastFaceDrawMs = now;

    switch (state) {
    case STATE_IDLE: {
        float eyelid = computeBlink();
        drawIdleFace(eyelid);
        break;
    }
    case STATE_THINKING: {
        float bobY = sinf((float)(now - g_stateEntryMs) / 400.0f) * 4.0f;
        drawThinkingFace(bobY);
        break;
    }
    case STATE_REPLYING: {
        if (g_lastMouthMs == 0 || now - g_lastMouthMs >= 180) {
            g_lastMouthMs = now;
            g_mouthShape = (g_mouthShape + 1) % 5;
        }
        drawReplyingFace(g_mouthShape);
        break;
    }
    case STATE_ERROR: {
        drawErrorFace();
        break;
    }
    }
}

// ---- Master tick ------------------------------------------------------------

static void animateAll(AgentState state) {
    drawRobotFace(state);
    stepServoPattern(state);
    stepLedPattern(state);

    static String lastL1;
    static String lastL2;
    static uint16_t lastCol2 = TFT_WHITE;
    if (g_overlayLine1 != lastL1 || g_overlayLine2 != lastL2 || g_overlayColor2 != lastCol2) {
        lastL1   = g_overlayLine1;
        lastL2   = g_overlayLine2;
        lastCol2 = g_overlayColor2;
        clearTextArea();
        M5.Display.setFont(&fonts::efontCN_16);
        M5.Display.setCursor(0, TEXT_AREA_Y + 2);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.setTextWrap(true);
        if (!g_overlayLine1.isEmpty()) M5.Display.println(g_overlayLine1);
        if (!g_overlayLine2.isEmpty()) {
            M5.Display.setTextColor(g_overlayColor2);
            M5.Display.println(g_overlayLine2);
            M5.Display.setTextColor(TFT_WHITE);
        }
    }
}

// ============================================================================
//  UI / Protocol
// ============================================================================

static void beep() { M5.Speaker.tone(1000, 150); }

static void showLines(const char* l1, const char* l2, uint16_t color2 = TFT_WHITE) {
    g_overlayLine1  = l1 ? l1 : "";
    g_overlayLine2  = l2 ? l2 : "";
    g_overlayColor2 = color2;
}

static void showPrompt() {
    setAgentState(STATE_IDLE);
    char hint[64];
    snprintf(hint, sizeof(hint), "[%d/%d] 点屏幕发送", g_promptIndex + 1, NUM_PROMPTS);
    g_overlayLine1  = "准备好了 — 点屏幕\n";
    g_overlayLine1 += String(hint) + "\n";
    g_overlayLine1 += PROMPTS[g_promptIndex];
    g_overlayLine2  = "";
}

static String readProtocolLine(uint32_t timeoutMs) {
    String line;
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        M5StackChan.update();
        animateAll(g_agentState);
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n') {
                line.trim();
                if (line.startsWith("@")) return line;
                line = "";
            } else if (c != '\r') {
                line += c;
                if (line.length() > 2048) line.remove(0, 1024);
            }
        }
        delay(5);
    }
    return "";
}

static void sendTurn(const char* prompt) {
    setAgentState(STATE_THINKING);
    showLines("思考中…", nullptr);
    beep();

    Serial.print("@ASK ");
    Serial.println(prompt);

    String line = readProtocolLine(REPLY_TIMEOUT_MS);

    if (line.startsWith("@REPLY ")) {
        String reply = line.substring(7);
        setAgentState(STATE_REPLYING);
        showLines(reply.c_str(), nullptr);
        beep();
        uint32_t endTime = millis() + 3000;
        while (millis() < endTime) { M5StackChan.update(); animateAll(g_agentState); delay(10); }

    } else if (line.startsWith("@ERR ")) {
        setAgentState(STATE_ERROR);
        showLines("出错:", line.substring(5).c_str(), FACE_RED);
        uint32_t endTime = millis() + 2000;
        while (millis() < endTime) { M5StackChan.update(); animateAll(g_agentState); delay(10); }

    } else {
        setAgentState(STATE_ERROR);
        showLines("出错: 超时", "(中继没运行?)", FACE_RED);
        uint32_t endTime = millis() + 2000;
        while (millis() < endTime) { M5StackChan.update(); animateAll(g_agentState); delay(10); }
    }

    showPrompt();
}

// ============================================================================
//  setup / loop
// ============================================================================

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5StackChan.begin();
    M5StackChan.setServoPowerEnabled(true);
    M5StackChan.Motion.setAutoTorqueReleaseEnabled(true);
    M5StackChan.Motion.move(0, PITCH_BASELINE, 250);
    M5StackChan.showRgbColor(0, 0, 0);

    Serial.begin(115200);
    M5.Speaker.setVolume(40);

    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextWrap(true);

    g_nextBlinkMs = millis() + 3000 + esp_random() % 3000;

    clearFaceArea();
    clearTextArea();
    showPrompt();
}

void loop() {
    M5StackChan.update();
    animateAll(g_agentState);

    auto touch = M5.Touch.getDetail();
    if (touch.wasPressed()) {
        sendTurn(PROMPTS[g_promptIndex]);
        g_promptIndex = (g_promptIndex + 1) % NUM_PROMPTS;
        showPrompt();
    }

    const auto& intensities = M5StackChan.TouchSensor.getIntensities();
    static bool wasTouched[3] = {false, false, false};
    for (int z = 0; z < 3; z++) {
        bool nowTouched = (intensities[z] > 0);
        if (nowTouched && !wasTouched[z]) {
            if (z == 1) {
                sendTurn(PROMPTS[g_promptIndex]);
                g_promptIndex = (g_promptIndex + 1) % NUM_PROMPTS;
                showPrompt();
            } else if (z == 2) {
                g_promptIndex = (g_promptIndex + 1) % NUM_PROMPTS;
                showPrompt();
            } else {
                g_promptIndex = (g_promptIndex - 1 + NUM_PROMPTS) % NUM_PROMPTS;
                showPrompt();
            }
        }
        wasTouched[z] = nowTouched;
    }

    if (M5StackChan.TouchSensor.wasSwipedForward()) {
        g_promptIndex = (g_promptIndex + 1) % NUM_PROMPTS;
        showPrompt();
    }
    if (M5StackChan.TouchSensor.wasSwipedBackward()) {
        g_promptIndex = (g_promptIndex - 1 + NUM_PROMPTS) % NUM_PROMPTS;
        showPrompt();
    }

    delay(10);
}
