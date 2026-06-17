/* Tab5 C6 co-processor one-shot updater.
 *
 * Flashes the embedded esp-hosted slave image (c6fw.bin = esp32c6-v2.8.5.bin)
 * into the ESP32-C6 over SDIO, using the 1.4.x OTA RPC that the factory
 * 1.4.1 slave actually implements (req_ota_begin_handler & friends in
 * slave_control.c). Mirrors the rpc_ota() flow in esp_hosted 1.4.7's
 * rpc_wrap.c, with the HTTP source swapped for the embedded image.
 * CHUNK of 1400 matches the library's own CHUNK_SIZE — RPC payloads are
 * limited; bigger chunks are why naive attempts fail.
 *
 * After "OTA done": the C6 reboots into 2.8.5 on its own; reflash the real
 * Arduino firmware (repo-root env m5stack-tab5) and WiFi should come up.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c.h"

static const char *TAG = "c6updater";

/* Tab5 power rails live behind two PI4IOE5V6408 IO expanders on I2C
 * (SDA=G31 SCL=G32). On the Arduino builds M5GFX configures them as a side
 * effect of display init — a bare IDF firmware must do it itself or the C6
 * has no power (WLAN_PWR_EN, exp2 P0) and no antenna (RF select, exp1 P0):
 * SDIO probe then times out with sdmmc send_op_cond 0x107. Register
 * sequences copied verbatim from M5GFX.cpp autodetect (board_M5Tab5). */
static void tab5_expander_init(void)
{
    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 31,
        .scl_io_num = 32,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    /* exp1 (0x43): RF path bit0=0 → internal antenna; LCD/touch resets. */
    const uint8_t io1_a[][2] = {
        {0x03, 0b01111111}, {0x05, 0b01000110}, {0x07, 0b00000000},
        {0x0D, 0b01111111}, {0x0B, 0b01111111},
    };
    /* exp2 (0x44): bit0=1 → WLAN_PWR_EN on (C6 power), USB5V, charger. */
    const uint8_t io2[][2] = {
        {0x03, 0b10111001}, {0x07, 0b00000110}, {0x0D, 0b10111001},
        {0x0B, 0b11111001}, {0x09, 0b01000000}, {0x11, 0b10111111},
        {0x05, 0b10001001},
    };
    const uint8_t io1_b[][2] = { {0x05, 0b01110110} };

    for (size_t i = 0; i < sizeof(io1_a)/2; i++)
        i2c_master_write_to_device(I2C_NUM_0, 0x43, io1_a[i], 2, pdMS_TO_TICKS(100));
    for (size_t i = 0; i < sizeof(io2)/2; i++)
        i2c_master_write_to_device(I2C_NUM_0, 0x44, io2[i], 2, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(10));
    for (size_t i = 0; i < sizeof(io1_b)/2; i++)
        i2c_master_write_to_device(I2C_NUM_0, 0x43, io1_b[i], 2, pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "PI4IOE5V6408 expanders configured (WLAN power ON, internal antenna)");
    vTaskDelay(pdMS_TO_TICKS(100));   /* let the C6 rail come up */
}

/* Public API (esp_hosted_api.c) + OTA RPC wrappers (rpc_wrap.c). The 1.4.x
 * component doesn't export headers for the rpc_* wrappers — declare them. */
esp_err_t esp_hosted_init(void);
/* esp_hosted_init() only registers channels — the slave reset + SDIO probe
 * live in transport_drv_reconfigure(), normally reached via the remote
 * esp_wifi_init(). Without it the transport stays down forever. */
esp_err_t transport_drv_reconfigure(void);
/* esp_hosted_setup() is #if 0'd out in 1.4.0 — poll the same flag the SDIO
 * tx path checks ("transport_up(0)" in its error message) instead. */
uint8_t is_transport_tx_ready(void);
int rpc_ota_begin(void);
int rpc_ota_write(uint8_t *ota_data, uint32_t ota_data_len);
int rpc_ota_end(void);

/* The image ships as a compiled C array (c6fw_data.c) — both EMBED_FILES and
 * board_build.embed_files are broken in PIO's espidf integration (generated
 * .S never reaches the link). */
extern const uint8_t  c6fw_data[];
extern const uint32_t c6fw_len;

#define CHUNK 1400  /* = rpc_wrap.c CHUNK_SIZE */

void app_main(void)
{
    tab5_expander_init();   /* C6 power + antenna — MUST precede hosted init */

    ESP_LOGI(TAG, "bringing up ESP-Hosted (esp_hosted 1.4.0, SDIO 12/13/11/10/9/8 rst15)...");
    ESP_ERROR_CHECK(esp_hosted_init());
    /* Kick the slave reset + SDIO probe (normally done lazily by the remote
     * esp_wifi_init). Blocks internally until the link handshakes. */
    ESP_LOGI(TAG, "transport_drv_reconfigure (slave reset + SDIO probe)...");
    ESP_ERROR_CHECK(transport_drv_reconfigure());

    /* Belt: confirm the tx path agrees before issuing RPC — an RPC while
     * transport_up(0) trips a double-free in the tx error path (tlsf
     * assert) and boot-loops the P4. */
    ESP_LOGI(TAG, "waiting for SDIO transport up...");
    int waited_ms = 0;
    while (!is_transport_tx_ready() && waited_ms < 30000) {
        vTaskDelay(pdMS_TO_TICKS(200));
        waited_ms += 200;
    }
    if (!is_transport_tx_ready()) {
        ESP_LOGE(TAG, "transport never came up after %d ms — check SDIO wiring/reset", waited_ms);
        for (;;) vTaskDelay(pdMS_TO_TICKS(5000));
    }
    ESP_LOGI(TAG, "transport up after %d ms; settling 1s before RPC", waited_ms);
    vTaskDelay(pdMS_TO_TICKS(1000));

    size_t len = (size_t)c6fw_len;
    ESP_LOGW(TAG, "C6 slave OTA starting: %u bytes. DO NOT POWER OFF.", (unsigned)len);

    int err = rpc_ota_begin();
    if (err) { ESP_LOGE(TAG, "ota_begin failed: %d", err); goto halt; }

    static uint8_t buf[CHUNK];
    for (size_t off = 0; off < len; off += CHUNK) {
        size_t n = len - off < CHUNK ? len - off : CHUNK;
        memcpy(buf, c6fw_data + off, n);
        err = rpc_ota_write(buf, (uint32_t)n);
        if (err) { ESP_LOGE(TAG, "ota_write failed at %u: %d", (unsigned)off, err); goto halt; }
        if ((off % (128 * 1024)) < CHUNK) {
            ESP_LOGI(TAG, "progress %u/%u (%u%%)", (unsigned)off, (unsigned)len,
                     (unsigned)(off * 100 / len));
        }
    }

    err = rpc_ota_end();
    if (err) { ESP_LOGE(TAG, "ota_end failed: %d", err); goto halt; }

    ESP_LOGW(TAG, "=== OTA done — slave reboots itself into the new firmware ===");
    ESP_LOGW(TAG, "=== Now reflash the real Tab5 firmware (env m5stack-tab5) ===");

halt:
    if (err) ESP_LOGE(TAG, "=== OTA FAILED (err=%d) — C6 unchanged, safe to retry ===", err);
    for (;;) vTaskDelay(pdMS_TO_TICKS(5000));
}
