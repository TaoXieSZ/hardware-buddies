// Tab5 dashboard UI (P1 layout C: sidebar + main + clawd avatar).
// Self-contained DEMO model for the design pass — uiTick() animates fake
// sessions through every visual state so the look can be judged on-device.
// M1 swaps the demo model for live TCP feeds; the render code stays.
#pragma once
#include <stdint.h>

// Live device status fed from main.cpp each tick.
struct UiStatus {
  bool wifiUp;
  char ip[20];
  int  rssi;
  int  battPct;
  int  micLevel;   // 0..100, drives the avatar mouth
};

void uiInit();
void uiTick(const UiStatus& st);

// ── live-feed API (M1) ───────────────────────────────────────────────
// feed.cpp pushes parsed cc-bridge heartbeats into session 0. The first
// uiFeedAlive() latches live mode and retires the demo script.
void uiFeedAlive();
void uiFeedState(uint8_t state, const char* tool, uint32_t tokens);
void uiFeedLine(const char* line);
void uiFeedPrompt(bool pending, const char* id, const char* text);
// Pops one queued touch verdict (Allow/Deny). Returns false when empty.
bool uiTakeDecision(char* idOut, unsigned idCap, bool* allow);

// main.cpp hook for the serial feed pump (implemented in feed.cpp).
void feedPoll();

// USB keyboard (kbd.cpp): HID usage code + modifier byte, called from loop().
void uiKeyEvent(uint8_t hidKey, uint8_t mods);
void kbdInit();
void kbdPoll();
