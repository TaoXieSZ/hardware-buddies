#pragma once
#include <stdint.h>
#include <stddef.h>

// Begin streaming a new utterance. Emits evt:"audio_begin" with sr=12000, frame_raw_bytes=240.
// Caller must have already started capture via audioCaptureStart(). Idempotent: calling while
// already streaming is a no-op.
void audioBleStreamBegin();

// End streaming. Drains remaining ring (one final flush of >=1 frame if any), emits final
// evt:"audio_end" with overall CRC32. Logs bleWrite duration percentiles. Idempotent.
void audioBleStreamEnd(const char* reason);  // reason string included in audio_end JSON

// Pump emit; called every loop() iteration. Decides whether to emit a frame this tick.
// Returns true if a frame was emitted, false otherwise.
bool audioBleStreamPump();

// Emit a one-shot PTT control event. state must be "down" or "up". Adds millis() ts.
void audioBleEmitPtt(const char* state);

// True if a stream is currently active (between Begin and End).
bool audioBleStreamActive();

// Inbound ack handler — called by data.h when {"cmd":"ptt_ack",...} arrives.
// For Phase B: logs the ack. Phase D will wire this to the PTT FSM.
void audioBleHandlePttAck(const char* state, const char* reason);
