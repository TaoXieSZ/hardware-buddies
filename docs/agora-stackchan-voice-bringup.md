# StackChan voice (Path A2) — bring-up

Make the Agora ConvoAI agent's voice play from StackChan's speaker. The Mac
browser stays the RTC client (mic + echo cancellation there); only the agent's
TTS audio is relayed to the device.

```
buddy-voice (browser)  --PCM/WebSocket-->  audio-relay (Mac)  --PCM/UDP-->  StackChan
  taps agent audio track                   tools/audio-relay                 audio_play.cpp -> M5.Speaker
```

Prereq: Path A baseline already works (browser conversation with the agent).
See `docs/agora-stackchan-voice-feasibility.md` for the why.

## 1. Device — set creds + flash

Edit `wifi_secrets.ini` (same network as the Mac):

```ini
[wifi_secrets]
ssid = YOUR_WIFI
pass = YOUR_PASS
host = 192.168.x.y    ; Mac IP (for the existing camera path; unused by audio)
port = 8770
audio_port = 5005     ; UDP port StackChan listens on for agent audio
```

Keep creds out of commits, then flash:

```bash
git update-index --skip-worktree wifi_secrets.ini
pio run -e cores3-stackchan-claude -t upload   # or cores3-stackchan
```

On boot the serial log shows `audioPlay: listening for PCM on udp/5005` and
StackChan's IP (`wifiStream: WiFi up, ip=...`). **Note that IP** — the relay
needs it.

## 2. Mac — run the relay

```bash
cd tools/audio-relay
pip install -r requirements.txt
STACKCHAN_AUDIO_HOST=<stackchan-ip> python relay.py
```

Logs `relay: ws :8771 -> udp <ip>:5005`. Leave it running.

## 3. Browser — enable the tap

In `buddy-voice/.env.local` add:

```bash
NEXT_PUBLIC_STACKCHAN_RELAY=1                  # turn the tap on
# NEXT_PUBLIC_STACKCHAN_RELAY_WS=ws://127.0.0.1:8771   # default, override if needed
# NEXT_PUBLIC_STACKCHAN_MUTE_LOCAL=1           # only StackChan speaks (mute Mac)
```

Restart `pnpm dev`, open http://localhost:3000, click **Try it now**, speak.

## 4. Verify (user / hardware step)

These cannot be checked in CI — confirm on the physical device:

- [ ] Agent's reply audio comes out of **StackChan's speaker**.
- [ ] With `NEXT_PUBLIC_STACKCHAN_MUTE_LOCAL=1`, the Mac is silent and only
      StackChan speaks.
- [ ] Latency is acceptable (a few hundred ms); audio is intelligible.

## Tuning (firmware, if audio is choppy/laggy)

In `src/stackchan/audio_play.cpp`:

- `CHUNK_SAMPLES` (default 512 = 32 ms) — larger = fewer glitches, more latency.
- `AudioRingBuffer<32768>` (default ~1 s) — jitter headroom; overflow drops
  oldest (a small click), never blocks the receiver.
- `HANGOVER_MS` (250) — how long `audioPlayIsActive()` stays true after the
  last chunk (for a future "talking" mouth animation).

## Known limits (v1)

- Mac must stay on (it is the RTC client). For a Mac-free device, see Path B in
  the feasibility doc.
- 16 kHz mono — voice quality, not hi-fi.
- Mouth/talking animation is **not** wired yet; `audioPlayIsActive()` is
  exposed for it but driving the character state is a follow-up.
- During camera permission-gesture mode the internal I2C bus is held by the
  camera, so speaker control can glitch then; normal idle/desk use is fine.
