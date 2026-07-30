# Task Spec: Cardputer "Connecting" state (carrying GIF + top-bar text while BLE is down)

- **ID**: 001-cardputer-connecting-state
- **Workdir**: /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy
- **Executor**: cursor
- **Created**: 2026-07-02 11:30
- **Iteration**: 0

## Goal

While the device is NOT connected to the Mac-side `cc-bridge` daemon over BLE
(`cclink::connected() == false`), the firmware must show a dedicated
"connecting" visual instead of being indistinguishable from real Idle:
`clawd-carrying.gif` as the character animation, plus a persistent
`Connecting...` text in the top-left bar. Once connected, everything returns
to the existing rendering (badge, session tag/rotation, state-derived GIFs).

## Background / Context

- Project: ESP32-S3 Arduino firmware (PlatformIO env `cardputer-adv`) for an
  M5Stack Cardputer-ADV desk companion. C++ files in `src/`. Code comments are
  written in **Chinese** — match that style for any new comments.
- The full requirements/design live in
  `/Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/openspec/changes/cardputer-connecting-state/`
  (proposal.md / design.md / specs/connecting-state/spec.md). The plan below
  already reflects those decisions — do not re-litigate them.
- The GIF asset is ALREADY in place: `data/characters/clawd/clawd-carrying.gif`
  (120x96, 45 frames). `manifest.json` is already updated. Do not touch either.
- Rendering architecture (`src/clawd_player.cpp`, anonymous namespace):
  - `fileForState(AgentState)` maps state → GIF filename (hardcoded switch).
  - `targetFile()` picks, in priority order: `reactionFile_` → `sleeping_`
    (`sleep.gif`) → `fileForState(baseState_)`.
  - `setState(AgentState)` stores `baseState_` and calls `applyTarget()`
    (which reopens the GIF only when the filename changed).
  - `drawSessionTag()` draws the top-left bar (session label + [i/N]) each
    tick in NORMAL mode; it clears rect `(0, 0, canvasW - 66, 13)` first and
    early-returns when `rotTotal_ <= 0 || !rotTag_[0]`. The right side
    (~66px) is reserved for battery % + T/R badge — never draw into it.
- Main loop (`src/main.cpp`): `bool online = cclink::connected();` is computed
  every frame near the top of `loop()`. Later, a block commented
  `── 多会话轮播 / FIFO 钉：主形象状态来源` derives the target
  `AgentState` (from `bs.sessions[]` rotation, falling back to
  `deriveAgentState(bs)` when `bs.nSessions == 0`) and calls
  `clawd::setState(target)` only when `(int)target != lastAg`.

### ⚠️ Critical interaction you MUST handle: the sleep override

Near the end of `loop()` in `src/main.cpp` there is:

```cpp
clawd::setSleeping(!online || (idle && g_motion.stillMs() > STILL_FOR_SLEEP));
```

`!online` currently FORCES sleeping, and `targetFile()` gives `sleeping_`
priority over `fileForState()`. If you leave this line as-is, the new
Connecting GIF will never be visible — sleep.gif wins. The `!online ||` part
must be removed (see Plan step 4). This is an intended behavior change:
"offline → sleep" is replaced by "offline → Connecting". Idle-stillness sleep
(30s) remains for the online case.

## Files in Scope

| File | Change |
|------|--------|
| /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/src/agent_state.h | modify: add `Connecting` enum value |
| /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/src/clawd_player.h | modify: declare `void setOnline(bool on);` |
| /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/src/clawd_player.cpp | modify: online_ flag + setter, fileForState branch, drawSessionTag Connecting branch |
| /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/src/main.cpp | modify: gate state derivation on `!online`, fix setSleeping condition |
| /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/README.md | modify: one-line doc update ("离线或久空闲 → sleep" is no longer accurate) |

## Plan

1. `src/agent_state.h`: add `Connecting` to `enum class AgentState`
   immediately before the `Count` sentinel, with a short Chinese comment
   (e.g. `// BLE 未连上 cc-bridge（开机/断链），Connecting 视觉`).

2. `src/clawd_player.h` + `src/clawd_player.cpp`:
   - Header: declare `void setOnline(bool on);` in the NORMAL-mode section
     next to `setSleeping(bool)`.
   - cpp: add `bool online_ = false;` in the anonymous namespace near
     `sleeping_`. Implement `void setOnline(bool on) { online_ = on; }`
     (a plain store; the GIF itself is driven by setState from main.cpp,
     and the top bar is redrawn every tick anyway).
   - `fileForState()`: add
     `case AgentState::Connecting: return "clawd-carrying.gif";`
   - `drawSessionTag()`: add an early branch at the top:

     ```cpp
     if (!online_) {                          // 未连上 cc-bridge：常驻连接提示
         canvas.setTextSize(1);
         canvas.setTextDatum(top_left);
         canvas.fillRect(0, 0, canvasW - 66, 13, BG);
         canvas.setTextColor(0x8410, BG);     // 与会话标识同款灰
         canvas.drawString("Connecting...", 2, 1);
         return;
     }
     ```

     Note: the existing body switches to `fonts::efontCN_12` for Chinese
     labels and back to `fonts::Font0` at the end — the new branch draws
     ASCII only and must NOT leave a non-default font set; drawing before
     any font switch (as sketched) is correct.

3. `src/main.cpp`, in the rotation block (comment
   `── 多会话轮播 / FIFO 钉：主形象状态来源`): call `clawd::setOnline(online);`
   at the top of the block, then gate the target derivation:

   ```cpp
   clawd::setOnline(online);
   uint8_t n = bs.nSessions;
   AgentState target;
   if (!online) {
       target = AgentState::Connecting;       // 未连上：不跑派生/轮播（bs 必为全零）
       clawd::setSessionTag(nullptr, 0, 0, false);
   } else if (n == 0) {
       ... existing fallback branch unchanged ...
   } else {
       ... existing rotation/pin branch unchanged ...
   }
   if ((int)target != lastAg) { clawd::setState(target); lastAg = (int)target; }
   ```

   Keep the existing `rotNextMs/rotIdx/lastAg` statics and the final
   setState-on-change line exactly as they are; only insert the `!online`
   arm ahead of the existing `if (n == 0)`.

4. `src/main.cpp`, the sleep line near the end of `loop()`: change

   ```cpp
   clawd::setSleeping(!online || (idle && g_motion.stillMs() > STILL_FOR_SLEEP));
   ```

   to keep only the online-idle-stillness part, e.g.

   ```cpp
   clawd::setSleeping(online && idle && g_motion.stillMs() > STILL_FOR_SLEEP);
   ```

   Add/adjust the Chinese comment on that line to say offline now shows the
   Connecting visual instead of sleep (`openspec cardputer-connecting-state`).

5. `README.md` (repo-relative `cardputer-adv-buddy/README.md`): the line
   containing `离线或久空闲 → **sleep**` — update so it says offline shows
   Connecting (carrying + `Connecting...`) and only long idle stillness
   sleeps. One line, minimal edit.

6. Build: `pio run -e cardputer-adv` from the workdir. Must end SUCCESS.
   Do NOT run any `-t upload` / `-t uploadfs` target (no device attached to
   you; flashing is the reviewer's job).

## Constraints

- Do NOT touch files outside "Files in Scope" unless a step explicitly requires it.
  In particular: do NOT edit `src/cclink.*`, `src/ble_link.*`,
  `data/characters/clawd/*` (assets/manifest already done), anything under
  `openspec/`, or anything outside the `cardputer-adv-buddy/` directory.
- Do NOT refactor, reformat, or "improve" unrelated code (there are fresh
  features in these same files — busy/idle GIF variant timers, backspace key
  relay; leave them alone).
- New comments in Chinese, matching the surrounding style.
- Do not add any new timers/state beyond `online_`; this feature is
  deliberately minimal.

## Acceptance Criteria

- [ ] `AgentState::Connecting` exists (before `Count`).
- [ ] `fileForState(AgentState::Connecting)` returns `"clawd-carrying.gif"`.
- [ ] `clawd::setOnline(bool)` exists and `drawSessionTag()` renders a
      persistent `Connecting...` (top-left, inside `(0,0,canvasW-66,13)`,
      color 0x8410 on BG) when `!online_`, early-returning before the
      session-tag logic; right-side 66px reserve untouched.
- [ ] `main.cpp` rotation block: when `!online`, target is
      `AgentState::Connecting`, session tag cleared, and the existing
      `deriveAgentState`/rotation code does not run; when online, behavior
      is byte-for-byte the previous logic.
- [ ] The `setSleeping` call no longer forces sleep when `!online`.
- [ ] `README.md` no longer claims offline → sleep.
- [ ] `pio run -e cardputer-adv` succeeds.
- [ ] `git diff` touches only the five files in scope.

## Verification

```bash
cd /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy
pio run -e cardputer-adv   # must end "[SUCCESS]"
git status --porcelain     # only the 5 in-scope files modified
```
