# Tab5 C6 co-processor updater (one-shot)

Factory M5Stack Tab5 units ship the on-board ESP32-C6 with **esp-hosted
slave firmware 1.4.1**. The Arduino host stack in arduino-esp32 3.3.x is
**esp-hosted 2.8.x**, which cannot talk to a 1.x slave: the version RPC
answers but WiFi scans return 0/-2, connects fail `ESP_ERR_WIFI_CONN`, and
the in-core hosted OTA API (`hostedBeginUpdate` …) times out — so the device
can never update itself. Chicken-and-egg.

This little ESP-IDF firmware breaks the loop. It runs on the P4 with
**esp_hosted 1.4.0** (the factory protocol generation, same pairing the
M5Tab5-UserDemo uses), so the old slave actually answers its OTA RPC, and it
streams an embedded **esp32c6-v2.8.5** image over SDIO. ~3 seconds. Run it
once per new unit.

## Usage

```bash
# 1. Fetch the C6 image and generate the embedded C array (both gitignored):
curl -fsSL -o src/c6fw.bin https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.8.5.bin
python3 - <<'EOF'
data = open("src/c6fw.bin","rb").read()
with open("src/c6fw_data.c","w") as f:
    f.write("#include <stdint.h>\n")
    f.write(f"const uint32_t c6fw_len = {len(data)};\n")
    f.write("const uint8_t c6fw_data[] = {\n")
    for i in range(0, len(data), 16):
        f.write("  " + ",".join(f"0x{b:02x}" for b in data[i:i+16]) + ",\n")
    f.write("};\n")
EOF

# 2. Build + flash (from this directory, or pio run -d tools/tab5-c6-updater):
pio run -e tab5-c6-updater -t upload --upload-port /dev/cu.usbmodemXXX

# 3. Watch serial (115200). Expected: expanders configured → transport up →
#    OTA progress 0→100% → "OTA done — slave reboots itself".

# 4. Reflash the real firmware (repo root):
pio run -e m5stack-tab5 -t upload --upload-port /dev/cu.usbmodemXXX
```

The screen stays black while this runs — the updater is headless.

## Hard-won facts encoded here (don't relearn them)

- **Power rails**: a bare IDF firmware must configure both PI4IOE5V6408 IO
  expanders itself (`tab5_expander_init` in main.c, register values copied
  verbatim from M5GFX) — otherwise the C6 has no power (`WLAN_PWR_EN`) and
  no antenna, and SDIO probing dies with `sdmmc send_op_cond 0x107`.
- **Transport bring-up is lazy**: `esp_hosted_init()` only registers
  channels; `transport_drv_reconfigure()` is what resets the slave and
  probes SDIO (normally reached via the remote `esp_wifi_init()`).
- **RPC payload cap is 1400 bytes** (`CHUNK_SIZE` in rpc_wrap.c) — bigger
  OTA chunks fail.
- **Kconfig landmines** (see sdkconfig.defaults): `ESP_HOSTED_ENABLED` is
  promptless (needs the esp_wifi_remote dependency to flip);
  `SDIO_PIN_D1` is derived (set `SDIO_PRIV_PIN_D1_4BIT_BUS`); the slave
  identity check needs `CONFIG_SLAVE_IDF_TARGET_ESP32C6=y` or the host
  expects a plain ESP32 and asserts.
- PIO's espidf integration ignores `CONFIG_PARTITION_TABLE_CUSTOM`
  (set `board_build.partitions`) and breaks both binary-embedding
  mechanisms (hence the generated C array).
