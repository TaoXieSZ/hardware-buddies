#pragma once

#include "audio_constants.h"

#include <cstdio>
#include <string>

namespace stopwatch {

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
