// GIF face player — fullscreen cat expressions from LittleFS.
// Replaces the old M5GFX-primitive robot face. One GIF per AgentState,
// generated from Lottie scenes (320x240 @10fps, see repo docs).
#pragma once
#include "motion.h"   // AgentState

void gifFaceInit();                      // mount LittleFS, init decoder
void gifFaceSetState(AgentState state);  // switch to that state's GIF
void gifFaceTick();                      // call every loop(); paces frames itself

// When the bottom text band (y >= bandY) is in use, GIF scanlines under it
// are skipped so text and animation don't fight. Pass 0 to disable clipping.
void gifFaceSetTextBand(int bandY);

// Vertical shift applied to all GIF rows (negative = move face up, e.g. when
// a taller bottom panel crops the face and you want the eyes centred in the
// remaining space). Rows that land off-screen are dropped.
void gifFaceSetYOffset(int dy);
