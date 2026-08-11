// GIF face player — native-size Clawd animations from LittleFS.
// Sleep/reminder have fixed assets; other states choose one random asset on
// state entry and keep it for every loop until the next state transition.
#pragma once
#include "motion.h"   // AgentState

void gifFaceInit();                      // mount LittleFS, init decoder
void gifFaceSetState(AgentState state);  // switch to that state's GIF
void gifFaceForceState(AgentState state); // restore fixed face after an overlay
void gifFaceShowRandom();                // force a fresh random GIF without changing mode
void gifFaceRefresh();                   // redraw after a full-screen menu
void gifFaceTick();                      // call every loop(); paces frames itself

// When the bottom text band (y >= bandY) is in use, GIF scanlines under it
// are skipped so text and animation don't fight. Pass 0 to disable clipping.
void gifFaceSetTextBand(int bandY);

// Optional centred-layout vertical nudge. Values outside +/-8px are ignored.
void gifFaceSetYOffset(int dy);
