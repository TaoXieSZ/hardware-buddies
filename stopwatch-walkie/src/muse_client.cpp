#include "muse_client.h"

#if defined(WALKIE_MUSE_DIRECT)

#include "muse_certs.h"

#include <ArduinoJson.h>
#include <esp_random.h>

namespace stopwatch {
namespace {

constexpr uint32_t kResponseLimitMs = 60000;
constexpr size_t kUploadChunk = 4096;
constexpr size_t kMaxBodyBytes = 16 * 1024;
constexpr size_t kMaxReminderEntries = 8;

}  // namespace

void MuseClient::begin(const char* host, const char* token)
{
    host_ = host;
    token_ = token;
    tls_.setCACert(kMuseRootCa);
    tls_.setHandshakeTimeout(30);
}

bool MuseClient::connect()
{
    if (tls_.connected()) return true;
    tls_.stop();
    const uint32_t started = millis();
    const bool ok = tls_.connect(host_.c_str(), 443);
    char reason[96] = "";
    if (!ok) tls_.lastError(reason, sizeof(reason));  // mbedTLS/socket reason only, never request data
    Serial.printf("[muse] tls connect ok=%d ms=%lu ip=%s err=%s\n", ok, static_cast<unsigned long>(millis() - started),
                  WiFi.hostByName(host_.c_str(), resolved_) ? resolved_.toString().c_str() : "unresolved", reason);
    return ok;
}

bool MuseClient::send(const char* method, const char* path, const char* content_type, const uint8_t* head,
                      size_t head_len, const uint8_t* body, size_t body_len, const char* extra_header)
{
    String req;
    req.reserve(384);
    req += method;
    req += " ";
    req += path;
    req += " HTTP/1.1\r\nHost: ";
    req += host_;
    req += "\r\nAuthorization: Bearer ";
    req += token_;
    req += "\r\nAccept: application/json\r\nConnection: keep-alive\r\nX-OpenMuse-Firmware: stopwatch-muse-0.1\r\n";
    if (extra_header) req += extra_header;
    if (content_type) {
        req += "Content-Type: ";
        req += content_type;
        req += "\r\nContent-Length: ";
        req += String(static_cast<unsigned long>(head_len + body_len));
        req += "\r\n";
    }
    req += "\r\n";
    const size_t size = req.length();
    const bool header_ok = tls_.write(reinterpret_cast<const uint8_t*>(req.c_str()), size) == size;
    req.clear();  // do not keep the token in memory longer than needed
    if (!header_ok) return false;
    if (head_len && tls_.write(head, head_len) != head_len) return false;
    const uint32_t started = millis();
    for (size_t sent = 0; sent < body_len;) {
        if (millis() - started > kMuseUploadLimitMs) {  // a very weak link: give up instead of hanging
            upload_timed_out_ = true;
            Serial.printf("[muse] upload too slow sent=%u of %u ms=%lu rssi=%d\n", static_cast<unsigned>(sent),
                          static_cast<unsigned>(body_len), static_cast<unsigned long>(millis() - started), WiFi.RSSI());
            return false;
        }
        const size_t n = std::min(kUploadChunk, body_len - sent);
        if (tls_.write(body + sent, n) != n) return false;
        sent += n;
    }
    return true;
}

bool MuseClient::receive(Response& response)
{
    const uint32_t started = millis();
    while (!tls_.available()) {
        if (!tls_.connected() || millis() - started > kResponseLimitMs) return false;
        delay(2);
    }
    tls_.setTimeout(15);  // seconds
    const String status_line = tls_.readStringUntil('\n');
    response.status = status_line.startsWith("HTTP/1.") ? status_line.substring(9, 12).toInt() : 0;
    long length = -1;
    bool chunked = false;
    bool close = false;
    while (true) {
        String line = tls_.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) break;
        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("content-length:")) length = line.substring(15).toInt();
        else if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") > 0) chunked = true;
        else if (lower.startsWith("connection:") && lower.indexOf("close") > 0) close = true;
        else if (lower.startsWith("retry-after:")) response.retry_after = line.substring(12).toInt();
    }
    char buf[256];
    auto take = [&](long n) {
        while (n > 0) {
            const size_t got = tls_.readBytes(buf, std::min<long>(n, sizeof(buf)));
            if (got == 0) return false;
            if (response.body.length() + got <= kMaxBodyBytes) response.body.concat(buf, got);
            n -= static_cast<long>(got);
        }
        return true;
    };
    bool ok = response.status > 0;
    if (ok && chunked) {
        while (ok) {
            const long n = strtol(tls_.readStringUntil('\n').c_str(), nullptr, 16);
            if (n <= 0) {
                tls_.readStringUntil('\n');
                break;
            }
            ok = take(n);
            tls_.readStringUntil('\n');
        }
    } else if (ok && length >= 0) {
        ok = take(length);
    }
    if (close || !ok) tls_.stop();
    return ok;
}

bool MuseClient::request(const char* method, const char* path, const char* content_type, const uint8_t* head,
                         size_t head_len, const uint8_t* body, size_t body_len, const char* extra_header,
                         Response& response)
{
    // A reused keep-alive connection may have been closed by the server: retry once on a fresh one.
    upload_timed_out_ = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        response = Response();
        const bool reused = tls_.connected();
        if (!connect()) return false;
        const uint32_t started = millis();
        if (send(method, path, content_type, head, head_len, body, body_len, extra_header) && receive(response)) {
            Serial.printf("[muse] %s %s status=%d up=%u down=%u ms=%lu rssi=%d\n", method, path, response.status,
                          static_cast<unsigned>(head_len + body_len), static_cast<unsigned>(response.body.length()),
                          static_cast<unsigned long>(millis() - started), WiFi.RSSI());
            return true;
        }
        tls_.stop();
        if (!reused || upload_timed_out_) return false;  // a slow link would only be slow again
    }
    return false;
}

MuseOutcome MuseClient::status()
{
    MuseOutcome out;
    out.stage = MuseStage::Status;
    Response response;
    if (!request("GET", "/device/v1/status", nullptr, nullptr, 0, nullptr, 0, nullptr, response)) {
        out.text = museFailureReason(MuseStage::Status, 0);
        return out;
    }
    out.status = response.status;
    if (response.status != 200) {
        out.text = museFailureReason(MuseStage::Status, response.status);
        return out;
    }
    JsonDocument doc;
    out.transcribe_ready = !deserializeJson(doc, response.body) && (doc["transcribeConfigured"] | false);
    out.ok = out.transcribe_ready;
    if (!out.ok) out.text = "个人云未开启语音转写";
    return out;
}

MuseOutcome MuseClient::transcribe(const uint8_t* pcm, uint32_t pcm_bytes)
{
    MuseOutcome out;
    uint8_t wav[44];
    museWavHeader(wav, pcm_bytes);

    out.stage = MuseStage::Transcribe;
    Response response;
    if (!request("POST", "/device/v1/transcribe", "audio/wav", wav, sizeof(wav), pcm, pcm_bytes, nullptr, response)) {
        out.text = upload_timed_out_ ? kMuseSlowNetwork : museFailureReason(out.stage, 0);
        out.slow_network = upload_timed_out_;
        return out;
    }
    out.status = response.status;
    if (response.status != 200) {
        out.text = museFailureReason(out.stage, response.status);
        return out;
    }
    std::string question;
    {
        JsonDocument doc;
        if (!deserializeJson(doc, response.body)) question = doc["text"] | "";
    }
    if (question.empty()) {
        out.text = kMuseEmptyTranscript;
        return out;
    }
    out.ok = true;
    out.heard = museHeardLine(question);
    out.question = question;
    return out;
}

MuseOutcome MuseClient::chat(const std::string& question_text)
{
    MuseOutcome out;
    std::string question = question_text;
    Response response;

    // One request ID for this utterance: 202 polling and a dropped connection replay it, never pay twice.
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    JsonDocument payload;
    payload["requestId"] = museRequestId(random);
    payload["question"] = question;
    payload["scope"] = "personal";
    payload["briefChars"] = kMuseBriefChars;
    String body;
    serializeJson(payload, body);
    question.clear();

    out.stage = MuseStage::Chat;
    const uint32_t deadline = millis() + kResponseLimitMs;
    bool retried = false;
    while (true) {
        const bool ok = request("POST", "/device/v1/chat", "application/json", nullptr, 0,
                                reinterpret_cast<const uint8_t*>(body.c_str()), body.length(),
                                "X-OpenMuse-Protocol: 2\r\n", response);
        if (!ok) {
            if (!retried && static_cast<int32_t>(deadline - millis()) > 0) {
                retried = true;
                continue;
            }
            out.text = museFailureReason(out.stage, 0);
            return out;
        }
        if (response.status == 202 && static_cast<int32_t>(deadline - millis()) > 0) {
            delay(std::min<uint32_t>(5, response.retry_after ? response.retry_after : 3) * 1000);
            continue;
        }
        break;
    }
    out.status = response.status;
    if (response.status != 200) {
        out.text = museFailureReason(out.stage, response.status);
        return out;
    }
    JsonDocument filter;
    filter["brief"] = true;
    filter["answer"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, response.body, DeserializationOption::Filter(filter))) {
        out.text = museFailureReason(out.stage, 0);
        return out;
    }
    std::string brief = doc["brief"] | "";
    if (brief.empty()) brief = doc["answer"] | "";
    if (brief.empty()) {
        out.text = "Muse 没有给出回答";
        return out;
    }
    out.ok = true;
    out.text = museBodyText(brief);
    return out;
}

MuseOutcome MuseClient::reminders()
{
    MuseOutcome out;
    out.stage = MuseStage::Status;
    Response response;
    if (!request("GET", "/device/v1/reminders", nullptr, nullptr, 0, nullptr, 0, nullptr, response) ||
        response.status != 200) {
        out.status = response.status;
        return out;
    }
    out.status = 200;
    JsonDocument filter;
    filter["unread"] = true;
    filter["cards"][0]["id"] = true;
    filter["cards"][0]["title"] = true;
    filter["cards"][0]["read"] = true;
    filter["cards"][0]["entries"][0]["text"] = true;
    filter["cards"][0]["entries"][0]["title"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, response.body, DeserializationOption::Filter(filter))) return out;
    out.ok = true;
    out.unread = doc["unread"] | 0;
    for (JsonObject card : doc["cards"].as<JsonArray>()) {
        if (card["read"] | false) continue;
        out.reminder_id = card["id"] | "";
        for (JsonObject entry : card["entries"].as<JsonArray>()) {
            std::string text = entry["text"] | "";
            if (text.empty()) text = entry["title"] | "";
            if (!text.empty()) out.entries.push_back(museBodyText(text));
            if (out.entries.size() == kMaxReminderEntries) break;
        }
        if (out.entries.empty()) out.entries.push_back(museBodyText(card["title"] | "提醒"));
        break;
    }
    return out;
}

bool MuseClient::markRead(const std::string& reminder_id)
{
    Response response;
    const String path = String("/device/v1/reminders/") + reminder_id.c_str() + "/read";
    static const uint8_t kEmpty[] = {'{', '}'};
    return request("POST", path.c_str(), "application/json", nullptr, 0, kEmpty, sizeof(kEmpty), nullptr, response) &&
           response.status == 200;
}

}  // namespace stopwatch

#endif
