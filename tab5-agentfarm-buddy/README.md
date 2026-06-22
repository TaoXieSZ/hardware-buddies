# tab5-agentfarm-buddy

A desk pet for **[Agent Farm](https://github.com/) (`trigger-cursor`)** on the
**M5Stack Tab5** (ESP32-P4, 1280×720). It shows every Trigger firing as a live
dashboard feed with a reacting **clawd GIF** avatar — success celebrates, error
goes dizzy, long-idle naps.

Unlike the WiFi/BLE buddies in this monorepo, the Tab5 gets its data over
**USB-serial** from a small Mac-side bridge. The bridge polls Agent Farm on
**localhost**, so the device works on any network with no on-device WiFi config
(the P4 has no radio anyway).

```
agent-farm/trigger-cursor (admin HTTP @ 127.0.0.1:60360, /api/logs = TriggerLog[])
        │ poll every ~3s (localhost — always reachable)
   bridge.py  ──USB-CDC JSON lines──▶  Tab5 SerialFeedClient ──▶ dashboard UI + clawd GIF
```

## Layout

```
platformio.ini              single env: tab5-agentfarm (USB-serial, no WiFi deps)
partitions_tab5_16MB.csv    drops OTA slot → ~9.6MB LittleFS for fonts+GIFs
src/
  main.cpp                  M5 + SerialFeedClient + FeedUITab5 loop
  feed_ui_tab5.{h,cpp}      1280x720 dashboard UI + pet-mood state machine
  avatar.{h,cpp}            clawd GIF renderer (AnimatedGIF + LittleFS)
  agentfarm_feed/
    trigger_log.h           TriggerLog mirror + result/type parsing
    serial_feed_client.{h,cpp}   USB-CDC line reader (JSON + heartbeats)
tools/agentfarm-usb-bridge/bridge.py   Mac-side localhost→USB bridge
data/
  fonts/*.vlw               anti-aliased VLW smooth fonts
  characters/clawd/*.gif    clawd avatar pack
```

## Build & flash

```bash
# compile
pio run -e tab5-agentfarm

# push the LittleFS assets (fonts + GIFs) ONCE, then the firmware.
# Run uploadfs and upload as SEPARATE commands — the device hard-resets after
# uploadfs and a chained firmware connect can't reattach to the re-enumerated port.
pio run -e tab5-agentfarm -t uploadfs --upload-port /dev/cu.usbmodem<NN>
pio run -e tab5-agentfarm -t upload   --upload-port /dev/cu.usbmodem<NN>
```

The P4 USB-Serial-JTAG flashes fine at the default 1.5 Mbaud. If two Espressif
boards are plugged in (both show as `303A` "USB JTAG/serial"), confirm the Tab5
with `esptool.py --port <p> chip_id` → reports `ESP32-P4`.

## Run the bridge

The bridge must stay running for the Tab5 to get data. It reads the Agent Farm
admin secret from the trigger-cursor `config.yaml` at runtime (not baked into
firmware):

```bash
python3 -u tools/agentfarm-usb-bridge/bridge.py \
  --port /dev/cu.usbmodem<NN> \
  --config ~/path/to/agent-farm/trigger-cursor/config.yaml
```

It auto-detects the port if `--port` is omitted, re-primes the snapshot on
connect, and reconnects on USB drops. Requires `pyserial`.

## Pet behavior

- Mood from `TriggerLog.result`: `error` → worried (dizzy avatar), `success` →
  happy (celebrate), `queued`/`skipped_*` → neutral. Idle 2 min → nap: the
  backlight turns off; a tap or new trigger wakes it.
- Header status pill: `LIVE` (green) when serial data is fresh, `OFFLINE` (red)
  after 15 s with no heartbeat.
- Feed rows show trigger name, `type · agent · time`, and a result pill.
- Mood chirps: low tone on error, high tone on success, soft blip otherwise.

## Volume (touch)

The sidebar has a `−` / `+` touch control with a level bar (`VOL nn%`). Tapping
adjusts the speaker volume in steps and plays a confirmation blip at the new
level; the setting is persisted to NVS so it survives reboots. Any tap also
wakes the pet from a nap. See `src/audio.{h,cpp}`.

The Tab5 speaker needs an explicit `M5.Speaker.begin()` + `setVolume()` at boot
(`audioInit()`); `M5.begin()` alone leaves it silent.

## Backlight note (Tab5)

`M5.Display.setBrightness()` does drive the Tab5 backlight (M5GFX wires a
`Light_PWM` on GPIO22, LEDC ch7 @ 44.1 kHz), but on-device probing showed the
LED driver is effectively **on/off**: duty 4/10/24/64 all look full-bright and
only 0 goes dark. So intermediate "dimming" isn't achievable on this panel —
napping turns the backlight fully off instead, which also saves power.
