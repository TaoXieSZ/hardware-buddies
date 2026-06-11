#pragma once
// Speaker playback for the Tab5 dashboard — same clip set as StackChan
// (data/sounds/*.wav preloaded from LittleFS into PSRAM, played by name
// when the daemon heartbeat carries {"play": "<event>"}).
//
// Tab5 twist: mic (ES7210) and speaker (ES8388) share I2S_NUM_0, so they
// can't run together. soundPlay() ends the mic and claims the bus;
// soundTick() (call from loop) hands the bus back to the mic once the
// clip finishes.

void soundInit();
void soundPlay(const char* name);
void soundTick();
