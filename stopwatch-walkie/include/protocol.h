#pragma once

#include "audio_constants.h"

#include <cstdio>
#include <cstdint>
#include <string>

namespace stopwatch {

constexpr uint8_t kProtocolV1 = 1;
constexpr uint8_t kProtocolV2 = 2;
constexpr size_t kControlSecretBytes = 32;
constexpr size_t kControlNonceBytes = 24;
constexpr size_t kMaxDeviceId = 64;
constexpr size_t kMaxSessionId = 64;
constexpr size_t kMaxCommandId = 64;
constexpr size_t kMaxTaskId = 64;
constexpr size_t kMaxLabel = 48;
constexpr size_t kMaxCommandText = 1024;
constexpr size_t kMaxPreview = 240;
constexpr size_t kMaxSummary = 512;
constexpr size_t kMaxErrorDetail = 160;
constexpr size_t kMaxControlBodyBytes = 4096;

inline std::string envelopeMacInput(const std::string& direction,
                                    const std::string& session_id,
                                    uint64_t sequence,
                                    const std::string& body_base64url)
{
    return "walkie-v2\n" + direction + "\n" + session_id + "\n" +
           std::to_string(sequence) + "\n" + body_base64url;
}

inline std::string authMacInput(const std::string& role,
                                const std::string& device_id,
                                const std::string& device_nonce,
                                const std::string& bridge_nonce,
                                const std::string& session_id)
{
    return "walkie-v2-auth\n" + role + "\n" + device_id + "\n" + device_nonce +
           "\n" + bridge_nonce + "\n" + session_id;
}

inline std::string jsonEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

inline std::string helloMessage(const std::string& device_id)
{
    return "{\"type\":\"hello\",\"protocol\":1,\"device_id\":\"" + jsonEscape(device_id) + "\"}";
}

inline std::string utteranceStartMessage(const std::string& id)
{
    return "{\"type\":\"utterance.start\",\"id\":\"" + jsonEscape(id) +
           "\",\"audio\":{\"rate\":16000,\"bits\":16,\"channels\":1,\"encoding\":\"pcm_s16le\"}}";
}

inline std::string utteranceEndMessage(const std::string& id)
{
    return "{\"type\":\"utterance.end\",\"id\":\"" + jsonEscape(id) + "\"}";
}

inline std::string utteranceCancelMessage(const std::string& id)
{
    return "{\"type\":\"utterance.cancel\",\"id\":\"" + jsonEscape(id) + "\"}";
}

inline std::string queueOverflowCode()
{
    return "device_queue_overflow";
}

inline std::string makeUtteranceId(const std::string& device_id, uint32_t sequence)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "-%08lu", static_cast<unsigned long>(sequence));
    return device_id + buf;
}

}  // namespace stopwatch
