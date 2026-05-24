// Streamed PCM playback for Path A2: the Mac relay sends the Agora agent's
// TTS audio as sequenced UDP datagrams; this module receives them, buffers
// them, and feeds M5.Speaker so the agent's voice comes out of StackChan.
//
// Wire format is defined in audio_packet.h and mirrored by the Python relay
// (tools/audio-relay/relay.py). Audio is signed-16-bit-LE mono PCM @ 16 kHz.
//
// Lifecycle (called from main.cpp):
//   setup()  -> audioPlayInit()   (brings WiFi up if creds present, opens UDP)
//   loop()   -> audioPlayPump()   (drain UDP -> ring buffer -> speaker)
//   anytime  -> audioPlayIsActive()  for mouth/talking visuals
//
// Best-effort and non-blocking: with no creds, no relay, or no packets it
// simply stays idle and never stalls the rest of the firmware.

#pragma once

// Open the UDP audio socket and bring WiFi up if wifi_secrets.ini has real
// creds. Safe to call once at boot; a no-op (returns false) when creds are
// placeholders. Idempotent.
bool audioPlayInit();

// Call every loop() tick. Reads any pending UDP datagrams into the jitter
// buffer and keeps M5.Speaker's queue fed. Cheap when there's no audio.
void audioPlayPump();

// True while audio is actively flowing (a packet/chunk within the last
// ~250 ms hangover). Intended to drive a "talking" character animation.
bool audioPlayIsActive();
