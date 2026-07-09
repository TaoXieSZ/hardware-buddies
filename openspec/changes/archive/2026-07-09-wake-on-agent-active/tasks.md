## 1. Implement level-based agent-active wake

- [x] 1.1 In the auto-dim block (main.cpp ~line 519), add `agentActive = bs.running > 0 || bs.waiting > 0` and fold it into the `activity` bool that gates `screenOn()`
- [x] 1.2 Confirm level semantics: while any session is running or waiting, `screenOn()` is called every frame (idempotent) so the screen stays lit; when all go idle, the normal 60s `SCREEN_OFF_MS` timer re-dims
- [x] 1.3 Do NOT use `cclink::changed()` (per-heartbeat true → never dims); use the `bs.running`/`bs.waiting` counts directly

## 2. Build + flash + verify

- [x] 2.1 `pio run -e cardputer-adv` builds clean
- [x] 2.2 Flash via ROM download mode (4 images, esptool `--before no_reset`); confirm normal boot, no PSRAM abort
- [x] 2.3 Live verify: with an agent session running, the screen stays lit showing the busy avatar (the bug that motivated this change — screen stayed dark while r=1 — is fixed)

## 3. Commit

- [x] 3.1 One commit: `feat(cardputer-adv): keep backlight on while an agent is running or waiting`, referencing the level-based `agentActive` condition
