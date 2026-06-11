// sound.cpp — pre-loaded WAV clip playback for the Tab5 dashboard.
// Clip loader copied from the proven stackchan/sound.cpp (dynamic
// /sounds/*.wav enumeration → PSRAM buffers, lookup by lowercase
// basename). What differs is bus arbitration: on Tab5 the ES7210 mic and
// ES8388 speaker share I2S_NUM_0, so the mic must be stopped while a clip
// plays and restarted afterwards (soundTick).
#include "sound.h"
#include <M5Unified.h>
#include <LittleFS.h>
#include <string.h>
#include <strings.h>   // strcasecmp

namespace {

struct Clip {
  char     name[28];   // basename without .wav, lowercase
  uint8_t* buf;
  size_t   len;
};

constexpr size_t MAX_CLIPS = 64;
Clip   g_clips[MAX_CLIPS];
size_t g_n_clips = 0;
bool   g_spkActive = false;     // speaker currently owns I2S_NUM_0
uint32_t g_spkIdleAt = 0;       // when playback was last seen active

bool loadOne(const char* path, const char* fname) {
  if (g_n_clips >= MAX_CLIPS) {
    Serial.printf("[snd] MAX_CLIPS reached, skipping %s\n", path);
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t sz = f.size();
  uint8_t* buf = (uint8_t*)ps_malloc(sz);
  if (!buf) {
    Serial.printf("[snd] ps_malloc failed for %s (%u bytes)\n",
                  path, (unsigned)sz);
    f.close();
    return false;
  }
  if (f.read(buf, sz) != sz) {
    Serial.printf("[snd] short read on %s\n", path);
    free(buf);
    f.close();
    return false;
  }
  f.close();

  Clip& c = g_clips[g_n_clips];
  size_t fl = strlen(fname);
  size_t copy = fl > sizeof(c.name) - 1 ? sizeof(c.name) - 1 : fl;
  memcpy(c.name, fname, copy);
  c.name[copy] = 0;
  if (copy >= 4 && strcasecmp(c.name + copy - 4, ".wav") == 0) {
    c.name[copy - 4] = 0;
  }
  for (char* p = c.name; *p; p++) {
    if (*p >= 'A' && *p <= 'Z') *p += 32;
  }
  c.buf = buf;
  c.len = sz;
  g_n_clips++;
  return true;
}

}  // namespace

void soundInit() {
  M5.Speaker.setVolume(160);
  File root = LittleFS.open("/sounds");
  if (!root || !root.isDirectory()) {
    Serial.println("[snd] /sounds not found on LittleFS");
    return;
  }
  File e = root.openNextFile();
  while (e) {
    if (!e.isDirectory()) {
      const char* full = e.name();
      const char* slash = strrchr(full, '/');
      const char* base = slash ? slash + 1 : full;
      size_t bl = strlen(base);
      if (bl > 4 && strcasecmp(base + bl - 4, ".wav") == 0) {
        char path[80];
        snprintf(path, sizeof(path), "/sounds/%s", base);
        loadOne(path, base);
      }
    }
    e = root.openNextFile();
  }
  Serial.printf("[snd] loaded %u clips\n", (unsigned)g_n_clips);
}

void soundPlay(const char* name) {
  if (!name || !*name) return;
  for (size_t i = 0; i < g_n_clips; i++) {
    if (strcasecmp(g_clips[i].name, name) == 0) {
      if (!g_spkActive) {            // claim the shared I2S bus
        M5.Mic.end();
        M5.Speaker.begin();
        g_spkActive = true;
      }
      g_spkIdleAt = millis();
      bool ok = M5.Speaker.playWav(g_clips[i].buf, g_clips[i].len,
                                   /*repeat=*/1, /*channel=*/-1,
                                   /*stop_current_sound=*/true);
      Serial.printf("[snd] play %s -> %s\n", g_clips[i].name, ok ? "ok" : "FAIL");
      return;
    }
  }
  Serial.printf("[snd] unknown clip: %s\n", name);
}

void soundTick() {
  if (!g_spkActive) return;
  if (M5.Speaker.isPlaying()) { g_spkIdleAt = millis(); return; }
  // small grace so back-to-back clips don't thrash the codecs
  if (millis() - g_spkIdleAt < 300) return;
  M5.Speaker.end();
  M5.Mic.begin();
  g_spkActive = false;
}
