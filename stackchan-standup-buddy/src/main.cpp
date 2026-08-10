// StackChan (M5Stack CoreS3) — stand-up reminder buddy.
//
// Every 30 minutes the robot shakes its head, sings a melody and shows a
// reminder to stand up and look out the window. Acknowledge ("知道了") by
// patting the head (IMU jolt), tapping the screen, or touching a body zone;
// otherwise the show ends by itself after REMINDER_DURATION_MS.
//
// IDLE screen: cropped cat face + a big countdown to the next reminder.
//
// Animations: face expressions (fullscreen cat GIFs from LittleFS, authored as
// Lottie scenes and pre-rendered to 320x240 GIF — see gif_face.cpp), servo head
// movements, and 12 RGB LED ring patterns — all non-blocking, millis()-driven.
//
// Servo hardware (StackChan BSP):
//   Yaw servo (ID 1): 360-degree continuous-rotation, -1280..+1280 position
//     range (= -128..+128 degrees). PWM/velocity mode available via rotateYaw().
//   Pitch servo (ID 2): feedback servo, 0..900 range (= 0..90 degrees, 0=down).

#include <M5Unified.h>
#include <M5StackChan.h>
#include "motion.h"
#include "gif_face.h"

static AgentState g_agentState = STATE_IDLE;

// ---- Stand-up reminder -------------------------------------------------------
static constexpr uint32_t REMINDER_INTERVAL_MS = 30UL * 60 * 1000;  // every 30 min
static constexpr uint32_t REMINDER_DURATION_MS = 8000;              // shake+sing max length
static uint32_t g_nextReminderMs = 0;

// Melody: 《两只老虎》 first verse, played note-by-note via M5.Speaker.tone().
struct Note { uint16_t freq; uint16_t ms; };
static const Note REMINDER_MELODY[] = {
    {262, 300}, {294, 300}, {330, 300}, {262, 300},   // C D E C
    {262, 300}, {294, 300}, {330, 300}, {262, 300},   // C D E C
    {330, 300}, {349, 300}, {392, 600},               // E F G
    {330, 300}, {349, 300}, {392, 600},               // E F G
};
static const int REMINDER_MELODY_LEN = sizeof(REMINDER_MELODY) / sizeof(REMINDER_MELODY[0]);
static int      g_noteIndex   = 0;
static uint32_t g_noteStartMs = 0;
static bool     g_melodyOn    = false;

static void melodyStart() {
    g_noteIndex   = 0;
    g_noteStartMs = 0;
    g_melodyOn    = true;
}

static void melodyTick() {
    if (!g_melodyOn) return;
    uint32_t now = millis();
    if (g_noteStartMs == 0) {   // first note
        g_noteStartMs = now;
        M5.Speaker.tone(REMINDER_MELODY[0].freq, REMINDER_MELODY[0].ms);
        return;
    }
    if (now - g_noteStartMs >= REMINDER_MELODY[g_noteIndex].ms) {
        g_noteIndex++;
        g_noteStartMs = now;
        if (g_noteIndex >= REMINDER_MELODY_LEN) {
            g_melodyOn = false;
            return;
        }
        M5.Speaker.tone(REMINDER_MELODY[g_noteIndex].freq,
                        REMINDER_MELODY[g_noteIndex].ms);
    }
}

// ---- Colours / Layout ---------------------------------------------------------
static constexpr uint16_t TEXT_BG   = 0x0000;  // bottom panel: black
static constexpr int SCREEN_W       = 320;
static constexpr int TEXT_AREA_Y    = 148;    // bottom panel starts here
static constexpr int TEXT_AREA_H    = 92;
static constexpr int FACE_YOFFSET   = -30;    // shift GIF face up so the eyes
                                              // sit centred above the 92px panel

// ---- Animation Timing Globals -----------------------------------------------
static uint32_t g_stateEntryMs = 0;

// ---- Text Overlay (shown in the bottom panel during REMINDER) ---------------
static String g_overlayLine1;
static String g_overlayLine2;
static uint16_t g_overlayColor2 = TFT_WHITE;

// ============================================================================
//  Bottom Panel (bottom 92px; GIF face owns everything above it).
//  IDLE shows the stand-up countdown; REMINDER shows overlay text.
// ============================================================================

static void clearTextArea() {
    M5.Display.fillRect(0, TEXT_AREA_Y, SCREEN_W, TEXT_AREA_H, TEXT_BG);
}

// Countdown panel: big 7-seg mm:ss + label. Redraws only when the displayed
// second changes; g_lastPanelSec is reset to force a redraw on entry to IDLE.
static uint32_t g_lastPanelSec = 0xFFFFFFFF;

static void drawCountdownPanel() {
    uint32_t now = millis();
    uint32_t remain = ((int32_t)(g_nextReminderMs - now) > 0) ? (g_nextReminderMs - now) : 0;
    uint32_t sec = (remain + 999) / 1000;   // ceil: shows 30:00 down to 00:01
    if (sec == g_lastPanelSec) return;
    g_lastPanelSec = sec;

    clearTextArea();

    char buf[8];
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    M5.Display.setFont(&fonts::Font7);      // 7-seg digits, ~48px tall
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.drawString(buf, SCREEN_W / 2, TEXT_AREA_Y + 12);

    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.drawString("后提醒站立 · 看窗外", SCREEN_W / 2, TEXT_AREA_Y + 64);

    M5.Display.setTextDatum(TL_DATUM);
}

// ============================================================================
//  State Management & Animation Driver
// ============================================================================

static void setAgentState(AgentState next) {
    if (g_agentState == next) return;
    g_agentState   = next;
    g_stateEntryMs = millis();
    motionSetState(next);
    gifFaceSetState(next);
    if (next != STATE_REPLYING && next != STATE_ERROR) {
        g_overlayLine1  = "";
        g_overlayLine2  = "";
    }
}

static void animateAll(AgentState state) {
    gifFaceTick();
    motionTick(state);

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

    if (state == STATE_IDLE) drawCountdownPanel();
}

// ============================================================================
//  UI helpers
// ============================================================================

static void showLines(const char* l1, const char* l2, uint16_t color2 = TFT_WHITE) {
    g_overlayLine1  = l1 ? l1 : "";
    g_overlayLine2  = l2 ? l2 : "";
    g_overlayColor2 = color2;
}

static void showIdle() {
    setAgentState(STATE_IDLE);
    // IDLE panel is the countdown — no overlay text. Force a panel redraw.
    g_overlayLine1  = "";
    g_overlayLine2  = "";
    g_lastPanelSec  = 0xFFFFFFFF;
}

// ============================================================================
//  Stand-up reminder: shake head + sing + on-screen text every 30 minutes.
//  Pat the head, tap the screen, or touch a body zone to acknowledge —
//  "知道了" — which stops the show immediately; otherwise it ends after
//  REMINDER_DURATION_MS.
// ============================================================================

// Head-pat detection via the CoreS3 IMU. A pat is a sharp acceleration jolt
// (~1g jerk between samples); the reminder's own head shake is two orders of
// magnitude smaller (~0.02g centripetal at the IMU), so a plain jerk
// threshold separates them cleanly. Only sampled during the reminder window.
static bool headPatted() {
    if (!M5.Imu.isEnabled()) return false;
    static float    lastMag    = 1.0f;
    static uint32_t lastFireMs = 0;
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);          // values in g
    float mag  = sqrtf(ax * ax + ay * ay + az * az);
    float jerk = fabsf(mag - lastMag);
    lastMag = mag;
    uint32_t now = millis();
    if (jerk > 0.8f && now - lastFireMs > 300) {   // tune threshold on device
        lastFireMs = now;
        return true;
    }
    return false;
}

static bool dismissTouched() {
    if (M5.Touch.getDetail().wasPressed()) return true;
    if (headPatted()) return true;           // 摸头
    const auto& intensities = M5StackChan.TouchSensor.getIntensities();
    for (int z = 0; z < 3; z++) if (intensities[z] > 0) return true;
    return false;
}

// Wait until nothing is being touched, so the acknowledging touch doesn't
// linger into the next reminder window and instantly dismiss it.
static void waitForRelease() {
    while (true) {
        animateAll(g_agentState);
        bool touching = M5.Touch.getDetail().isPressed();
        const auto& intensities = M5StackChan.TouchSensor.getIntensities();
        for (int z = 0; z < 3; z++) touching = touching || (intensities[z] > 0);
        if (!touching) break;
        delay(10);
    }
}

static void fireReminder() {
    setAgentState(STATE_REMINDER);
    // showLines AFTER setAgentState so the overlay survives the state
    // transition (setAgentState clears it for non-REPLYING/ERROR states).
    showLines("该起来活动一下啦", "站起来看窗外 · 摸头确认", TFT_GREEN);
    melodyStart();
    uint32_t endTime = millis() + REMINDER_DURATION_MS;
    while (millis() < endTime) {
        animateAll(g_agentState);
        melodyTick();
        if (dismissTouched()) break;   // pat / tap = "知道了"
        delay(10);
    }
    g_melodyOn = false;
    M5.Speaker.stop();                 // cut the sounding note immediately
    waitForRelease();
    showIdle();                        // back to the countdown panel
}

// ============================================================================
//  setup / loop
// ============================================================================

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    motionInit();

    Serial.begin(115200);
    M5.Speaker.setVolume(40);

    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextWrap(true);

    gifFaceInit();
    gifFaceSetTextBand(TEXT_AREA_Y);   // GIF keeps out of the bottom panel
    gifFaceSetYOffset(FACE_YOFFSET);   // face shifted up, eyes above the panel
    gifFaceSetState(STATE_IDLE);       // g_agentState already IDLE; open GIF directly

    clearTextArea();
    showIdle();

    g_nextReminderMs = millis() + REMINDER_INTERVAL_MS;
}

void loop() {
    // -- Stand-up reminder --
    if ((int32_t)(millis() - g_nextReminderMs) >= 0) {
        fireReminder();
        g_nextReminderMs = millis() + REMINDER_INTERVAL_MS;
    }

    animateAll(g_agentState);

    delay(10);
}
