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
// Animations: face expressions (fullscreen cat GIFs from LittleFS, authored as
// Lottie scenes and pre-rendered to 320x240 GIF — see gif_face.cpp), servo head
// movements, and 12 RGB LED ring patterns — all non-blocking, millis()-driven.
//
// Servo hardware (StackChan BSP):
//   Yaw servo (ID 1): 360-degree continuous-rotation, -1280..+1280 position
//     range (= -128..+128 degrees). PWM/velocity mode available via rotateYaw().
//     We use position-mode moves for expressive sweeps and PWM/velocity mode
//     for joyful spins (REPLYING state).
//   Pitch servo (ID 2): feedback servo, 0..900 range (= 0..90 degrees, 0=down).

#include <M5Unified.h>
#include <M5StackChan.h>
#include "motion.h"
#include "gif_face.h"
#include <esp_random.h>

static AgentState g_agentState = STATE_IDLE;

// ---- Prompts ----------------------------------------------------------------
static const char* PROMPTS[] = {
    "现在几点了？",
    "看一下项目状态",
    "给我讲个短笑话",
};
static const int NUM_PROMPTS = sizeof(PROMPTS) / sizeof(PROMPTS[0]);
static int g_promptIndex = 0;

static const unsigned long REPLY_TIMEOUT_MS = 60000;  // agent can take ~10-20s

// ---- Colours / Layout ---------------------------------------------------------
static constexpr uint16_t TEXT_BG   = 0x0000;  // text band: black
static constexpr int SCREEN_W       = 320;
static constexpr int TEXT_AREA_Y    = 181;    // text band starts here
static constexpr int TEXT_AREA_H    = 59;

// ---- Animation Timing Globals -----------------------------------------------
static uint32_t g_stateEntryMs = 0;


// ---- Re-entrancy guard ------------------------------------------------------
static bool g_inTurn = false;

// ---- Text Overlay (shown in bottom band during REPLY / ERROR) ---------------
static String g_overlayLine1;
static String g_overlayLine2;
static uint16_t g_overlayColor2 = TFT_WHITE;

// ============================================================================
//  Text Band (bottom 59px; GIF face owns everything above it)
// ============================================================================

static void clearTextArea() {
    M5.Display.fillRect(0, TEXT_AREA_Y, SCREEN_W, TEXT_AREA_H, TEXT_BG);
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

// Guarded against re-entrancy: only one turn can be in-flight at a time.
static void sendTurn(const char* prompt) {
    if (g_inTurn) return;
    g_inTurn = true;

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
        while (millis() < endTime) { animateAll(g_agentState); delay(10); }

    } else if (line.startsWith("@ERR ")) {
        setAgentState(STATE_ERROR);
        showLines("出错:", line.substring(5).c_str(), TFT_RED);
        uint32_t endTime = millis() + 2000;
        while (millis() < endTime) { animateAll(g_agentState); delay(10); }

    } else {
        setAgentState(STATE_ERROR);
        showLines("出错: 超时", "(中继没运行?)", TFT_RED);
        uint32_t endTime = millis() + 2000;
        while (millis() < endTime) { animateAll(g_agentState); delay(10); }
    }

    showPrompt();
    g_inTurn = false;
}

static void nextPreset() {
    g_promptIndex = (g_promptIndex + 1) % NUM_PROMPTS;
    showPrompt();
}
static void prevPreset() {
    g_promptIndex = (g_promptIndex - 1 + NUM_PROMPTS) % NUM_PROMPTS;
    showPrompt();
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
    gifFaceSetTextBand(TEXT_AREA_Y);   // GIF keeps out of the text band
    gifFaceSetState(STATE_IDLE);       // g_agentState already IDLE; open GIF directly

    clearTextArea();
    showPrompt();
}

void loop() {
    animateAll(g_agentState);

    // -- Primary: screen touch --
    auto touch = M5.Touch.getDetail();
    if (touch.wasPressed()) {
        sendTurn(PROMPTS[g_promptIndex]);
        g_promptIndex = (g_promptIndex + 1) % NUM_PROMPTS;
        // sendTurn() always ends with showPrompt(); no need to call it again.
    }

    // -- Bonus: body 3-zone touch --
    const auto& intensities = M5StackChan.TouchSensor.getIntensities();
    static bool wasTouched[3] = {false, false, false};
    for (int z = 0; z < 3; z++) {
        bool nowTouched = (intensities[z] > 0);
        if (nowTouched && !wasTouched[z]) {
            if (z == 1) {
                sendTurn(PROMPTS[g_promptIndex]);
                g_promptIndex = (g_promptIndex + 1) % NUM_PROMPTS;
            } else if (z == 2) {
                nextPreset();
            } else {  // z == 0
                prevPreset();
            }
        }
        wasTouched[z] = nowTouched;
    }

    // -- Bonus: swipe gestures --
    if (M5StackChan.TouchSensor.wasSwipedForward())  { nextPreset(); }
    if (M5StackChan.TouchSensor.wasSwipedBackward()) { prevPreset(); }

    delay(10);
}
