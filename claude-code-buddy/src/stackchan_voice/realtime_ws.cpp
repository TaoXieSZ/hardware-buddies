#include "realtime_ws.h"

#include <M5Unified.h>
#include <WebSocketsClient.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>

#include "dashscope_ca.h"

namespace {

constexpr const char* HOST = "dashscope.aliyuncs.com";
constexpr const char* PATH =
    "/api-ws/v1/realtime?model=qwen-audio-3.0-realtime-flash";

// 人设定稿（任务 1.4，用户 2026-07-18 拍板；音色 longpaopao_v3.6）。
// 与 tools/voice-prototype/talk_once.py 的 INSTRUCTIONS 保持一致。
constexpr const char* VOICE = "longpaopao_v3.6";
constexpr const char* INSTRUCTIONS =
    "你是小抓，一只住在主人桌面上的黑白美短小猫桌宠（StackChan 机器人）。"
    "性格活泼粘人，说话口语化、偶尔卖萌，中文回答。"
    "硬性要求：每次回答不超过 3 句话，不要列清单，不要长篇大论。";

WebSocketsClient s_ws;
RtCallbacks s_cb{};
bool s_connected = false;
bool s_session_ready = false;

// base64 编解码走 PSRAM scratch：上行 100ms 帧（3200B→4.3KB b64+封包），
// 下行 delta 最大 ~25KB b64 → ~19KB PCM（实测上限，48KB 留裕量）。
char*    s_tx = nullptr;             // 上行 JSON 组装
uint8_t* s_rx_pcm = nullptr;         // 下行解码输出
constexpr size_t TX_CAP = 12 * 1024;
constexpr size_t RX_PCM_CAP = 40 * 1024;

// 在 payload 中定位 "key":"<value>"，返回 value 起点与长度（不拷贝）。
const char* findStr(const char* hay, const char* key, size_t* out_len) {
  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char* p = strstr(hay, pat);
  if (!p) return nullptr;
  p += strlen(pat);
  const char* q = strchr(p, '"');
  if (!q) return nullptr;
  *out_len = q - p;
  return p;
}

bool typeIs(const char* payload, const char* type) {
  char pat[64];
  snprintf(pat, sizeof(pat), "\"type\":\"%s\"", type);
  return strstr(payload, pat) != nullptr;
}

void sendSessionUpdate() {
  // Manual/PTT：turn_detection 必须显式 null（服务端默认 server_vad，实测）。
  int n = snprintf(s_tx, TX_CAP,
      "{\"type\":\"session.update\",\"session\":{"
      "\"modalities\":[\"text\",\"audio\"],"
      "\"voice\":\"%s\","
      "\"input_audio_format\":\"pcm\",\"output_audio_format\":\"pcm\","
      "\"instructions\":\"%s\","
      "\"turn_detection\":null}}",
      VOICE, INSTRUCTIONS);
  s_ws.sendTXT((uint8_t*)s_tx, n);
}

void handleText(uint8_t* payload, size_t length) {
  const char* p = (const char*)payload;  // 库保证 TEXT 帧 NUL 结尾

  if (typeIs(p, "response.audio.delta")) {
    size_t b64_len = 0;
    const char* b64 = findStr(p, "audio", &b64_len);
    if (!b64) b64 = findStr(p, "delta", &b64_len);   // 字段名两种形态都见过
    if (b64 && b64_len) {
      size_t out = 0;
      if (mbedtls_base64_decode(s_rx_pcm, RX_PCM_CAP, &out,
                                (const uint8_t*)b64, b64_len) == 0 && out) {
        if (s_cb.onAudio) s_cb.onAudio(s_rx_pcm, out);
      } else {
        Serial.printf("[rt] b64 decode failed (in=%u)\n", (unsigned)b64_len);
      }
    }
    return;
  }
  if (typeIs(p, "response.audio_transcript.delta")) {
    size_t len = 0;
    const char* d = findStr(p, "delta", &len);
    if (d && len && s_cb.onTranscript) s_cb.onTranscript(d, len);
    return;
  }
  if (typeIs(p, "response.done")) {
    const char* usage = strstr(p, "\"usage\"");
    Serial.printf("[rt] done  %.200s\n", usage ? usage : "(no usage)");
    if (s_cb.onDone) s_cb.onDone();
    return;
  }
  if (typeIs(p, "session.created")) {
    sendSessionUpdate();
    return;
  }
  if (typeIs(p, "session.updated")) {
    s_session_ready = true;
    if (s_cb.onSessionReady) s_cb.onSessionReady();
    return;
  }
  if (typeIs(p, "error") || strstr(p, "\"error\":{")) {
    Serial.printf("[rt] server error: %.300s\n", p);
    if (s_cb.onError) s_cb.onError(p);
    return;
  }
  // 其余事件（committed/item.created/transcription…）只留串口一行。
  size_t tlen = 0;
  const char* t = findStr(p, "type", &tlen);
  if (t) Serial.printf("[rt] ev %.*s\n", (int)tlen, t);
}

void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      s_connected = true;
      Serial.println("[rt] ws connected");
      break;
    case WStype_DISCONNECTED:
      if (s_connected) Serial.println("[rt] ws disconnected");
      s_connected = false;
      s_session_ready = false;
      if (s_cb.onError) s_cb.onError("disconnected");
      break;
    case WStype_TEXT:
      handleText(payload, length);
      break;
    case WStype_ERROR:
      Serial.printf("[rt] ws error len=%u\n", (unsigned)length);
      break;
    default:
      break;
  }
}

}  // namespace

bool rtBegin(const RtCallbacks& cb, const char* api_key) {
  if (!s_tx) s_tx = (char*)heap_caps_malloc(TX_CAP, MALLOC_CAP_SPIRAM);
  if (!s_rx_pcm) s_rx_pcm = (uint8_t*)heap_caps_malloc(RX_PCM_CAP, MALLOC_CAP_SPIRAM);
  if (!s_tx || !s_rx_pcm) return false;

  static char auth[160];
  snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
  s_cb = cb;
  s_connected = false;
  s_session_ready = false;

  // 初始化序列抄 examples/esp32/WebSocketClientSSL（beginSSL→onEvent→loop）；
  // 换 beginSslWithCA 走根证书校验而非裸 beginSSL。
  s_ws.beginSslWithCA(HOST, 443, PATH, DASHSCOPE_ROOT_CA);
  s_ws.setExtraHeaders(auth);
  s_ws.onEvent(wsEvent);
  // 懒连接语义：断了不自动风暴重连（spec：下次触摸再试）。库最小配置是
  // 拉长重连间隔；真正的断开由上层 rtDisconnect() 决定。
  s_ws.setReconnectInterval(60000);
  s_ws.enableHeartbeat(15000, 4000, 2);
  return true;
}

void rtLoop() { s_ws.loop(); }

void rtDisconnect() {
  s_ws.disconnect();
  s_connected = false;
  s_session_ready = false;
}

bool rtSessionReady() { return s_connected && s_session_ready; }

void rtAppendAudio(const uint8_t* pcm16k, size_t len) {
  if (!rtSessionReady() || !len) return;
  static const char PRE[] = "{\"type\":\"input_audio_buffer.append\",\"audio\":\"";
  static const char POST[] = "\"}";
  size_t b64_len = 0;
  size_t off = sizeof(PRE) - 1;
  memcpy(s_tx, PRE, off);
  if (mbedtls_base64_encode((uint8_t*)s_tx + off, TX_CAP - off - sizeof(POST),
                            &b64_len, pcm16k, len) != 0) {
    Serial.println("[rt] b64 encode overflow");
    return;
  }
  off += b64_len;
  memcpy(s_tx + off, POST, sizeof(POST) - 1);
  off += sizeof(POST) - 1;
  s_ws.sendTXT((uint8_t*)s_tx, off);
}

void rtCommitAndRespond() {
  if (!rtSessionReady()) return;
  s_ws.sendTXT("{\"type\":\"input_audio_buffer.commit\"}");
  s_ws.sendTXT("{\"type\":\"response.create\"}");
}
