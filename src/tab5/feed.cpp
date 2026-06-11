// M1 live feed — NDJSON heartbeat over USB-CDC serial (REFERENCE.md schema).
// The cc-bridge daemon writes the same snapshot it sends the BLE sticks;
// we parse it into the dashboard session and answer permission prompts with
// {"cmd":"permission","id":...,"decision":"once"|"deny"} lines.
#include <Arduino.h>
#include <ArduinoJson.h>
#include "ui.h"
#include "sound.h"

static char     g_buf[4608];
static size_t   g_len = 0;
static char     g_lastEntry[96] = "";
static uint32_t g_doneUntil = 0;
static int      g_prevRunning = 0;

static void handleHeartbeat(JsonDocument& doc) {
  // One-shot sound trigger — daemon sets "play" for exactly one heartbeat
  // per hook event (same contract as the stackchan firmware).
  const char* play = doc["play"] | (const char*)nullptr;
  if (play && play[0]) soundPlay(play);

  int running = doc["running"] | 0;
  int total   = doc["total"] | 0;
  uint32_t tokens = doc["tokens"] | 0;
  const char* msg = doc["msg"] | "";

  JsonVariant prompt = doc["prompt"];
  bool pending = !prompt.isNull();
  if (pending) {
    const char* id   = prompt["id"]   | "";
    const char* tool = prompt["tool"] | "";
    const char* hint = prompt["hint"] | "";
    char text[96];
    snprintf(text, sizeof(text), "%s  %s", tool, hint);
    uiFeedPrompt(true, id, text);
  } else {
    uiFeedPrompt(false, "", "");
  }

  // celebrate briefly when the last running session finishes
  if (g_prevRunning > 0 && running == 0 && !pending) g_doneUntil = millis() + 3000;
  g_prevRunning = running;

  uint8_t st;
  if (pending)                    st = 2;   // ST_ATTN
  else if (running > 0)           st = 1;   // ST_BUSY
  else if (millis() < g_doneUntil) st = 3;  // ST_DONE
  else                            st = 0;   // ST_IDLE
  uiFeedState(st, (total == 0) ? "no sessions" : msg, tokens);

  // entries[] is newest-first; append only what's new since the last beat.
  // (Compare against the stashed previous newest — the data.h lineGen lesson:
  // comparing against a mutating field re-fires every tick.)
  JsonArray entries = doc["entries"];
  if (!entries.isNull() && entries.size() > 0) {
    int n = entries.size();
    int newCount = n;                        // default: all are new
    for (int i = 0; i < n; i++) {
      const char* e = entries[i] | "";
      if (strncmp(e, g_lastEntry, sizeof(g_lastEntry) - 1) == 0) { newCount = i; break; }
    }
    for (int i = newCount - 1; i >= 0; i--) {   // oldest→newest
      const char* e = entries[i] | "";
      if (e[0]) uiFeedLine(e);
    }
    snprintf(g_lastEntry, sizeof(g_lastEntry), "%s", (const char*)(entries[0] | ""));
  }
}

static void handleLine(const char* line) {
  if (line[0] != '{') return;
  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, line);
  if (jerr != DeserializationError::Ok) {
    Serial.printf("[feed] json parse failed (%s) len=%d\n", jerr.c_str(), (int)strlen(line));
    return;
  }
  // a heartbeat is recognized by its mandatory counters
  if (!doc["running"].isNull() || !doc["entries"].isNull()) {
    uiFeedAlive();
    handleHeartbeat(doc);
    Serial.printf("[feed] hb ok running=%d msg=%s\n",
                  (int)(doc["running"] | 0), (const char*)(doc["msg"] | ""));
  } else {
    Serial.println("[feed] json line ignored (no heartbeat keys)");
  }
}

void feedPoll() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      g_buf[g_len] = 0;
      if (g_len) handleLine(g_buf);
      g_len = 0;
    } else if (g_len < sizeof(g_buf) - 1) {
      g_buf[g_len++] = c;
    } else {
      g_len = 0;   // oversized line — drop and resync
    }
  }

  // pump touch verdicts back to the daemon
  char id[48]; bool allow;
  while (uiTakeDecision(id, sizeof(id), &allow)) {
    Serial.printf("{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}\n",
                  id, allow ? "once" : "deny");
  }
}
