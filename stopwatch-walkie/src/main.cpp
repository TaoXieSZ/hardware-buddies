#include "audio_constants.h"
#include "audio_loop.h"
#include "audio_queue.h"
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
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5IOE1.h>
#include <M5Unified.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <algorithm>
#include <array>
#include <cstring>

using namespace stopwatch;

namespace {

constexpr uint8_t kIoeAddrPrimary = 0x4F;
constexpr uint8_t kIoeAddrSecondary = 0x6F;
constexpr uint8_t kMotorPin = M5IOE1_PIN_9;
constexpr uint8_t kMotorPwmChannel = 0;
constexpr uint16_t kMotorPwmHz = 5000;

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
uint16_t ui_audio_level = 0;

#if defined(WALKIE_UI_DEMO)
constexpr std::array<DeviceState, 6> kDemoStates = {
    DeviceState::Connecting, DeviceState::Ready,         DeviceState::Recording,
    DeviceState::Transcribing, DeviceState::Result,      DeviceState::Error,
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
    Serial.printf("[ws] tx %s %s\n", sent ? "ok" : "failed", payload.c_str());
    return sent;
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
    static constexpr std::array<const char*, 6> kDemoDetails = {
        "Searching for your Mac", "Press A to explore", "Speak to test the mic",
        "Sending audio securely", "Agent reply received", "bridge_unreachable",
    };
    model.state = kDemoStates[demo_state_index];
    model.detail = kDemoDetails[demo_state_index];
    model.retryable = true;
    model.showcase = true;
#else
    String detail;
    model.state = loop_state.state();
    model.retryable = loop_state.retryableError();
    if (model.state == DeviceState::Connecting) {
        detail = String(WALKIE_WS_HOST) + ":" + String(WALKIE_WS_PORT);
    } else if (model.state == DeviceState::Result) {
        detail = String(loop_state.resultText().c_str());
    } else if (model.state == DeviceState::Error) {
        detail = String(loop_state.errorCode().c_str());
    }
    model.detail = detail.c_str();
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
    return sendText(utteranceStartMessage(id));
}

bool sendEnd(const std::string& id)
{
    return sendText(utteranceEndMessage(id));
}

bool sendCancel(const std::string& id)
{
    return sendText(utteranceCancelMessage(id));
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

    const char* type = doc["type"] | "";
    const char* id = doc["id"] | "";
    if (std::strcmp(type, "transcript") == 0) {
        const char* text = doc["text"] | "";
        if (!loop_state.onTranscript(id, text)) {
            Serial.printf("[ws] ignored transcript for id=%s\n", id);
            return;
        }
        renderUi(true);
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
            resetBackoff();
            loop_state.onConnected();
            sendText(helloMessage(device_id.c_str()));
            renderUi(true);
            Serial.println("[ws] connected");
            break;
        case WStype_DISCONNECTED:
            ws_connected = false;
            audio_queue.clear();
            loop_state.onDisconnected();
            scheduleReconnect();
            renderUi(true);
            Serial.println("[ws] disconnected");
            break;
        case WStype_TEXT:
            handleTextFrame(payload, length);
            break;
        default:
            break;
    }
}

void connectWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("[wifi] connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        M5.update();
        renderUi();
    }
    Serial.printf("[wifi] connected ip=%s\n", WiFi.localIP().toString().c_str());
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
    if (M5.BtnA.wasPressed()) {
        const std::string id = makeUtteranceId(device_id.c_str(), ++utterance_seq);
        if (loop_state.onKeyADown(id) == LoopAction::StartUtterance) {
            audio_queue.clear();
            if (sendStart(id)) {
                renderUi(true);
            } else {
                cancelActiveForDeviceError("device_control_send_failed");
            }
        } else {
            renderUi(true);
        }
    }

    if (M5.BtnB.wasPressed()) {
        const std::string id = loop_state.activeId();
        if (loop_state.onKeyBCancel() == LoopAction::CancelUtterance) {
            if (!sendCancel(id)) {
                Serial.printf("[ws] cancel send failed id=%s\n", id.c_str());
            }
            audio_queue.clear();
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

    if (!ws_connected && (!ws_started || millis() >= next_reconnect_ms)) {
        beginWebSocket();
        scheduleReconnect();
    }
    web_socket.loop();
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(200);
    device_id = deriveDeviceId();
    initHardware();
    renderUi(true);
#if defined(WALKIE_UI_DEMO)
    Serial.println("[ui-demo] offline showcase ready; KEYA=next KEYB=back");
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
#else
    maintainConnection();
    handleButtons();
    captureIfRecording();
    drainAudioQueue();
#endif
    renderUi();
    delay(1);
}
