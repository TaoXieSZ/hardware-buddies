#include "audio_constants.h"
#include "audio_loop.h"
#include "audio_queue.h"
#include "control_protocol.h"
#include "protocol.h"
#include "walkie_ui.h"

#if __has_include("walkie_config.h")
#include "walkie_config.h"
#else
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"
#define WALKIE_WS_HOST "192.168.1.10"
#define WALKIE_WS_PORT 8765
#define WALKIE_WS_PATH "/audio"
#define WALKIE_DEVICE_ID ""
#define WALKIE_CONTROL_ENABLED 0
#define WALKIE_CONTROL_SECRET ""
#endif
#ifndef WIFI_SSID_2
#define WIFI_SSID_2 ""
#endif
#ifndef WIFI_PASSWORD_2
#define WIFI_PASSWORD_2 ""
#endif

#ifndef WALKIE_CONTROL_ENABLED
#define WALKIE_CONTROL_ENABLED 0
#endif
#ifndef WALKIE_CONTROL_SECRET
#define WALKIE_CONTROL_SECRET ""
#endif
#ifndef WALKIE_MUSE_HOST
#define WALKIE_MUSE_HOST "muse.example.com"
#endif
#ifndef WALKIE_MUSE_TOKEN
#define WALKIE_MUSE_TOKEN ""
#endif
#if defined(WALKIE_MUSE_DIRECT)
#include "muse_client.h"
#include "muse_fonts.h"
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5IOE1.h>
#include <M5Unified.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

using namespace stopwatch;

namespace {

constexpr uint8_t kIoeAddrPrimary = 0x4F;
constexpr uint8_t kIoeAddrSecondary = 0x6F;
constexpr uint8_t kMotorPin = M5IOE1_PIN_9;
constexpr uint8_t kMotorPwmChannel = 0;
constexpr uint16_t kMotorPwmHz = 5000;
constexpr size_t kMaxTtsPcmBytes = 16000 * sizeof(int16_t) * 30;

AudioLoop loop_state;
AudioQueue audio_queue;
WebSocketsClient web_socket;
M5IOE1 ioe;
WalkieUi walkie_ui;

std::array<int16_t, kPcmSamplesPerChunk> mic_chunk{};
PcmChunk tx_chunk;
String device_id;
uint32_t utterance_seq = 0;
uint32_t reconnect_delay_ms = kReconnectInitialMs;
uint32_t next_reconnect_ms = 0;
uint32_t last_ui_ms = 0;
bool ws_connected = false;
bool ioe_ready = false;
bool ws_started = false;
bool ws_attempt_in_progress = false;
uint16_t ui_audio_level = 0;
uint8_t* tts_pcm = nullptr;
size_t tts_pcm_bytes = 0;
String tts_result_id;
bool tts_receiving = false;
bool tts_playing = false;
ControlCrypto control_crypto;
SequenceWindow inbound_control_sequence;
String control_device_nonce;
String control_bridge_nonce;
String control_session_id;
uint64_t outbound_control_sequence = 0;
bool control_authenticated = false;
bool control_mode = false;

#if defined(WALKIE_MUSE_DIRECT)
// Direct-to-Muse build: a worker task owns all HTTPS so the UI keeps animating.
// Spec: openspec/changes/add-stopwatch-muse-direct
enum class MuseJobKind : uint8_t { Status, Transcribe, Chat, Reminders, MarkRead };
struct MuseJob {
    MuseJobKind kind;
    uint32_t pcm_bytes;
    char id[48];
};
struct MuseDone {
    MuseJobKind kind;
    std::string id;
    MuseOutcome outcome;
};
constexpr uint32_t kMuseStatusIdleMs = 60000;
constexpr uint32_t kMuseStatusRetryMs = 10000;
MuseClient muse_client;
QueueHandle_t muse_jobs = nullptr;
QueueHandle_t muse_done = nullptr;
size_t muse_record_bytes = 0;
int muse_pending = 0;
uint32_t muse_next_status_ms = 0;
bool muse_disabled = false;  // no device token: never contact the cloud
constexpr uint32_t kMuseConfirmMs = 3000;  // D3: visible countdown, then send
std::string muse_question;      // raw transcript, RAM only, cleared once sent or cancelled
std::string muse_heard;         // "问：…" line shown on confirm and answer
std::string muse_confirm_id;
uint32_t muse_confirm_deadline_ms = 0;
int muse_unread = 0;
std::string muse_reminder_id;
std::vector<std::string> muse_reminder_entries;  // newest unread card, one reading page per entry
size_t muse_reminder_index = 0;
bool muse_showing_reminder = false;
std::string muse_abandoned_id;   // KEYB while thinking: this request's late reply is ignored
constexpr uint32_t kMuseSleepMs = 120000;  // home untouched this long: dim and let the crab sleep (burn-in)
constexpr uint8_t kMuseDimBrightness = 24;
uint32_t muse_activity_ms = 0;
bool muse_asleep = false;
uint8_t muse_brightness = 0;
#endif

#if defined(WALKIE_UI_DEMO)
constexpr std::array<DeviceState, 13> kDemoStates = {
    DeviceState::Connecting, DeviceState::Ready,         DeviceState::Recording,
    DeviceState::Transcribing, DeviceState::Result,      DeviceState::Error,
    DeviceState::ProposalReview, DeviceState::Dispatching, DeviceState::Running,
    DeviceState::WaitingPermission, DeviceState::Completed, DeviceState::Cancelled,
    DeviceState::Failed,
};
size_t demo_state_index = 0;
#endif

bool drainAudioQueue();

String deriveDeviceId()
{
    if (String(WALKIE_DEVICE_ID).length() > 0) {
        return String(WALKIE_DEVICE_ID);
    }
    uint64_t mac = ESP.getEfuseMac();
    char buf[24];
    std::snprintf(buf, sizeof(buf), "stopwatch-%04X%08lX", static_cast<uint16_t>(mac >> 32),
                  static_cast<unsigned long>(mac & 0xFFFFFFFFUL));
    return String(buf);
}

bool sendText(const std::string& payload)
{
    bool sent = false;
    if (ws_connected) {
        sent = web_socket.sendTXT(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.size());
    }
    Serial.printf("[ws] tx %s bytes=%u\n", sent ? "ok" : "failed", static_cast<unsigned>(payload.size()));
    return sent;
}

String randomToken()
{
    uint8_t bytes[kControlNonceBytes]{};
    esp_fill_random(bytes, sizeof(bytes));
    return String(base64UrlEncode(bytes, sizeof(bytes)).c_str());
}

bool sendControlText(const std::string& body)
{
    if (!control_authenticated) {
        return sendText(body);
    }
    if (body.size() > kMaxControlBodyBytes || outbound_control_sequence == UINT64_MAX) {
        return false;
    }
    const std::string encoded = base64UrlEncode(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    const uint64_t sequence = ++outbound_control_sequence;
    const std::string mac = control_crypto.envelopeMac("d2b", control_session_id.c_str(), sequence, encoded);
    JsonDocument envelope;
    envelope["session_id"] = control_session_id;
    envelope["direction"] = "d2b";
    envelope["seq"] = sequence;
    envelope["body"] = encoded;
    envelope["mac"] = mac;
    String serialized;
    serializeJson(envelope, serialized);
    return sendText(serialized.c_str());
}

bool decodeControlEnvelope(const JsonDocument& envelope, String& body)
{
    if (!control_authenticated || String(envelope["session_id"] | "") != control_session_id ||
        std::strcmp(envelope["direction"] | "", "b2d") != 0) {
        return false;
    }
    const uint64_t sequence = envelope["seq"] | 0ULL;
    const std::string encoded = envelope["body"] | "";
    const std::string received_mac = envelope["mac"] | "";
    if (sequence == 0 || sequence <= inbound_control_sequence.last() || encoded.size() > kMaxControlBodyBytes * 2) {
        return false;
    }
    const std::string expected = control_crypto.envelopeMac("b2d", control_session_id.c_str(), sequence, encoded);
    if (expected.empty() || !constantTimeEqual(expected, received_mac)) {
        return false;
    }
    std::vector<uint8_t> decoded;
    if (!base64UrlDecode(encoded, decoded) || decoded.size() > kMaxControlBodyBytes) {
        return false;
    }
    decoded.push_back(0);
    body = String(reinterpret_cast<const char*>(decoded.data()));
    return inbound_control_sequence.accept(sequence);
}

void updateAudioLevel()
{
    uint32_t peak = 0;
    for (const int16_t sample : mic_chunk) {
        const uint32_t magnitude = sample < 0 ? static_cast<uint32_t>(-static_cast<int32_t>(sample))
                                              : static_cast<uint32_t>(sample);
        peak = std::max(peak, magnitude);
    }
    const uint16_t target = static_cast<uint16_t>(std::min<uint32_t>(1000, peak * 1000 / 6000));
    ui_audio_level = static_cast<uint16_t>((ui_audio_level * 2 + target) / 3);
}

void renderUi(bool force = false)
{
    const uint32_t now = millis();
    if (!force && now - last_ui_ms < 80) {
        return;
    }
    last_ui_ms = now;

    UiModel model;
#if defined(WALKIE_UI_DEMO)
    static constexpr std::array<const char*, 13> kDemoDetails = {
        "Searching for your Mac", "Press A to explore", "Speak to test the mic",
        "Sending audio securely", "Agent reply received", "bridge_unreachable",
        "run the focused tests", "Verifying exact target", "Agent is working",
        "shell needs approval", "All tests passed", "Nothing was sent", "target_stale",
    };
    model.state = kDemoStates[demo_state_index];
    model.detail = kDemoDetails[demo_state_index];
    model.retryable = true;
    model.showcase = true;
    model.actionable = model.state == DeviceState::ProposalReview || model.state == DeviceState::WaitingPermission;
#else
    String detail;
    model.state = loop_state.state();
    model.retryable = loop_state.retryableError();
    if (model.state == DeviceState::Connecting) {
#if defined(WALKIE_MUSE_DIRECT)
        detail = String(WALKIE_MUSE_HOST);
#else
        detail = String(WALKIE_WS_HOST) + ":" + String(WALKIE_WS_PORT);
#endif
    } else if (model.state == DeviceState::Result || model.state == DeviceState::ProposalReview ||
               model.state == DeviceState::WaitingPermission || model.state == DeviceState::Completed ||
               model.state == DeviceState::Cancelled || model.state == DeviceState::Failed) {
        detail = String(loop_state.resultText().c_str());
    } else if (model.state == DeviceState::Error) {
        detail = String(loop_state.errorCode().c_str());
    }
    model.detail = detail.c_str();
#if defined(WALKIE_MUSE_DIRECT)
    // Reading pages and copy: openspec add-stopwatch-muse-direct "Reading pages use large smooth text".
    String label;
    const bool online = loop_state.connected();
    switch (model.state) {
        case DeviceState::Connecting:
            model.title = "连接中";
            detail = "正在连接个人云";
            break;
        case DeviceState::Ready:
            model.title = "可以提问";
            detail = muse_unread > 0 ? String("有 ") + String(muse_unread) + " 条提醒" : String();
            model.hint_a = "按住说话";
            model.hint_b = muse_reminder_entries.empty() ? nullptr : "看提醒";
            break;
        case DeviceState::Recording:
            model.title = "在听";
            detail = "";
            model.hint_a = "松开发送";
            model.hint_b = "取消";
            break;
        case DeviceState::Transcribing:
            model.title = "在想";
            detail = muse_question.empty() ? "正在识别…" : "Muse 在想…";
            model.hint_b = "取消";
            break;
        case DeviceState::ProposalReview: {
            const int32_t left = std::max<int32_t>(0, static_cast<int32_t>(muse_confirm_deadline_ms - now));
            label = String((left + 999) / 1000) + " 秒后发送";
            detail = String(muse_question.c_str());
            model.progress = static_cast<int>(left * 100 / static_cast<int32_t>(kMuseConfirmMs));
            model.hint_a = "现在发";
            model.hint_b = "取消";
            break;
        }
        case DeviceState::Result:
            if (muse_showing_reminder && muse_reminder_index < muse_reminder_entries.size()) {
                label = String("提醒 ") + String(muse_reminder_index + 1) + "/" + String(muse_reminder_entries.size());
                detail = String(muse_reminder_entries[muse_reminder_index].c_str());
                model.hint_b = muse_reminder_index + 1 < muse_reminder_entries.size() ? "下一条" : "知道了";
            } else {
                label = String(muse_heard.c_str());
                model.label_quiet = true;
                model.hint_b = "好的";
            }
            model.hint_a = "再问";
            break;
        case DeviceState::Error:
            label = model.retryable ? "没发出去" : "需要处理";
            model.hint_a = online ? "重说" : nullptr;
            model.hint_b = online ? "返回" : nullptr;
            break;
        default:
            break;
    }
    model.detail = detail.c_str();
    model.label = label.c_str();
    model.reminder = model.state == DeviceState::Result && muse_showing_reminder;
    model.unread = muse_unread;
    model.asleep = muse_asleep;
#endif
    model.actionable = model.state == DeviceState::ProposalReview ||
                       (model.state == DeviceState::WaitingPermission && loop_state.permissionActionable());
#endif
    model.audio_level = ui_audio_level;
    walkie_ui.render(model, now);
}

void initVibrator()
{
    auto ret = ioe.begin(&M5.In_I2C, kIoeAddrPrimary, M5IOE1_I2C_FREQ_400K);
    if (ret != M5IOE1_OK) {
        ret = ioe.begin(&M5.In_I2C, kIoeAddrSecondary, M5IOE1_I2C_FREQ_400K);
    }
    if (ret != M5IOE1_OK) {
        Serial.printf("[ioe] M5IOE1 init failed: %d\n", ret);
        return;
    }
    ioe.pinMode(kMotorPin, OUTPUT);
    ioe.setPwmFrequency(kMotorPwmHz);
    ioe.setPwmDuty(kMotorPwmChannel, 0, false, true);
    ioe_ready = true;
}

uint8_t motorDutyFromStrength(uint8_t strength)
{
    if (strength == 0) {
        return 0;
    }
    if (strength > 100) {
        strength = 100;
    }
    return 25 + (static_cast<uint32_t>(strength) * 75) / 100;
}

void releaseAck()
{
    if (!ioe_ready) {
        return;
    }
    // The factory demo uses the same M5IOE1 PWM channel for the motor. Keep this
    // short and only after KEYA release so the vibration is outside capture.
    ioe.setPwmDuty(kMotorPwmChannel, motorDutyFromStrength(kReleaseAckStrength), false, true);
    delay(kReleaseAckMs);
    ioe.setPwmDuty(kMotorPwmChannel, 0, false, true);
}

void controlFeedback(DeviceState state)
{
    if (!ioe_ready) return;
    const uint8_t strength = state == DeviceState::WaitingPermission ? 75 : 55;
    const uint16_t duration = state == DeviceState::Completed ? 90 : 55;
    const int pulses = state == DeviceState::WaitingPermission ? 2 : 1;
    for (int i = 0; i < pulses; ++i) {
        ioe.setPwmDuty(kMotorPwmChannel, motorDutyFromStrength(strength), false, true);
        delay(duration);
        ioe.setPwmDuty(kMotorPwmChannel, 0, false, true);
        if (i + 1 < pulses) delay(45);
    }
}

void resetBackoff()
{
    reconnect_delay_ms = kReconnectInitialMs;
    next_reconnect_ms = 0;
}

void scheduleReconnect()
{
    next_reconnect_ms = millis() + reconnect_delay_ms;
    reconnect_delay_ms = std::min<uint32_t>(reconnect_delay_ms * 2, kReconnectMaxMs);
}

void beginWebSocket()
{
    web_socket.begin(WALKIE_WS_HOST, WALKIE_WS_PORT, WALKIE_WS_PATH);
    web_socket.setReconnectInterval(0);
    web_socket.enableHeartbeat(15000, 3000, 2);
    ws_started = true;
}

bool sendStart(const std::string& id)
{
    return sendControlText(utteranceStartMessage(id));
}

bool sendEnd(const std::string& id)
{
    return sendControlText(utteranceEndMessage(id));
}

bool sendCancel(const std::string& id)
{
    return sendControlText(utteranceCancelMessage(id));
}

bool sendDecision(const char* type, const std::string& id, const char* decision)
{
    JsonDocument doc;
    doc["type"] = type;
    if (std::strcmp(type, "command.decision") == 0) doc["command_id"] = id;
    else doc["request_id"] = id;
    doc["decision"] = decision;
    String serialized;
    serializeJson(doc, serialized);
    return sendControlText(serialized.c_str());
}

bool sendTaskSnapshot(const std::string& task_id)
{
    JsonDocument doc;
    doc["type"] = "task.snapshot";
    doc["task_id"] = task_id;
    String serialized;
    serializeJson(doc, serialized);
    return sendControlText(serialized.c_str());
}

void cancelActiveForDeviceError(const char* code)
{
    const std::string id = loop_state.activeId();
    if (!id.empty()) {
        sendCancel(id);
    }
    audio_queue.clear();
    loop_state.onError(id, code, true);
    renderUi(true);
}

void handleTextFrame(const uint8_t* payload, size_t length)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.printf("[ws] invalid json: %s\n", err.c_str());
        return;
    }

    if (!doc["type"].is<const char*>()) {
        String body;
        if (!decodeControlEnvelope(doc, body)) {
            Serial.println("[control] rejected envelope");
            return;
        }
        handleTextFrame(reinterpret_cast<const uint8_t*>(body.c_str()), body.length());
        return;
    }

    const char* type = doc["type"] | "";
    const char* id = doc["id"] | "";
    if (std::strcmp(type, "auth.challenge") == 0) {
        if (!control_mode || control_device_nonce.isEmpty()) {
            Serial.println("[control] unexpected challenge");
            return;
        }
        control_bridge_nonce = doc["bridge_nonce"] | "";
        control_session_id = doc["session_id"] | "";
        const std::string proof = doc["proof"] | "";
        if (control_bridge_nonce.length() > kMaxSessionId || control_session_id.length() > kMaxSessionId ||
            proof.size() > kMaxSessionId) {
            loop_state.onControlError("authentication_failed", false);
            renderUi(true);
            return;
        }
        const std::string expected = control_crypto.proof(
            "bridge", device_id.c_str(), control_device_nonce.c_str(),
            control_bridge_nonce.c_str(), control_session_id.c_str());
        if (control_bridge_nonce.isEmpty() || control_session_id.isEmpty() || !constantTimeEqual(expected, proof)) {
            loop_state.onControlError("authentication_failed", false);
            renderUi(true);
            return;
        }
        JsonDocument response;
        response["type"] = "auth.proof";
        response["proof"] = control_crypto.proof(
            "device", device_id.c_str(), control_device_nonce.c_str(),
            control_bridge_nonce.c_str(), control_session_id.c_str());
        String serialized;
        serializeJson(response, serialized);
        if (!sendText(serialized.c_str())) {
            loop_state.onControlError("authentication_send_failed", true);
            renderUi(true);
            return;
        }
        inbound_control_sequence.reset();
        outbound_control_sequence = 0;
        control_authenticated = true;
        return;
    }
    if (std::strcmp(type, "auth.ok") == 0) {
        loop_state.onConnected();
        if (!loop_state.taskId().empty()) sendTaskSnapshot(loop_state.taskId());
        renderUi(true);
        return;
    }
    if (std::strcmp(type, "transcript") == 0) {
        const char* text = doc["text"] | "";
        if (!loop_state.onTranscript(id, text)) {
            Serial.printf("[ws] ignored transcript for id=%s\n", id);
            return;
        }
        tts_result_id = id;
        renderUi(true);
        return;
    }

    if (std::strcmp(type, "audio.start") == 0) {
        JsonObjectConst audio = doc["audio"];
        const bool format_ok = (audio["rate"] | 0) == 16000 && (audio["bits"] | 0) == 16 &&
                               (audio["channels"] | 0) == 1 &&
                               std::strcmp(audio["encoding"] | "", "pcm_s16le") == 0;
        if (!tts_pcm || tts_result_id != id || !format_ok || tts_playing) {
            Serial.printf("[tts] rejected start id=%s\n", id);
            return;
        }
        tts_pcm_bytes = 0;
        tts_receiving = true;
        Serial.printf("[tts] receiving id=%s\n", id);
        return;
    }

    if (std::strcmp(type, "command.proposal") == 0) {
        const std::string command_id = doc["command_id"] | "";
        const std::string preview = doc["preview"] | "";
        if (command_id.size() <= kMaxCommandId && preview.size() <= kMaxPreview &&
            loop_state.onProposal(command_id, preview)) {
            controlFeedback(DeviceState::ProposalReview);
            renderUi(true);
        }
        return;
    }

    if (std::strcmp(type, "command.error") == 0) {
        loop_state.onControlError(doc["code"] | "target_required", true);
        renderUi(true);
        return;
    }

    if (std::strcmp(type, "task.accepted") == 0) {
        const std::string command_id = doc["command_id"] | "";
        const std::string task_id = doc["task_id"] | "";
        if (command_id.size() <= kMaxCommandId && task_id.size() <= kMaxTaskId &&
            loop_state.onTaskAccepted(command_id, task_id)) renderUi(true);
        return;
    }
    if (std::strcmp(type, "task.running") == 0) {
        if (loop_state.onTaskRunning(doc["task_id"] | "")) renderUi(true);
        return;
    }
    if (std::strcmp(type, "permission.request") == 0) {
        const std::string task_id = doc["task_id"] | "";
        const std::string request_id = doc["request_id"] | "";
        const std::string hint = doc["hint"] | "Permission requested";
        const bool actionable = doc["actionable"] | false;
        if (request_id.size() <= kMaxCommandId && hint.size() <= kMaxErrorDetail &&
            loop_state.onPermission(task_id, request_id, hint, actionable)) {
            controlFeedback(DeviceState::WaitingPermission);
            renderUi(true);
        }
        return;
    }
    if (std::strcmp(type, "permission.resolved") == 0) {
        if (String(doc["request_id"] | "") == loop_state.permissionId().c_str()) {
            loop_state.onTaskRunning(loop_state.taskId());
            renderUi(true);
        }
        return;
    }
    if (std::strcmp(type, "task.completed") == 0 || std::strcmp(type, "task.failed") == 0 ||
        std::strcmp(type, "task.cancelled") == 0) {
        const std::string task_id = doc["task_id"] | "";
        const std::string command_id = doc["command_id"] | "";
        const std::string detail = std::strcmp(type, "task.completed") == 0
            ? std::string(doc["summary"] | "Completed") : std::string(doc["code"] | type);
        DeviceState terminal = DeviceState::Failed;
        if (std::strcmp(type, "task.completed") == 0) terminal = DeviceState::Completed;
        else if (std::strcmp(type, "task.cancelled") == 0) terminal = DeviceState::Cancelled;
        bool changed = loop_state.onTaskTerminal(task_id, terminal, detail);
        if (!changed && terminal == DeviceState::Failed) changed = loop_state.onDispatchFailed(command_id, detail);
        if (changed) {
            tts_result_id = task_id.c_str();
            controlFeedback(terminal);
            renderUi(true);
        }
        return;
    }

    if (std::strcmp(type, "audio.end") == 0) {
        if (!tts_receiving || tts_result_id != id || tts_pcm_bytes == 0 || (tts_pcm_bytes % 2) != 0) {
            Serial.printf("[tts] rejected end id=%s bytes=%u\n", id, static_cast<unsigned>(tts_pcm_bytes));
            tts_receiving = false;
            tts_pcm_bytes = 0;
            return;
        }
        tts_receiving = false;
        while (M5.Mic.isRecording()) {
            M5.delay(1);
        }
        M5.Mic.end();
        M5.Speaker.setVolume(180);
        if (!M5.Speaker.begin() ||
            !M5.Speaker.playRaw(reinterpret_cast<const int16_t*>(tts_pcm), tts_pcm_bytes / sizeof(int16_t), 16000,
                                false, 1, 0, true)) {
            Serial.println("[tts] playback start failed");
            M5.Speaker.end();
            M5.Mic.begin();
            tts_pcm_bytes = 0;
            return;
        }
        tts_playing = true;
        Serial.printf("[tts] playback started bytes=%u\n", static_cast<unsigned>(tts_pcm_bytes));
        return;
    }

    if (std::strcmp(type, "error") == 0) {
        const char* code = doc["code"] | "bridge_error";
        const bool retryable = doc["retryable"] | true;
        if (!loop_state.onError(id, code, retryable)) {
            Serial.printf("[ws] ignored error for id=%s code=%s\n", id, code);
            return;
        }
        audio_queue.clear();
        renderUi(true);
    }
}

void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length)
{
    switch (type) {
        case WStype_CONNECTED:
            ws_connected = true;
            ws_attempt_in_progress = false;
            resetBackoff();
            control_authenticated = false;
            inbound_control_sequence.reset();
            outbound_control_sequence = 0;
            if (control_mode) {
                control_device_nonce = randomToken();
                JsonDocument hello;
                hello["type"] = "hello";
                hello["protocol"] = kProtocolV2;
                hello["device_id"] = device_id;
                hello["device_nonce"] = control_device_nonce;
                String serialized;
                serializeJson(hello, serialized);
                sendText(serialized.c_str());
            } else {
                loop_state.onConnected();
                sendText(helloMessage(device_id.c_str()));
            }
            renderUi(true);
            Serial.println("[ws] connected");
            break;
        case WStype_DISCONNECTED:
            ws_connected = false;
            ws_attempt_in_progress = false;
            audio_queue.clear();
            loop_state.onDisconnected();
            control_authenticated = false;
            control_session_id = "";
            scheduleReconnect();
            renderUi(true);
            Serial.println("[ws] disconnected");
            break;
        case WStype_TEXT:
            handleTextFrame(payload, length);
            break;
        case WStype_BIN:
            if (!tts_receiving || !tts_pcm || (length % 2) != 0 || tts_pcm_bytes + length > kMaxTtsPcmBytes) {
                Serial.printf("[tts] rejected audio chunk bytes=%u total=%u\n", static_cast<unsigned>(length),
                              static_cast<unsigned>(tts_pcm_bytes));
                tts_receiving = false;
                tts_pcm_bytes = 0;
                break;
            }
            std::memcpy(tts_pcm + tts_pcm_bytes, payload, length);
            tts_pcm_bytes += length;
            break;
        default:
            break;
    }
}

void connectWiFi()
{
    // An optional second network (e.g. office and home): try each for 12 s in turn until one connects.
    constexpr uint32_t kWifiTryMs = 12000;
    const char* ssids[] = {WIFI_SSID, WIFI_SSID_2};
    const char* passwords[] = {WIFI_PASSWORD, WIFI_PASSWORD_2};
    const int networks = std::strlen(WIFI_SSID_2) > 0 ? 2 : 1;
    WiFi.mode(WIFI_STA);
    for (int i = 0;; i = (i + 1) % networks) {
        WiFi.begin(ssids[i], passwords[i]);
        Serial.printf("[wifi] connecting network=%d\n", i + 1);
        const uint32_t started = millis();
        while (WiFi.status() != WL_CONNECTED && (networks == 1 || millis() - started < kWifiTryMs)) {
            delay(250);
            M5.update();
            renderUi();
        }
        if (WiFi.status() == WL_CONNECTED) break;
        WiFi.disconnect();
    }
    Serial.printf("[wifi] connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void initHardware()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(0);
    M5.Display.setTextFont(2);
    M5.Display.setTextWrap(true);
    M5.Display.clear(TFT_BLACK);
    walkie_ui.begin();

    M5.Speaker.end();
    if (!M5.Mic.begin()) {
        Serial.println("[mic] M5.Mic.begin failed");
    }
    initVibrator();
    control_mode = WALKIE_CONTROL_ENABLED && control_crypto.configure(WALKIE_CONTROL_SECRET);
    if (WALKIE_CONTROL_ENABLED && !control_mode) {
        Serial.println("[control] disabled: invalid local secret");
    }
}

#if defined(WALKIE_UI_DEMO)
void handleDemoButtons()
{
    if (M5.BtnA.wasPressed()) {
        demo_state_index = (demo_state_index + 1) % kDemoStates.size();
        ui_audio_level = 0;
        Serial.printf("[ui-demo] A next -> %s\n", stateName(kDemoStates[demo_state_index]));
        renderUi(true);
    }
    if (M5.BtnB.wasPressed()) {
        demo_state_index = (demo_state_index + kDemoStates.size() - 1) % kDemoStates.size();
        ui_audio_level = 0;
        Serial.printf("[ui-demo] B back -> %s\n", stateName(kDemoStates[demo_state_index]));
        renderUi(true);
    }
}

void captureDemoMic()
{
    if (kDemoStates[demo_state_index] != DeviceState::Recording || !M5.Mic.isEnabled()) {
        ui_audio_level = static_cast<uint16_t>(ui_audio_level * 3 / 4);
        return;
    }
    if (M5.Mic.record(mic_chunk.data(), mic_chunk.size(), kPcmSampleRateHz)) {
        updateAudioLevel();
    }
}
#endif

void handleButtons()
{
    if (tts_playing) {
        if (M5.BtnB.wasPressed()) {
            M5.Speaker.stop();
            M5.Speaker.end();
            M5.Mic.begin();
            tts_playing = false;
            tts_pcm_bytes = 0;
            Serial.println("[tts] playback cancelled");
        }
        return;
    }

    if (M5.BtnA.wasPressed()) {
        const std::string id = makeUtteranceId(device_id.c_str(), ++utterance_seq);
        const LoopAction action = loop_state.onKeyADown(id);
        if (action == LoopAction::StartUtterance) {
            audio_queue.clear();
            if (sendStart(id)) {
                renderUi(true);
            } else {
                cancelActiveForDeviceError("device_control_send_failed");
            }
        } else if (action == LoopAction::ApproveProposal) {
            if (!sendDecision("command.decision", loop_state.commandId(), "approve")) {
                loop_state.onDispatchFailed(loop_state.commandId(), "device_control_send_failed");
            }
            renderUi(true);
        } else if (action == LoopAction::ApprovePermission) {
            sendDecision("permission.decision", loop_state.permissionId(), "approve");
            renderUi(true);
        } else {
            renderUi(true);
        }
    }

    if (M5.BtnB.wasPressed()) {
        const std::string id = loop_state.activeId();
        const LoopAction action = loop_state.onKeyBCancel();
        if (action == LoopAction::CancelUtterance) {
            if (!sendCancel(id)) {
                Serial.printf("[ws] cancel send failed id=%s\n", id.c_str());
            }
            audio_queue.clear();
            renderUi(true);
        } else if (action == LoopAction::RejectProposal) {
            sendDecision("command.decision", loop_state.commandId(), "reject");
            renderUi(true);
        } else if (action == LoopAction::DenyPermission) {
            sendDecision("permission.decision", loop_state.permissionId(), "deny");
            renderUi(true);
        } else if (action == LoopAction::Acknowledge) {
            renderUi(true);
        }
    }

    if (M5.BtnA.wasReleased()) {
        const std::string id = loop_state.activeId();
        if (loop_state.onKeyAUp() == LoopAction::EndUtterance) {
            if (drainAudioQueue()) {
                if (sendEnd(id)) {
                    releaseAck();
                    renderUi(true);
                } else {
                    cancelActiveForDeviceError("device_control_send_failed");
                }
            }
        }
    }
}

void captureIfRecording()
{
    if (loop_state.state() != DeviceState::Recording) {
        return;
    }
    if (!ws_connected) {
        audio_queue.clear();
        loop_state.onDisconnected();
        scheduleReconnect();
        renderUi(true);
        return;
    }
    if (!M5.Mic.isEnabled()) {
        cancelActiveForDeviceError("mic_unavailable");
        return;
    }
    if (M5.Mic.record(mic_chunk.data(), mic_chunk.size(), kPcmSampleRateHz)) {
        updateAudioLevel();
        if (!audio_queue.push(mic_chunk.data(), mic_chunk.size())) {
            Serial.printf("[audio] queue overflow size=%u high=%u\n", static_cast<unsigned>(audio_queue.size()),
                          static_cast<unsigned>(audio_queue.high_water()));
            cancelActiveForDeviceError(queueOverflowCode().c_str());
        }
    }
}

bool drainAudioQueue()
{
    while (ws_connected && audio_queue.pop(tx_chunk)) {
        const bool sent =
            web_socket.sendBIN(reinterpret_cast<uint8_t*>(tx_chunk.samples.data()), tx_chunk.sample_count * sizeof(int16_t));
        if (!sent) {
            Serial.printf("[audio] send failed pending=%u high=%u\n", static_cast<unsigned>(audio_queue.size()),
                          static_cast<unsigned>(audio_queue.high_water()));
            cancelActiveForDeviceError("device_send_failed");
            return false;
        }
    }
    return true;
}

void maintainConnection()
{
    if (WiFi.status() != WL_CONNECTED) {
        ws_connected = false;
        audio_queue.clear();
        loop_state.onDisconnected();
        connectWiFi();
    }

    if (!ws_started) {
        beginWebSocket();
    }
    if (!ws_connected && !ws_attempt_in_progress) {
        const uint32_t now = millis();
        if (next_reconnect_ms != 0 && static_cast<int32_t>(now - next_reconnect_ms) < 0) {
            return;
        }
        // WebSocketsClient retries inside loop(). Clear the due time before
        // allowing exactly one attempt so its disconnect callback (or the
        // fallback below) owns the next exponential-backoff deadline.
        next_reconnect_ms = 0;
        ws_attempt_in_progress = true;
    }
    web_socket.loop();
    if (!ws_connected && !ws_attempt_in_progress && next_reconnect_ms == 0) {
        scheduleReconnect();
    }
}

void maintainPlayback()
{
    if (!tts_playing || M5.Speaker.isPlaying()) {
        return;
    }
    M5.Speaker.end();
    M5.Mic.begin();
    tts_playing = false;
    tts_pcm_bytes = 0;
    Serial.println("[tts] playback complete");
}

#if defined(WALKIE_MUSE_DIRECT)
void museWorker(void*)
{
    MuseJob job;
    for (;;) {
        if (xQueueReceive(muse_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        auto* done = new MuseDone{job.kind, job.id, {}};
        switch (job.kind) {
            case MuseJobKind::Status: done->outcome = muse_client.status(); break;
            case MuseJobKind::Transcribe: done->outcome = muse_client.transcribe(tts_pcm, job.pcm_bytes); break;
            case MuseJobKind::Chat: done->outcome = muse_client.chat(muse_question); break;
            case MuseJobKind::Reminders: done->outcome = muse_client.reminders(); break;
            case MuseJobKind::MarkRead: done->outcome.ok = muse_client.markRead(job.id); break;
        }
        xQueueSend(muse_done, &done, portMAX_DELAY);
    }
}

bool postMuseJob(MuseJobKind kind, const std::string& id, uint32_t pcm_bytes)
{
    MuseJob job{kind, pcm_bytes, {}};
    std::snprintf(job.id, sizeof(job.id), "%s", id.c_str());
    if (xQueueSend(muse_jobs, &job, 0) != pdTRUE) return false;
    ++muse_pending;
    return true;
}

void beginMuse()
{
    muse_brightness = M5.Display.getBrightness();
    muse_activity_ms = millis();
    muse_client.begin(WALKIE_MUSE_HOST, WALKIE_MUSE_TOKEN);
    muse_jobs = xQueueCreate(3, sizeof(MuseJob));
    muse_done = xQueueCreate(3, sizeof(MuseDone*));
    xTaskCreatePinnedToCore(museWorker, "muse", 16384, nullptr, 1, nullptr, 0);
    if (std::strlen(WALKIE_MUSE_TOKEN) < 16) {
        loop_state.onError("", "缺少设备令牌", false);
        muse_disabled = true;
    }
}

void finishMuseUtterance()
{
    const uint32_t ms = static_cast<uint32_t>(muse_record_bytes / (kPcmSampleRateHz * sizeof(int16_t) / 1000));
    if (ms < kMuseMinUtteranceMs) {  // an accidental tap: discard without uploading
        loop_state.onKeyBCancel();
        muse_record_bytes = 0;
        Serial.printf("[muse] discarded short press ms=%lu\n", static_cast<unsigned long>(ms));
        renderUi(true);
        return;
    }
    const MuseLevel level = museLevel(reinterpret_cast<const int16_t*>(tts_pcm), muse_record_bytes / sizeof(int16_t));
    Serial.printf("[muse] level ms=%lu peak_dbfs=%.1f rms_dbfs=%.1f clipped=%lu\n", static_cast<unsigned long>(ms),
                  level.peak_dbfs, level.rms_dbfs, static_cast<unsigned long>(level.clipped));
    const std::string id = loop_state.activeId();
    if (loop_state.onKeyAUp() != LoopAction::EndUtterance) return;
    releaseAck();
    if (!postMuseJob(MuseJobKind::Transcribe, id, muse_record_bytes)) loop_state.onError(id, "设备忙，请稍后再说", true);
    renderUi(true);
}

void sendConfirmedQuestion()
{
    const std::string id = muse_confirm_id;
    muse_confirm_id.clear();
    if (!loop_state.onMuseConfirmed(id)) return;
    if (!postMuseJob(MuseJobKind::Chat, id, 0)) {
        muse_question.clear();
        loop_state.onError(id, "设备忙，请稍后再说", true);
    }
    renderUi(true);
}

void cancelConfirm()
{
    muse_confirm_id.clear();
    muse_question.clear();
    loop_state.onKeyBCancel();  // ProposalReview -> Cancelled
    loop_state.onKeyBCancel();  // Cancelled -> Ready, no model call
    renderUi(true);
}

// Leaving a result/error page; a reminder card is marked read when it is dismissed.
void dismissResult()
{
    if (muse_showing_reminder && !muse_reminder_id.empty()) {
        postMuseJob(MuseJobKind::MarkRead, muse_reminder_id, 0);
        muse_reminder_id.clear();
        muse_reminder_entries.clear();
        muse_unread = std::max(0, muse_unread - 1);
    }
    muse_showing_reminder = false;
}

// Three abstract keys (openspec openmuse-device-interaction): KEYA = talk / confirm, KEYB = cancel / back.
void handleMuseButtons()
{
    const DeviceState state = loop_state.state();
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) muse_activity_ms = millis();
    if (M5.BtnA.wasPressed()) {
        if (state == DeviceState::ProposalReview) {
            sendConfirmedQuestion();
        } else if (loop_state.connected()) {
            const std::string id = makeUtteranceId(device_id.c_str(), ++utterance_seq);
            LoopAction action = loop_state.onKeyADown(id);
            if (action == LoopAction::Acknowledge) {  // holding talk on a result starts the next question
                dismissResult();
                action = loop_state.onKeyADown(id);
            }
            if (action == LoopAction::StartUtterance) muse_record_bytes = 0;
            renderUi(true);
        }
    }
    if (M5.BtnB.wasPressed()) {
        if (state == DeviceState::Recording && loop_state.onKeyBCancel() == LoopAction::CancelUtterance) {
            muse_record_bytes = 0;
        } else if (state == DeviceState::ProposalReview) {
            cancelConfirm();
        } else if (state == DeviceState::Transcribing) {
            const std::string id = loop_state.activeId();
            if (loop_state.onMuseAbandon()) {  // stop waiting; the request finishes in the background, unseen
                muse_abandoned_id = id;
                muse_question.clear();
                Serial.println("[muse] abandoned while thinking");
            }
        } else if (state == DeviceState::Result && muse_showing_reminder &&
                   muse_reminder_index + 1 < muse_reminder_entries.size()) {
            ++muse_reminder_index;  // KEYB pages through the card; on the last entry it closes it
        } else if ((state == DeviceState::Result || state == DeviceState::Error) && loop_state.connected()) {
            dismissResult();
            loop_state.onKeyADown("");  // acknowledge -> Ready
        } else if (state == DeviceState::Ready && !muse_reminder_entries.empty() &&
                   loop_state.onMuseReminder(muse_reminder_entries.front())) {
            muse_showing_reminder = true;
            muse_reminder_index = 0;
        }
        renderUi(true);
    }
    if (M5.BtnA.wasReleased() && loop_state.state() == DeviceState::Recording) finishMuseUtterance();
    if (loop_state.state() == DeviceState::ProposalReview && !muse_confirm_id.empty() &&
        static_cast<int32_t>(millis() - muse_confirm_deadline_ms) >= 0) {
        sendConfirmedQuestion();
    }
}

void captureMuse()
{
    if (loop_state.state() != DeviceState::Recording) return;
    if (!M5.Mic.isEnabled() || !tts_pcm) {
        loop_state.onError(loop_state.activeId(), "麦克风不可用", true);
        renderUi(true);
        return;
    }
    if (!M5.Mic.record(mic_chunk.data(), mic_chunk.size(), kPcmSampleRateHz)) return;
    updateAudioLevel();
    const size_t bytes = mic_chunk.size() * sizeof(int16_t);
    if (muse_record_bytes + bytes > kMaxTtsPcmBytes) {  // 30 s cap: same as releasing KEYA
        finishMuseUtterance();
        return;
    }
    std::memcpy(tts_pcm + muse_record_bytes, mic_chunk.data(), bytes);
    muse_record_bytes += bytes;
}

void maintainMuse()
{
    if (WiFi.status() != WL_CONNECTED) {
        loop_state.onDisconnected();
        connectWiFi();
        muse_next_status_ms = 0;
    }
    MuseDone* done = nullptr;
    while (xQueueReceive(muse_done, &done, 0) == pdTRUE) {
        --muse_pending;
        const MuseOutcome& out = done->outcome;
        if (!muse_abandoned_id.empty() && done->id == muse_abandoned_id) {
            Serial.printf("[muse] done kind=%d ignored: abandoned\n", static_cast<int>(done->kind));
            muse_abandoned_id.clear();
            delete done;
            continue;
        }
        if (done->kind == MuseJobKind::Status) {
            const bool idle = loop_state.state() == DeviceState::Connecting || loop_state.state() == DeviceState::Ready ||
                              loop_state.state() == DeviceState::Error;
            if (out.ok) {
                // Only recover from a connection failure; never wipe an answer-level error the user is reading.
                if (!loop_state.connected()) loop_state.onConnected();
                muse_next_status_ms = millis() + kMuseStatusIdleMs;
                postMuseJob(MuseJobKind::Reminders, "", 0);
            } else if (idle) {
                loop_state.onDisconnected();
                loop_state.onError("", out.text, true);
                muse_next_status_ms = millis() + kMuseStatusRetryMs;
            }
        } else if (done->kind == MuseJobKind::Reminders) {
            if (out.ok) {
                muse_unread = out.unread;
                if (!muse_showing_reminder) {  // never swap the card someone is reading
                    muse_reminder_id = out.reminder_id;
                    muse_reminder_entries = out.reminder_id.empty() ? std::vector<std::string>() : out.entries;
                }
            }
        } else if (done->kind == MuseJobKind::MarkRead) {
            postMuseJob(MuseJobKind::Reminders, "", 0);
        } else if (out.ok && done->kind == MuseJobKind::Transcribe) {
            // Confirm step: show what was heard and count down before any model call.
            muse_question = out.question;
            muse_heard = out.heard;
            if (loop_state.onTranscript(done->id, muse_heard) && loop_state.onProposal(done->id, muse_heard)) {
                muse_confirm_id = done->id;
                muse_confirm_deadline_ms = millis() + kMuseConfirmMs;
            } else {
                muse_question.clear();
            }
        } else if (out.ok) {
            muse_question.clear();
            // The answer page: the heard question as its label (so mishearing is visible), then the answer.
            if (loop_state.onTranscript(done->id, out.text)) releaseAck();
            muse_next_status_ms = millis() + kMuseStatusIdleMs;
        } else {
            muse_question.clear();
            loop_state.onError(done->id, out.text, true);
            muse_next_status_ms = millis() + kMuseStatusIdleMs;
            if (out.status == 401 || out.status == 403 || (out.status == 0 && !out.slow_network)) {
                loop_state.onDisconnected();
                loop_state.onError("", out.text, true);
                muse_next_status_ms = millis() + kMuseStatusRetryMs;
            }
        }
        Serial.printf("[muse] done kind=%d ok=%d status=%d\n", static_cast<int>(done->kind), out.ok, out.status);
        delete done;
        renderUi(true);
    }
    const DeviceState state = loop_state.state();
    static DeviceState last_state = state;
    if (state != last_state) {
        last_state = state;
        muse_activity_ms = millis();
    }
    const bool asleep = state == DeviceState::Ready && millis() - muse_activity_ms > kMuseSleepMs;
    if (asleep != muse_asleep) {
        muse_asleep = asleep;
        M5.Display.setBrightness(asleep ? kMuseDimBrightness : muse_brightness);
        Serial.printf("[ui] %s\n", asleep ? "idle: dimmed" : "awake");
    }
    const bool busy = state == DeviceState::Recording || state == DeviceState::Transcribing ||
                      state == DeviceState::ProposalReview;
    if (!muse_disabled && !busy && muse_pending == 0 && static_cast<int32_t>(millis() - muse_next_status_ms) >= 0) {
        if (postMuseJob(MuseJobKind::Status, "", 0)) muse_next_status_ms = millis() + kMuseStatusIdleMs;
    }
}
#endif

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(200);
    device_id = deriveDeviceId();
    tts_pcm = static_cast<uint8_t*>(heap_caps_malloc(kMaxTtsPcmBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    Serial.printf("[tts] psram buffer %s bytes=%u\n", tts_pcm ? "ready" : "failed",
                  static_cast<unsigned>(kMaxTtsPcmBytes));
    initHardware();
    renderUi(true);
#if defined(WALKIE_UI_DEMO)
    Serial.println("[ui-demo] offline showcase ready; KEYA=next KEYB=back");
#elif defined(WALKIE_MUSE_DIRECT)
    if (museFontsBegin()) renderUi(true);
    connectWiFi();
    beginMuse();
#else
    connectWiFi();
    web_socket.onEvent(onWebSocketEvent);
    beginWebSocket();
#endif
}

void loop()
{
    M5.update();
#if defined(WALKIE_UI_DEMO)
    handleDemoButtons();
    captureDemoMic();
#elif defined(WALKIE_MUSE_DIRECT)
    maintainMuse();
    handleMuseButtons();
    captureMuse();
#else
    maintainConnection();
    maintainPlayback();
    handleButtons();
    captureIfRecording();
    drainAudioQueue();
#endif
    renderUi();
    delay(1);
}
