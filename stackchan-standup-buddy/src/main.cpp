// StackChan CoreS3 stand-up buddy: device-authoritative work/free/meeting/break
// state machine, 30-minute activity cycles, head-pat clock-in and wisdom easter egg.

#include <M5Unified.h>
#include <M5StackChan.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>
#include "motion.h"
#include "gif_face.h"
#include "camera_height.h"
#include "mode_logic.h"

namespace {

constexpr uint32_t CYCLE_MS = 30UL * 60 * 1000;
constexpr uint32_t REPEAT_MS = 5UL * 60 * 1000;
constexpr uint32_t REMINDER_UI_MS = 8000;
constexpr uint32_t BREAK_MS = 10UL * 60 * 1000;
constexpr uint32_t DOUBLE_PAT_MS = 700;
constexpr uint32_t WISDOM_MS = 8000;
constexpr int SCREEN_W = 320;
constexpr int PANEL_Y = 148;
constexpr int PANEL_H = 92;
constexpr int16_t PITCH_BASELINE = 600;

BuddyMode g_mode = MODE_UNCHECKED;
BuddyMode g_returnMode = MODE_UNCHECKED;
BuddyMode g_freeReturnMode = MODE_UNCHECKED;
AgentState g_agentState = STATE_SLEEP;
bool g_menu = false;
bool g_meetingMenu = false;
bool g_reminder = false;
bool g_repeatReminder = false;
bool g_wisdom = false;
bool g_checkedToday = false;
bool g_prevMonitoring = false;
uint32_t g_checkedDate = 0;
uint16_t g_checkedMinute = 0;
uint32_t g_nextReminderMs = 0;
uint32_t g_reminderEndsMs = 0;
uint32_t g_modeEndsMs = 0;
uint32_t g_wisdomEndsMs = 0;
uint32_t g_firstPatMs = 0;
uint32_t g_lastModeReportMs = 0;
uint32_t g_lastPanelSecond = UINT32_MAX;
uint32_t g_lastDateSeen = 0;
int g_lastMinute = -2;
Preferences g_prefs;
String g_remoteWisdom;
String g_activeWisdom;
void acceptTrack(int cx, int cy);

// TIME/CLOCK from helper, RTC fallback when helper is absent.
bool g_timeKnown = false;
int g_timeBaseMin = 0;
uint32_t g_timeBaseMs = 0;
uint32_t g_lastTimeMs = 0;

const char* LOCAL_WISDOM[] = {
    "今天也不用满分，在线就很好。",
    "先伸个懒腰，世界不会趁机跑掉。",
    "人生很长，肩膀不用一直加班。",
    "喝口水吧，灵感也喜欢湿润的土壤。",
    "暂停不是掉线，是给自己缓冲。",
    "慢一点没关系，螃蟹也是横着到达。",
    "认真生活的人，也值得认真休息。",
    "站起来看看，难题也许只是坐太久了。",
};

uint32_t dateKey(const m5::rtc_date_t& d) {
    return (uint32_t)d.year * 10000 + d.month * 100 + d.date;
}

bool readRtc(m5::rtc_datetime_t& dt) {
    return M5.Rtc.getDateTime(&dt) && dt.date.year >= 2024;
}

int currentMinutes() {
    uint32_t now = millis();
    if (g_timeKnown && now - g_lastTimeMs < 90000)
        return (g_timeBaseMin + (int)((now - g_timeBaseMs) / 60000)) % 1440;
    static uint32_t lastRead = 0;
    static int cached = -1;
    if (now - lastRead >= 1000) {
        lastRead = now;
        m5::rtc_datetime_t dt;
        cached = readRtc(dt) ? dt.time.hours * 60 + dt.time.minutes : -1;
    }
    return cached;
}

uint32_t currentDate() {
    static uint32_t lastRead = 0;
    static uint32_t cached = 0;
    if (millis() - lastRead >= 1000 || cached == 0) {
        lastRead = millis();
        m5::rtc_datetime_t dt;
        cached = readRtc(dt) ? dateKey(dt.date) : 0;
    }
    return cached;
}

void setRtcClock(uint32_t day, int minute) {
    m5::rtc_datetime_t dt;
    if (!readRtc(dt)) dt = {{2026, 1, 1}, {0, 0, 0}};
    if (day >= 20240101) {
        dt.date.year = day / 10000;
        dt.date.month = (day / 100) % 100;
        dt.date.date = day % 100;
    }
    dt.time = m5::rtc_time_t(minute / 60, minute % 60, 0);
    M5.Rtc.setDateTime(&dt);
}

void clearPanel() { M5.Display.fillRect(0, PANEL_Y, SCREEN_W, PANEL_H, TFT_BLACK); }

const char* modeName(BuddyMode m) {
    switch (m) {
    case MODE_WORK: return "工作";
    case MODE_MEETING: return "会议";
    case MODE_FREE: return "自由";
    case MODE_BREAK: return "休息";
    default: return "未打卡";
    }
}

AgentState visualState() {
    if (g_reminder) return STATE_REMINDER;
    if (g_mode == MODE_MEETING) return STATE_MEETING;
    if (g_mode == MODE_BREAK) return STATE_BREAK;
    if (g_mode == MODE_FREE && monitoringEnabled(g_mode, currentMinutes())) return STATE_FREE;
    if (g_mode == MODE_WORK && monitoringEnabled(g_mode, currentMinutes())) return STATE_IDLE;
    return STATE_SLEEP;
}

void applyVisualState(bool force = false) {
    AgentState next = visualState();
    if (!force && next == g_agentState) return;
    g_agentState = next;
    motionSetState(next);
    if (force) gifFaceForceState(next);
    else gifFaceSetState(next);
    g_lastPanelSecond = UINT32_MAX;
}

void reportMode(bool force = false) {
    uint32_t now = millis();
    if (!force && now - g_lastModeReportMs < 30000) return;
    g_lastModeReportMs = now;
    const char* token = monitoringEnabled(g_mode, currentMinutes())
        ? (g_mode == MODE_FREE ? "FREE" : "WORK") : "OFF";
    Serial.printf("MODE %s\n", token);
}

void persistCheckin() {
    g_prefs.putUInt("checkDate", g_checkedDate);
    g_prefs.putUShort("checkMin", g_checkedMinute);
}

void clearCheckin() {
    g_checkedToday = false;
    g_checkedDate = 0;
    g_checkedMinute = 0;
    g_prefs.remove("checkDate");
    g_prefs.remove("checkMin");
}

void startCycle() {
    g_nextReminderMs = millis() + CYCLE_MS;
    g_repeatReminder = false;
    g_lastPanelSecond = UINT32_MAX;
}

void setMode(BuddyMode next, uint32_t durationMs = 0) {
    if (next == MODE_MEETING && g_mode != MODE_MEETING && g_mode != MODE_BREAK)
        g_returnMode = g_mode;
    g_mode = next;
    g_modeEndsMs = durationMs ? millis() + durationMs : 0;
    g_reminder = false;
    M5.Speaker.stop();
    if (next == MODE_WORK || next == MODE_FREE) startCycle();
    applyVisualState();
    reportMode(true);
}

void startBreak() {
    if (g_mode != MODE_MEETING && g_mode != MODE_BREAK) g_returnMode = g_mode;
    setMode(MODE_BREAK, BREAK_MS);
}

void endBreak() {
    int minute = currentMinutes();
    BuddyMode target = g_returnMode;
    if (target == MODE_WORK && (!g_checkedToday || (minute >= 0 && !inWorkWindow(minute))))
        target = MODE_UNCHECKED;
    if (target == MODE_FREE && inQuietHours(minute)) target = MODE_UNCHECKED;
    setMode(target);
}

void endMeeting() {
    g_returnMode = (g_returnMode == MODE_FREE) ? MODE_FREE : MODE_WORK;
    startBreak();
}

void playReminderSound(bool repeated) {
    const uint8_t normal = 40;
    M5.Speaker.setVolume(repeated ? normal / 3 : normal / 2);
    M5.Speaker.tone(880, repeated ? 110 : 140);
    delay(repeated ? 130 : 220);
    if (!repeated) {
        M5.Speaker.tone(1047, 140);
        delay(220);
    }
    M5.Speaker.stop();
    M5.Speaker.setVolume(normal);
}

void fireReminder() {
    g_reminder = true;
    g_reminderEndsMs = millis() + REMINDER_UI_MS;
    applyVisualState();
    playReminderSound(g_repeatReminder);
}

bool headPat() {
    if (!M5.Imu.isEnabled()) return false;
    static float lastMag = 1.0f;
    static uint32_t lastFire = 0;
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    float jerk = fabsf(mag - lastMag);
    lastMag = mag;
    uint32_t now = millis();
    if (jerk > 0.8f && now - lastFire > 300) {
        lastFire = now;
        return true;
    }
    return false;
}

void showWisdom() {
    g_activeWisdom = g_remoteWisdom.length() ? g_remoteWisdom
        : LOCAL_WISDOM[esp_random() % (sizeof(LOCAL_WISDOM) / sizeof(LOCAL_WISDOM[0]))];
    g_remoteWisdom = "";
    g_wisdom = true;
    g_wisdomEndsMs = millis() + WISDOM_MS;
    gifFaceShowRandom();
    Serial.println("WISDOM_REQUEST");
    g_lastPanelSecond = UINT32_MAX;
}

void clockIn() {
    int minute = currentMinutes();
    uint32_t today = currentDate();
    if (minute < 0 || today == 0 || !inWorkWindow(minute)) {
        g_activeWisdom = "现在不在工作时段，先好好休息吧。";
        g_wisdom = true;
        g_wisdomEndsMs = millis() + WISDOM_MS;
        gifFaceShowRandom();
        return;
    }
    g_checkedToday = true;
    g_checkedDate = today;
    g_checkedMinute = minute;
    persistCheckin();
    g_agentState = STATE_CELEBRATE;
    motionSetState(STATE_CELEBRATE);
    gifFaceSetState(STATE_CELEBRATE);
    M5.Speaker.setVolume(20);
    M5.Speaker.tone(1047, 120);
    delay(200);
    M5.Speaker.stop();
    M5.Speaker.setVolume(40);
    uint32_t until = millis() + 2000;
    while ((int32_t)(millis() - until) < 0) {
        gifFaceTick(); motionTick(STATE_CELEBRATE); delay(10);
    }
    setMode(MODE_WORK);
}

void handlePat() {
    if (g_reminder) { startBreak(); return; }
    uint32_t now = millis();
    if (!g_checkedToday && g_mode == MODE_UNCHECKED) {
        if (g_firstPatMs && now - g_firstPatMs <= DOUBLE_PAT_MS) {
            g_firstPatMs = 0;
            clockIn();
        } else {
            g_firstPatMs = now;
        }
        return;
    }
    showWisdom();
}

void drawCentered(const String& text, int y, uint16_t color, const lgfx::IFont* font) {
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.setFont(font);
    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.drawString(text, SCREEN_W / 2, y);
    M5.Display.setTextDatum(TL_DATUM);
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t color) {
    M5.Display.fillRoundRect(x, y, w, h, 8, color);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextColor(TFT_WHITE, color);
    M5.Display.drawString(label, x + w / 2, y + h / 2);
    M5.Display.setTextDatum(TL_DATUM);
}

void drawPanel() {
    uint32_t now = millis();
    uint32_t seconds = 0;
    if (g_mode == MODE_MEETING || g_mode == MODE_BREAK)
        seconds = g_modeEndsMs && (int32_t)(g_modeEndsMs - now) > 0 ? (g_modeEndsMs - now + 999) / 1000 : 0;
    else if (monitoringEnabled(g_mode, currentMinutes()))
        seconds = (int32_t)(g_nextReminderMs - now) > 0 ? (g_nextReminderMs - now + 999) / 1000 : 0;
    if (!g_wisdom && !g_reminder && seconds == g_lastPanelSecond && currentMinutes() == g_lastMinute) return;
    g_lastPanelSecond = seconds;
    g_lastMinute = currentMinutes();
    clearPanel();

    if (g_wisdom) {
        M5.Display.setTextDatum(TL_DATUM);
        M5.Display.setFont(&fonts::efontCN_16);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setTextWrap(true);
        M5.Display.setCursor(16, PANEL_Y + 8);
        M5.Display.println(g_activeWisdom);
        drawCentered("轻轻一拍，补给一点好心情", PANEL_Y + 62, TFT_DARKGREY, &fonts::efontCN_16);
        return;
    }
    if (g_reminder) {
        drawCentered("该起来活动一下啦", PANEL_Y + 2, TFT_WHITE, &fonts::efontCN_16);
        drawButton(8, PANEL_Y + 34, 146, 50, "开始休息", TFT_DARKGREEN);
        drawButton(166, PANEL_Y + 34, 146, 50, "进入会议", TFT_NAVY);
        return;
    }
    if (g_mode == MODE_UNCHECKED) {
        drawCentered("未打卡 · 双拍脑袋打卡", PANEL_Y + 34, TFT_WHITE, &fonts::efontCN_16);
        return;
    }
    char value[32];
    if (g_mode == MODE_MEETING && g_modeEndsMs == 0) snprintf(value, sizeof(value), "会议 · 等你回来");
    else snprintf(value, sizeof(value), "%s · %02lu:%02lu", modeName(g_mode),
                  (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    drawCentered(value, PANEL_Y + 10, g_mode == MODE_FREE ? TFT_MAGENTA : TFT_WHITE, &fonts::efontCN_24);
    if (g_mode == MODE_MEETING) drawButton(80, PANEL_Y + 50, 160, 36, "我回来了", TFT_NAVY);
    else if (g_mode == MODE_BREAK) drawButton(80, PANEL_Y + 50, 160, 36, "继续工作", TFT_DARKGREEN);
    else {
        int minute = currentMinutes();
        if (g_mode == MODE_WORK && minute >= 0 && !inWorkWindow(minute)) {
            const char* msg = minute < WORK_AM_START ? "08:00 开始" : (minute < WORK_PM_START ? "午休中 · 14:00 继续" : "今天辛苦了");
            drawCentered(msg, PANEL_Y + 62, TFT_DARKGREY, &fonts::efontCN_16);
        } else drawCentered("点这里切换模式", PANEL_Y + 62, TFT_DARKGREY, &fonts::efontCN_16);
    }
}

void drawMenu() {
    M5.Display.fillScreen(TFT_BLACK);
    drawCentered(g_meetingMenu ? "会议时长" : "选择模式", 12, TFT_WHITE, &fonts::efontCN_24);
    if (g_meetingMenu) {
        const char* labels[] = {"15 分钟", "30 分钟", "60 分钟", "90 分钟", "直到我回来"};
        for (int i = 0; i < 5; ++i) drawButton(28, 48 + i * 36, 264, 30, labels[i], i == 4 ? TFT_DARKGREY : TFT_NAVY);
    } else {
        if (g_checkedToday) {
            char checked[32];
            snprintf(checked, sizeof(checked), "今日打卡 %02u:%02u", g_checkedMinute / 60, g_checkedMinute % 60);
            drawCentered(checked, 36, TFT_DARKGREY, &fonts::efontCN_16);
        }
        const char* work = g_checkedToday ? "工作模式" : "工作模式（请先双拍打卡）";
        drawButton(28, 52, 264, 42, work, g_checkedToday ? TFT_NAVY : TFT_DARKGREY);
        drawButton(28, 104, 264, 42, "自由模式", TFT_PURPLE);
        drawButton(28, 156, 264, 42, "会议模式", TFT_DARKCYAN);
        if (g_mode == MODE_FREE) {
            drawButton(24, 208, 128, 26, "退出自由", TFT_DARKGREY);
            drawButton(168, 208, 128, 26, "关闭", TFT_DARKGREY);
        } else drawButton(104, 208, 112, 26, "关闭", TFT_DARKGREY);
    }
}

void closeMenu() {
    g_menu = false; g_meetingMenu = false;
    gifFaceRefresh();
    g_lastPanelSecond = UINT32_MAX;
}

void handleTouch() {
    auto t = M5.Touch.getDetail();
    if (!t.wasReleased()) return;
    if (g_menu) {
        if (g_meetingMenu) {
            if (t.x >= 28 && t.x <= 292 && t.y >= 48 && t.y < 228) {
                int row = (t.y - 48) / 36;
                const uint32_t mins[] = {15, 30, 60, 90, 0};
                setMode(MODE_MEETING, mins[row] * 60UL * 1000);
                closeMenu();
            }
            return;
        }
        if (t.y >= 52 && t.y < 94 && g_checkedToday) { setMode(MODE_WORK); closeMenu(); }
        else if (t.y >= 104 && t.y < 146) {
            if (inQuietHours(currentMinutes())) {
                closeMenu();
                g_activeWisdom = "08:00 前保持安静，自由模式也要睡觉。";
                g_wisdom = true; g_wisdomEndsMs = millis() + WISDOM_MS; gifFaceShowRandom();
            } else {
                if (g_mode != MODE_FREE) g_freeReturnMode = g_mode;
                setMode(MODE_FREE); closeMenu();
            }
        } else if (t.y >= 156 && t.y < 198 && (g_mode == MODE_WORK || g_mode == MODE_FREE)) {
            g_meetingMenu = true; drawMenu();
        } else if (t.y >= 204) {
            if (g_mode == MODE_FREE && t.x < 160) {
                BuddyMode target = g_freeReturnMode;
                int minute = currentMinutes();
                if (target == MODE_WORK && (!g_checkedToday || !inWorkWindow(minute)))
                    target = MODE_UNCHECKED;
                setMode(target);
            }
            closeMenu();
        }
        return;
    }
    if (g_reminder) {
        if (t.y >= PANEL_Y + 28 && t.x < SCREEN_W / 2) startBreak();
        else if (t.y >= PANEL_Y + 28) { g_menu = true; g_meetingMenu = true; drawMenu(); }
        return;
    }
    if (g_mode == MODE_MEETING && t.y >= PANEL_Y + 42) { endMeeting(); return; }
    if (g_mode == MODE_BREAK && t.y >= PANEL_Y + 42) { endBreak(); return; }
    if (t.y >= PANEL_Y) { g_menu = true; drawMenu(); }
}

// Mac tracker serial protocol. StackChan reports MODE; helper sends CLOCK and
// one cached WISDOM. The helper never decides the mode or work schedule.
void parseSerial(const char* line) {
    unsigned day = 0; int minute = 0;
    if (sscanf(line, "CLOCK %u %d", &day, &minute) == 2) {
        minute = constrain(minute, 0, 1439);
        g_timeBaseMin = minute; g_timeBaseMs = millis(); g_lastTimeMs = g_timeBaseMs; g_timeKnown = true;
        setRtcClock(day, minute);
        reportMode(true);
        return;
    }
    if (sscanf(line, "TIME %d", &minute) == 1) {
        minute = constrain(minute, 0, 1439);
        g_timeBaseMin = minute; g_timeBaseMs = millis(); g_lastTimeMs = g_timeBaseMs; g_timeKnown = true;
        setRtcClock(0, minute);
        reportMode(true);
        return;
    }
    if (strncmp(line, "WISDOM ", 7) == 0) g_remoteWisdom = String(line + 7).substring(0, 120);
    if (strncmp(line, "TRACK LOST", 10) == 0) return;
    int cx, cy, conf;
    if (sscanf(line, "TRACK %d %d %d", &cx, &cy, &conf) == 3) {
        acceptTrack(constrain(cx, -1000, 1000), constrain(cy, -1000, 1000));
    }
}

void pollSerial() {
    static char buf[160]; static uint8_t len = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') { buf[len] = 0; parseSerial(buf); len = 0; }
        else if (c != '\r') { if (len < sizeof(buf) - 1) buf[len++] = c; else len = 0; }
    }
}

// Face tracking and board-camera height adjustment are both gated by the same
// monitoringEnabled() result as reminders.
int g_trackCx = 0, g_trackCy = 0;
uint32_t g_lastTrackMs = 0;
float g_yaw = 0, g_pitch = PITCH_BASELINE;
int16_t g_camPitch = PITCH_BASELINE;
uint32_t g_nextCamMs = 20000;

void acceptTrack(int cx, int cy) { g_trackCx = cx; g_trackCy = cy; g_lastTrackMs = millis(); }

void monitoringTick() {
    bool enabled = monitoringEnabled(g_mode, currentMinutes()) && !g_reminder;
    bool fresh = enabled && millis() - g_lastTrackMs < 3000;
    if (fresh) {
        g_yaw += .25f * ((g_trackCx * .45f) - g_yaw);
        g_pitch += .25f * ((PITCH_BASELINE + g_trackCy * .20f) - g_pitch);
        motionSetTracking(true);
        motionTrackTarget(constrain((int)g_yaw, -1000, 1000), constrain((int)g_pitch, 400, 800));
    } else if (enabled) {
        if (enabled && (int32_t)(millis() - g_nextCamMs) >= 0) {
            g_nextCamMs = millis() + 30000;
            CamProbe p = cameraProbeOnce();
            if (p.ok && p.motion) g_camPitch = constrain(g_camPitch + constrain((int)((.40f - p.cy) * 500), -50, 50), 400, 800);
            Serial.printf("[cam] motion=%d cy=%.2f pitch=%d\n", p.motion, p.cy, g_camPitch);
        }
        motionSetTracking(true);
        motionTrackTarget(0, g_camPitch);
    } else motionSetTracking(false);
}

void timeTransitions() {
    int minute = currentMinutes();
    uint32_t today = currentDate();
    if (today && today != g_lastDateSeen) {
        g_lastDateSeen = today;
        g_checkedToday = g_checkedDate == today;
        if (!g_checkedToday) { clearCheckin(); if (g_mode == MODE_WORK) setMode(MODE_UNCHECKED); }
        else if (g_mode == MODE_UNCHECKED && inWorkWindow(minute)) setMode(MODE_WORK);
        if (g_mode == MODE_FREE && inQuietHours(minute)) setMode(MODE_UNCHECKED);
    }
    if (g_checkedToday && shouldClearCheckin(minute)) {
        clearCheckin();
        if (g_mode == MODE_WORK) setMode(MODE_UNCHECKED);
    }
    if (g_mode == MODE_FREE && inQuietHours(minute)) setMode(MODE_UNCHECKED);
    if (g_mode == MODE_UNCHECKED && g_checkedToday && inWorkWindow(minute)) setMode(MODE_WORK);

    bool active = monitoringEnabled(g_mode, minute);
    if (active && !g_prevMonitoring) startCycle(); // includes 14:00 resume
    if (active != g_prevMonitoring) { g_prevMonitoring = active; applyVisualState(); reportMode(true); }
}

} // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Speaker.setVolume(40);
    motionInit();
    gifFaceInit();
    gifFaceSetTextBand(PANEL_Y);
    gifFaceSetYOffset(0);
    g_prefs.begin("standup", false);
    g_checkedDate = g_prefs.getUInt("checkDate", 0);
    g_checkedMinute = g_prefs.getUShort("checkMin", 0);
    uint32_t today = currentDate();
    int minute = currentMinutes();
    g_checkedToday = today && g_checkedDate == today;
    g_lastDateSeen = today;
    g_mode = restoredMode(g_checkedToday, minute);
    g_prevMonitoring = monitoringEnabled(g_mode, minute);
    g_nextReminderMs = millis() + CYCLE_MS;
    applyVisualState(true);
    Serial.println("WISDOM_REQUEST");
    reportMode(true);
}

void loop() {
    pollSerial();
    timeTransitions();

    uint32_t now = millis();
    if (g_mode == MODE_MEETING && g_modeEndsMs && (int32_t)(now - g_modeEndsMs) >= 0) endMeeting();
    if (g_mode == MODE_BREAK && g_modeEndsMs && (int32_t)(now - g_modeEndsMs) >= 0) endBreak();
    if (g_wisdom && (int32_t)(now - g_wisdomEndsMs) >= 0) { g_wisdom = false; applyVisualState(true); }
    if (g_reminder && (int32_t)(now - g_reminderEndsMs) >= 0) {
        g_reminder = false; g_repeatReminder = true; g_nextReminderMs = now + REPEAT_MS; applyVisualState();
    }
    if (!g_reminder && monitoringEnabled(g_mode, currentMinutes()) && (int32_t)(now - g_nextReminderMs) >= 0) fireReminder();

    if (!g_menu) {
        monitoringTick();
        gifFaceTick();
        motionTick(g_agentState);
        if (!g_wisdom && headPat()) handlePat();
        if (g_firstPatMs && now - g_firstPatMs > DOUBLE_PAT_MS) { g_firstPatMs = 0; showWisdom(); }
        drawPanel();
    } else {
        monitoringTick();
        motionTick(g_agentState);
    }
    handleTouch();
    reportMode();
    delay(10);
}
