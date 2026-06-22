#include "audio.h"

#include <M5Unified.h>
#include <Preferences.h>

namespace {
constexpr int  kStep    = 32;        // ~8 steps across the 0-255 range
constexpr int  kDefault = 160;       // matches the buddy Tab5 default
constexpr char kNs[]    = "tab5af";  // NVS namespace
constexpr char kKey[]   = "vol";

int g_vol = kDefault;

int clamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

void persist() {
  Preferences p;
  if (p.begin(kNs, /*readOnly=*/false)) {
    p.putInt(kKey, g_vol);
    p.end();
  }
}

// A short blip at the just-set level so the user hears the new volume.
void blip() { M5.Speaker.tone(880, 60); }
}  // namespace

void audioInit() {
  // This build never uses the mic, so the speaker can own the shared I2S bus
  // outright — just begin() it once (the mic-arbitration dance in the buddy's
  // sound.cpp isn't needed here).
  M5.Speaker.begin();
  Preferences p;
  if (p.begin(kNs, /*readOnly=*/true)) {
    g_vol = p.getInt(kKey, kDefault);
    p.end();
  }
  g_vol = clamp(g_vol);
  M5.Speaker.setVolume((uint8_t)g_vol);
}

int audioVolume()    { return g_vol; }
int audioVolumePct() { return (g_vol * 100 + 127) / 255; }

void audioSetVolume(int v) {
  g_vol = clamp(v);
  M5.Speaker.setVolume((uint8_t)g_vol);
  persist();
}

void audioVolumeUp()   { audioSetVolume(g_vol + kStep); blip(); }
void audioVolumeDown() { audioSetVolume(g_vol - kStep); blip(); }
