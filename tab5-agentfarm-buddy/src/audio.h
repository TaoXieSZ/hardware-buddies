// audio.h — speaker + volume control for the Tab5 Agent Farm desk pet.
//
// On the Tab5 (ESP32-P4) M5.begin() alone leaves M5.Speaker silent — tone()
// and playWav produce nothing until the speaker is explicitly begun and given
// a volume (ground truth: claude-code-buddy/src/tab5/sound.cpp). audioInit()
// claims the speaker once at boot; the volume is persisted to NVS so it
// survives reboots.
#pragma once

void audioInit();           // claim the speaker + restore the saved volume
int  audioVolume();         // current volume, 0-255
int  audioVolumePct();      // current volume as 0-100 (for display)
void audioSetVolume(int v); // clamp to 0-255, apply, persist (no confirmation tone)
void audioVolumeUp();       // one step louder  + confirmation blip
void audioVolumeDown();     // one step quieter + confirmation blip
