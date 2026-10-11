#pragma once

// Pure helpers for the direct-to-Muse build (WALKIE_MUSE_DIRECT): no Arduino, native-testable.
// Spec: openspec/changes/add-stopwatch-muse-direct/specs/stopwatch-muse-direct/spec.md

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace stopwatch {

constexpr uint32_t kMuseMinUtteranceMs = 500;
constexpr uint32_t kMuseMaxUtteranceMs = 30000;
constexpr int kMuseBriefChars = 40;

enum class MuseStage { Status, Transcribe, Chat };

/** Number of UTF-8 code points in `text`. */
inline size_t utf8Length(const std::string& text)
{
    size_t count = 0;
    for (unsigned char c : text) count += (c & 0xC0) != 0x80;
    return count;
}

/** 44-byte RIFF/WAVE header for 16 kHz mono PCM16 with `pcm_bytes` of samples. */
inline void museWavHeader(uint8_t out[44], uint32_t pcm_bytes)
{
    auto le32 = [](uint8_t* p, uint32_t v) {
        for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
    };
    auto le16 = [](uint8_t* p, uint16_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
    };
    const char* tags = "RIFF\0\0\0\0WAVEfmt ";
    for (int i = 0; i < 16; ++i) out[i] = static_cast<uint8_t>(tags[i]);
    le32(out + 4, 36 + pcm_bytes);
    le32(out + 16, 16);
    le16(out + 20, 1);                // PCM
    le16(out + 22, 1);                // mono
    le32(out + 24, 16000);            // sample rate
    le32(out + 28, 16000 * 2);        // byte rate
    le16(out + 32, 2);                // block align
    le16(out + 34, 16);               // bits per sample
    out[36] = 'd';
    out[37] = 'a';
    out[38] = 't';
    out[39] = 'a';
    le32(out + 40, pcm_bytes);
}

/** RFC 4122 version-4 UUID from 16 random bytes (the server validates requestId as a UUID). */
inline std::string museRequestId(const uint8_t random[16])
{
    uint8_t b[16];
    for (int i = 0; i < 16; ++i) b[i] = random[i];
    b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);
    char out[37];
    std::snprintf(out, sizeof(out), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
                  b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return out;
}

/** Plain-language reason shown on the error page; `status` 0 means the request never got an answer. */
inline std::string museFailureReason(MuseStage stage, int status)
{
    if (status == 0) return "连不上个人云";
    if (status == 401 || status == 403) return "设备需要重新配对";
    if (status == 429) return stage == MuseStage::Transcribe ? "今天的转写次数用完了" : "今天的提问次数用完了";
    if (stage == MuseStage::Transcribe && (status == 502 || status == 503 || status == 504)) return "转写服务暂不可用";
    if (stage == MuseStage::Transcribe && (status == 413 || status == 422)) return "录音无法识别，请重说";
    if (stage == MuseStage::Chat && (status == 502 || status == 503)) return "Muse 暂时不可用";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "个人云出错 %d", status);
    return buf;
}

constexpr const char* kMuseEmptyTranscript = "没听清，请再说一次";
constexpr const char* kMuseSlowNetwork = "网络太慢，靠近路由器再试";
constexpr uint32_t kMuseUploadLimitMs = 30000;  // give up an upload that has not finished by then

constexpr size_t kMuseBodyChars = 200;   // reading pages wrap and shrink the text; this only bounds memory
constexpr size_t kMuseHeardChars = 60;   // the "问：" label is clipped to the screen width when drawn

/** Whitespace collapsed to single spaces, clipped to `limit` code points ending in "…". */
inline std::string museBodyText(const std::string& raw, size_t limit = kMuseBodyChars)
{
    std::string text;
    bool space = false;
    for (char c : raw) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            space = !text.empty();
            continue;
        }
        if (space) text.push_back(' ');
        space = false;
        text.push_back(c);
    }
    if (utf8Length(text) <= limit) return text;
    std::string out;
    size_t chars = 0;
    for (char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80 && ++chars > limit - 1) break;
        out.push_back(c);
    }
    return out + "…";
}

/** "问：" plus the recognized question, so mishearing is visible at a glance. */
inline std::string museHeardLine(const std::string& question)
{
    return museBodyText("问：" + question, kMuseHeardChars);
}

struct MuseLevel {
    float peak_dbfs = -120.0F;
    float rms_dbfs = -120.0F;
    uint32_t clipped = 0;  // samples within 1% of full scale
};

/** Loudness of one utterance: numbers only, never the audio itself. */
inline MuseLevel museLevel(const int16_t* samples, size_t count)
{
    MuseLevel level;
    if (count == 0) return level;
    double sum = 0;
    int32_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t v = samples[i] < 0 ? -static_cast<int32_t>(samples[i]) : samples[i];
        peak = v > peak ? v : peak;
        sum += static_cast<double>(v) * v;
        if (v >= 32440) ++level.clipped;
    }
    auto dbfs = [](double amplitude) {
        return amplitude <= 0 ? -120.0F : static_cast<float>(20.0 * std::log10(amplitude / 32768.0));
    };
    level.peak_dbfs = dbfs(peak);
    level.rms_dbfs = dbfs(std::sqrt(sum / static_cast<double>(count)));
    return level;
}

}  // namespace stopwatch
