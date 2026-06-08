# M5Stack StickS3 + Xiaozhi firmware — bring-up retrospective

Hard-won notes from porting the open-source **xiaozhi-esp32** AI-voice
firmware to the **M5Stack StickS3** (and adding a BLE-HID air-mouse / PPT
presenter on top). Captured so the next firmware doesn't re-walk these walls.

> The firmware itself lives in a separate fork of `78/xiaozhi-esp32`
> (board `main/boards/m5stack-sticks3/`), not in this repo. This doc is the
> lessons-learned companion.

## Hardware

| Block | Part | Pins / notes |
|---|---|---|
| MCU | ESP32-S3-PICO-1-**N8R8** | 8 MB flash / **8 MB OCTAL** PSRAM |
| Audio | ES8311 codec + MEMS mic + AW8737 amp | I2S MCLK18 BCLK14 WS15 DOUT17 DIN16; codec I²C 0x18 |
| IMU | BMI270 (6-axis) | I²C 0x68, shared bus SDA47/SCL48 |
| LCD | ST7789P3 135×240 | SPI MOSI39 SCLK40 CS41 DC45 **RST21 BL38** |
| PMIC | **M5PM1** | I²C 0x6E — gates LCD + audio power (L3B rail) |
| IR | TX46 / RX42 | |
| Buttons | KEY1=G11, KEY2=G12 | |

WiFi-config SoftAP name = `Xiaozhi-<last 2 MAC bytes>` (captive portal at
`192.168.4.1` — you must join that AP first, not your home WiFi).

## The five hardware traps (none fully documented; ground truth = M5GFX source)

1. **PSRAM is OCTAL.** N8R8 → `CONFIG_SPIRAM_MODE_OCT=y`. Using QUAD (copied
   from CoreS3) → boot panic `quad_psram: wrong PSRAM line mode` /
   `Failed to init external RAM` → black-screen boot loop.

2. **M5PM1 PMIC powers the LCD *and* the codec (L3B rail).** The ESP32 runs
   fine off USB, but the LCD and ES8311 stay unpowered (black screen + codec
   I²C NACK) until you drive PM1 **GPIO2** high. Verbatim sequence from
   `M5GFX/src/M5GFX.cpp` (`board_M5StickS3`):
   ```
   reg 0x16 bit2 = 0   # GPIO2 = GPIO function
   reg 0x10 bit2 = 1   # GPIO2 = output
   reg 0x13 bit2 = 0   # GPIO2 = push-pull
   reg 0x11 bit2 = 1   # GPIO2 = high -> L3B / LCD+codec power ON
   reg 0x09     = 0x00 # disable I2C idle sleep
   delay 100ms
   ```

3. **I²C device clock: use 100 kHz, not 400 kHz.** `i2c_bus_device_create(bus,
   0x6E, 400000)` returned `ESP_ERR_INVALID_STATE` on *every* read/write —
   silently, so the writes looked like they ran. `100000` works. This single
   wrong constant kept L3B (and thus the whole display) off.

4. **The M5 web pinmap had RST/BL swapped.** Web docs said RST=38/BL=21;
   M5GFX (the actual driver) says **RST=21, BL=38**. Backlight PWM was driving
   the wrong pin → dark panel. *Trust M5GFX source over the web pinmap.*

5. **I²C API mixing.** `bmi270_sensor` only accepts the legacy
   `i2c_bus_handle_t`; the codec + PMIC want the new `i2c_master` API. Bridge:
   `i2c_bus_create(...)` then `i2c_bus_get_internal_bus_handle()` (as in
   `boards/esp-vocat`). For custom PMIC writes use the **legacy**
   `i2c_bus_device_create` + `i2c_bus_write_bit/byte` — the new-API `I2cDevice`
   class can't attach to that bus handle (`i2c.master: port not initialized`).

### BLE HID (air-mouse / presenter)
- Add `esp_hid` to `main` `PRIV_REQUIRES` **unconditionally** — a
  `if(CONFIG_BOARD_TYPE_...)`-gated `MAIN_PRIV_REQUIRES_EXTRA` silently no-ops
  during dependency expansion.
- Set `CONFIG_BT_NIMBLE_HID_SERVICE=y`, else link fails with undefined
  `esp_ble_hidd_dev_init` (it's `#if`-gated inside `nimble_hidd.c`).

## Toolchain / China-network workarounds

- System Python 3.14 is too new for ESP-IDF 5.5 (≤3.12) → use a 3.11 via a
  `python3` PATH shim.
- `dl.espressif.com` is blocked here:
  - `export IDF_PYTHON_CHECK_CONSTRAINTS=no` (constraints download fails).
  - Install IDF pip deps from a mirror: `--index-url https://pypi.tuna.tsinghua.edu.cn/simple`.
  - `cmake`/`ninja` aren't installed by IDF (blocked) → `brew install cmake ninja`.
  - `components.espressif.com` *is* reachable (managed components are fine).
- `export.sh` fails to set PATH in a non-interactive shell → instead:
  `eval "$(python3.11 $IDF_PATH/tools/idf_tools.py export)"`.

## Build / flash gotchas

- **`release.py` skips the build if `releases/<ver>_<board>.zip` exists.**
  Always `rm -f releases/*.zip` before a rebuild, and verify `build/xiaozhi.bin`
  mtime + `build/config/sdkconfig.h` after. (Cost us a flash of stale firmware.)
- **ESP32-S3 native USB flashing:** esptool auto-reset (`--before default_reset`)
  works while the chip is crashed / boot-looping, but drops off the bus when a
  stable app holds the native USB-CDC. Reliable fallback: hold the **side reset
  button ~2 s until the green LED blinks** → download mode → flash with
  `--before no_reset`.
- **Serial capture:** pyserial open with `dtr=False` resets *into* download
  mode; `dtr=True` boots the app. `esp_idf_monitor` needs a TTY (no headless) —
  capture with raw pyserial.

## Methodology lesson

The black screen took many iterations because of guess-and-check on the PMIC
sequence. What actually broke it open: **(a) going back to the M5GFX source as
ground truth** instead of the web pinmap, and **(b) adding on-device
diagnostics** (read PMIC registers back + scan the I²C bus) to get *data* — the
scan proved 0x6E ACKed while my handle's ops failed, which pinpointed the
400 kHz clock bug. When a hardware symptom resists ≥2 fixes: stop guessing,
re-align to upstream source, and instrument for real data.
