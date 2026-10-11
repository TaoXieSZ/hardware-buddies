#pragma once

// HTTPS client for the direct-to-Muse build: personal cloud status, transcribe and chat.
// Logs stages, status codes, byte counts and durations only — never audio, text or the token.

#if defined(WALKIE_MUSE_DIRECT)

#include "muse_text.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <vector>

namespace stopwatch {

struct MuseOutcome {
    bool ok = false;           // true: `text` is the answer to read; false: `text` is the error reason
    MuseStage stage = MuseStage::Status;
    int status = 0;            // last HTTP status, 0 when no response arrived
    std::string text;
    std::string heard;              // "问：…" line once transcription succeeded
    std::string question;           // raw transcript, kept in RAM only until the chat request
    int unread = 0;                 // reminders only
    std::string reminder_id;        // reminders only: newest unread card
    std::vector<std::string> entries;  // reminders only: that card's entries, one reading page each
    bool transcribe_ready = false;  // status probe only
    bool slow_network = false;      // the upload gave up: the cloud is reachable, the link is just weak
};

class MuseClient {
public:
    void begin(const char* host, const char* token);
    /** GET /device/v1/status; ok when 200 and transcription is configured. */
    MuseOutcome status();
    /** WAV upload to /v1/transcribe; ok with `question` and the "问：…" `heard` line. */
    MuseOutcome transcribe(const uint8_t* pcm, uint32_t pcm_bytes);
    /** Protocol-2 /v1/chat with one request ID (202 and a dropped connection replay it); returns the brief answer. */
    MuseOutcome chat(const std::string& question);
    /** GET /v1/reminders: unread count, plus the newest unread card's id and entries. */
    MuseOutcome reminders();
    /** POST /v1/reminders/:id/read. */
    bool markRead(const std::string& reminder_id);

private:
    struct Response {
        int status = 0;
        uint32_t retry_after = 0;
        String body;
    };
    bool connect();
    bool send(const char* method, const char* path, const char* content_type, const uint8_t* head, size_t head_len,
              const uint8_t* body, size_t body_len, const char* extra_header);
    bool receive(Response& response);
    bool request(const char* method, const char* path, const char* content_type, const uint8_t* head,
                 size_t head_len, const uint8_t* body, size_t body_len, const char* extra_header, Response& response);

    bool upload_timed_out_ = false;  // the last send() gave up after kMuseUploadLimitMs
    WiFiClientSecure tls_;
    IPAddress resolved_;
    String host_;
    String token_;
};

}  // namespace stopwatch

#endif
