/* StackChan Voice Agent -- ESP-IDF entry point.
 * G3 pipeline: Wi-Fi -> Agora RTC -> Conversational AI Agent -> speaker/mic.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

// Agora RTC SDK
#include "agora_rtc_api.h"

// M5Stack CoreS3 (display + ES7210 mic + AW88298 speaker)
#include <M5Unified.h>
// Raw I2S access for direct mic/speaker bypass
#include "driver/i2s_std.h"


// ---- Credential loading ----
#if __has_include("config.h")
  #include "config.h"
#endif
#if __has_include("agora_credentials.h")
  #include "agora_credentials.h"
#endif

// ---- Fallbacks ----
#ifndef WIFI1_SSID
  #define WIFI1_SSID   "YOUR_WIFI_SSID"
#endif
#ifndef WIFI1_PASS
  #define WIFI1_PASS   "YOUR_WIFI_PASS"
#endif
#ifndef AGORA_APP_ID
  #define AGORA_APP_ID "YOUR_AGORA_APP_ID"
#endif

static const char *TAG = "stackchan";

/* ---- Wi-Fi: scan + multi-network ---------------------------------------- */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

struct WifiNet { const char *ssid; const char *pass; };
static const WifiNet KNOWN_NETS[] = {
    { WIFI1_SSID, WIFI1_PASS },
#ifdef WIFI2_SSID
    { WIFI2_SSID, WIFI2_PASS },
#endif
#ifdef WIFI3_SSID
    { WIFI3_SSID, WIFI3_PASS },
#endif
#ifdef WIFI4_SSID
    { WIFI4_SSID, WIFI4_PASS },
#endif
};

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi connected. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_try_connect(int idx) {
    if (idx >= (int)(sizeof(KNOWN_NETS)/sizeof(KNOWN_NETS[0]))) return false;
    // Reset event group before each attempt
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    wifi_config_t wc = {};
    strncpy((char*)wc.sta.ssid, KNOWN_NETS[idx].ssid, sizeof(wc.sta.ssid)-1);
    strncpy((char*)wc.sta.password, KNOWN_NETS[idx].pass, sizeof(wc.sta.password)-1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_STA, &wc);  // ignore if wifi state prevents
    ESP_LOGI(TAG, "Wi-Fi trying [%d]: %s", idx, KNOWN_NETS[idx].ssid);
    esp_wifi_connect();
    EventBits_t b = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (b & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi OK: %s", KNOWN_NETS[idx].ssid);
        return true;
    }
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(1000));
    return false;
}

static bool wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    // M5Unified (M5.begin) may have already initialized netif/event-loop/Wi-Fi.
    // Call init functions but don't abort on "already initialized" errors.
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t r = esp_wifi_init(&cfg);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_wifi_init: %s (continuing)", esp_err_to_name(r));
    }
    // Register handlers — may already be registered; dup registration returns ESP_ERR_INVALID_ARG, ignore.
    esp_event_handler_instance_t i1, i2;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &i1);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &i2);
    r = esp_wifi_set_mode(WIFI_MODE_STA);
    if (r == ESP_ERR_WIFI_NOT_INIT) { ESP_LOGW(TAG, "wifi not init"); return false; }
    r = esp_wifi_start();
    // ESP_ERR_WIFI_STATE = wifi already started (by M5Unified) — fine.
    if (r != ESP_OK && r != ESP_ERR_WIFI_STATE) {
        ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(r));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    int nn = sizeof(KNOWN_NETS) / sizeof(KNOWN_NETS[0]);
    for (int i = 0; i < nn; i++) {
        if (wifi_try_connect(i)) return true;
    }
    ESP_LOGE(TAG, "No known Wi-Fi connected after trying %d networks", nn);
    // Diagnostic: scan and log visible SSIDs so we can debug why our nets aren't seen
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_wifi_scan_start(&sc, true);
    uint16_t n_ap = 0;
    esp_wifi_scan_get_ap_num(&n_ap);
    wifi_ap_record_t aps[10];
    esp_wifi_scan_get_ap_records(&n_ap, aps);
    ESP_LOGE(TAG, "Visible APs (%u):", n_ap > 10 ? 10 : n_ap);
    for (int i = 0; i < (int)n_ap && i < 10; i++) {
        ESP_LOGE(TAG, "  [%d] '%s' rssi=%d auth=%d",
                 i, aps[i].ssid, aps[i].rssi, aps[i].authmode);
    }
    return false;
}


/* ---- Audio pipeline ----
 * Half-duplex turn-taking (no AEC): mic and speaker share I2S_NUM_1 on the
 * CoreS3, and skipping capture while the agent talks avoids echo loops.
 *   LISTEN: M5.Mic 20ms PCM chunks -> agora_rtc_send_audio_data
 *   SPEAK : on_audio_data -> stream buffer -> M5.Speaker.playRaw
 * Switch to SPEAK as soon as remote audio arrives; back to LISTEN after
 * SPEAK_HANG_MS of silence and the speaker queue drains.
 */
static constexpr int      SAMPLE_RATE     = 16000;
static constexpr size_t   CHUNK_SAMPLES   = 320;               // 20ms @16k mono
static constexpr size_t   CHUNK_BYTES     = CHUNK_SAMPLES * 2; // int16
static constexpr size_t   RX_BUF_BYTES    = 96000;             // ~3s of PCM
// Long hang: TTS sentence pauses must NOT reopen the mic, or the agent hears
// its own trailing audio through the room and answers itself in a loop.
static constexpr uint32_t SPEAK_HANG_MS   = 1500;
// After returning to LISTEN, drop mic input briefly (room reverb tail).
static constexpr uint32_t LISTEN_WARMUP_MS = 300;

static StreamBufferHandle_t g_rx_audio  = nullptr;
static volatile uint32_t    g_last_rx_ms = 0;

/* ---- Agora RTC ---- */
static connection_id_t g_conn_id = 0;
static bool g_rtc_joined = false;

static void on_join_success(connection_id_t conn_id, uint32_t uid, int elapsed) {
    ESP_LOGI(TAG, "Agora: joined channel uid=%lu, elapsed=%dms", uid, elapsed);
    g_rtc_joined = true;
}

static void on_connection_lost(connection_id_t conn_id) {
    ESP_LOGW(TAG, "Agora: connection lost");
    g_rtc_joined = false;
}

static void on_audio_data(connection_id_t conn_id, const uint32_t uid,
                           uint16_t sent_ts, const void *data, size_t len,
                           const audio_frame_info_t *info) {
    if (!g_rx_audio) return;
    // Drop the frame if the buffer is full (agent talks faster than playback
    // drains) — stale audio is worse than a hiccup.
    xStreamBufferSend(g_rx_audio, data, len, 0);
    g_last_rx_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

static void on_error_cb(connection_id_t conn_id, int code, const char *msg) {
    ESP_LOGE(TAG, "Agora error [%d]: %s", code, msg ? msg : "");
}

static bool agora_init(void) {
    agora_rtc_event_handler_t handler = {};
    handler.on_join_channel_success = on_join_success;
    handler.on_connection_lost      = on_connection_lost;
    handler.on_audio_data           = on_audio_data;
    handler.on_error                = on_error_cb;

    rtc_service_option_t opt = {};
    opt.area_code = AREA_CODE_GLOB;
    opt.log_cfg.log_level = RTC_LOG_WARNING;
    opt.log_cfg.log_disable = false;
    opt.domain_limit = false;

    int r = agora_rtc_init(AGORA_APP_ID, &handler, &opt);
    if (r < 0) {
        ESP_LOGE(TAG, "agora_rtc_init failed: %s", agora_rtc_err_2_str(r));
        return false;
    }
    ESP_LOGI(TAG, "Agora SDK initialized");

    r = agora_rtc_create_connection(&g_conn_id);
    if (r < 0) {
        ESP_LOGE(TAG, "agora_rtc_create_connection failed: %s", agora_rtc_err_2_str(r));
        return false;
    }

    rtc_channel_options_t ch_opt = {};
    ch_opt.audio_codec_opt.audio_codec_type = AUDIO_CODEC_DISABLED;  // raw PCM — no codec negotiation
    ch_opt.audio_codec_opt.pcm_sample_rate  = 16000;
    ch_opt.audio_codec_opt.pcm_channel_num  = 1;
    ch_opt.auto_subscribe_audio = true;

    // Join with retry: after a reflash/reboot our previous uid-5678 session
    // can linger server-side for a few seconds (CCGA code=11 rejections)
    // before Agora lets the same uid rejoin. Never halt — retry, then reboot.
    for (int attempt = 1; attempt <= 3; attempt++) {
        r = agora_rtc_join_channel(g_conn_id, STACKCHAN_CHANNEL_NAME, 5678,
                                   AGORA_RTC_TOKEN, &ch_opt);
        if (r < 0) {
            ESP_LOGE(TAG, "join_channel call failed: %s", agora_rtc_err_2_str(r));
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        ESP_LOGI(TAG, "Agora: joining '%s' as uid 5678 (attempt %d)...",
                 STACKCHAN_CHANNEL_NAME, attempt);
        for (int w = 0; w < 75 && !g_rtc_joined; w++) {   // wait up to 15s
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (g_rtc_joined) {
            ESP_LOGI(TAG, "Agora RTC: connected and streaming");
            return true;
        }
        ESP_LOGW(TAG, "Agora: join attempt %d timed out, leaving and retrying", attempt);
        agora_rtc_leave_channel(g_conn_id);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    ESP_LOGE(TAG, "Agora: all join attempts failed — rebooting");
    esp_restart();
    return false;   // unreachable
}


/* ---- Audio task: half-duplex mic/speaker state machine ---- */
enum class AudioState { LISTEN, SPEAK };

// I2S driver handle for raw mic capture on CoreS3.
// Mic: ES7210 ADC on I2S_NUM_1, GPIO 14 DIN, shared BCLK/WS/MCLK with speaker.
// We use M5.Mic.begin() only as a "disable-speaker-gpio" convenience;
// the actual reading is done via i2s_read() directly (M5.Mic.record() can
// block too long and its FreeRTOS helper task adds latency).
static i2s_chan_handle_t g_rx_i2s = nullptr;

static void raw_i2s_start(void) {
    // Release speaker so its GPIOs don't collide on I2S_NUM_1.
    M5.Speaker.end();
    // Let M5.Mic.begin() configure the ES7210 codec (I2C) + I2S pins.
    if (!M5.Mic.begin()) {
        ESP_LOGE(TAG, "M5.Mic.begin() FAILED — falling back to raw I2S init");
        static i2s_chan_config_t ch = {
            .id = I2S_NUM_1,
            .role = I2S_ROLE_MASTER,
            .dma_desc_num = 8,
            .dma_frame_num = 320,
            .auto_clear = true,
        };
        static i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = { .mclk = GPIO_NUM_0, .bclk = GPIO_NUM_34, .ws = GPIO_NUM_33, .dout = I2S_GPIO_UNUSED, .din = GPIO_NUM_14, .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } },
        };
        i2s_new_channel(&ch, nullptr, &g_rx_i2s);
        i2s_channel_init_std_mode(g_rx_i2s, &std_cfg);
        i2s_channel_enable(g_rx_i2s);
    }
    ESP_LOGI(TAG, "mic on (raw i2s=%d)", g_rx_i2s != nullptr);
}
static void raw_i2s_stop(void) {
    if (g_rx_i2s) { i2s_channel_disable(g_rx_i2s); i2s_del_channel(g_rx_i2s); g_rx_i2s = nullptr; }
    M5.Mic.end();
}
static bool raw_i2s_read(int16_t *buf, size_t samples) {
    size_t bytes;
    if (g_rx_i2s)
        return i2s_channel_read(g_rx_i2s, buf, samples * 2, &bytes, pdMS_TO_TICKS(50)) == ESP_OK;
    return M5.Mic.record(buf, samples, 16000, false);
}
static void set_listen(void) {
    // Always try M5Mic fallback first; raw_i2s_start() guards itself.
    raw_i2s_start();
}
static void set_speak(void) {
    raw_i2s_stop();
    M5.Speaker.begin();
    M5.Speaker.setVolume(200);  // re-apply after begin() resets it
    M5.Speaker.setChannelVolume(0, 200);
    ESP_LOGI(TAG, "speaker begin ok=%d enabled=%d", M5.Speaker.begin(), M5.Speaker.isEnabled());
}

static void audio_task(void *arg) {
    static int16_t pcm[CHUNK_SAMPLES];
    AudioState state = AudioState::LISTEN;
    uint32_t listen_since_ms = 0;
    uint32_t chunks_sent    = 0;
    uint32_t mic_fail_count = 0;
    uint32_t last_diag_ms   = 0;
    set_listen();
    ESP_LOGI(TAG, "audio task: LISTEN");

    while (true) {
        if (state == AudioState::LISTEN) {
            // Remote audio waiting? switch to playback.
            if (xStreamBufferBytesAvailable(g_rx_audio) >= CHUNK_BYTES) {
                state = AudioState::SPEAK;
                set_speak();
                ESP_LOGI(TAG, "audio task: SPEAK (buffer was %u bytes)",
                         (unsigned)xStreamBufferBytesAvailable(g_rx_audio));
                continue;
            }
            // Capture one 20ms chunk (blocking); publish unless still inside
            // the warm-up window (drops the reverb tail of the agent's voice).
            if (g_rtc_joined && raw_i2s_read(pcm, CHUNK_SAMPLES)) {
                uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
                chunks_sent++;
                if (now - listen_since_ms >= LISTEN_WARMUP_MS) {
                    audio_frame_info_t info = {};
                    info.data_type = AUDIO_DATA_TYPE_PCM;
                    agora_rtc_send_audio_data(g_conn_id, pcm, CHUNK_BYTES, &info);
                }
                if (now - last_diag_ms >= 10000) {
                    ESP_LOGI(TAG, "mic diag: chunks=%lu sent, fails=%lu, rx_buf=%u",
                             chunks_sent, mic_fail_count,
                             (unsigned)xStreamBufferBytesAvailable(g_rx_audio));
                    last_diag_ms = now; chunks_sent = 0; mic_fail_count = 0;
                }
            } else {
                mic_fail_count++;
                if (mic_fail_count == 1 || mic_fail_count % 500 == 0)
                    ESP_LOGW(TAG, "M5.Mic.record FAILED (count=%lu)", mic_fail_count);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        } else { // SPEAK
            // Coalesce arriving chunks into ~100ms batches before playRaw()
            // to reduce I2S-DMA restart overhead (20ms chunks cause stutter).
            static int16_t coalesce[6 * CHUNK_SAMPLES];    // 1920 samples = 120ms max
            static int      coal_idx  = 0;
            static uint32_t coal_last_ms = 0;
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

            // Drain stream buffer into coalesce; play as soon as any data
            // lands (don't wait for full buffer — the first chunk of a TTS
            // sentence is often only 1–2 chunks and waiting causes dead air).
            bool played = false;
            while (coal_idx < 6) {
                size_t n = xStreamBufferReceive(g_rx_audio,
                    coalesce + coal_idx * CHUNK_SAMPLES, CHUNK_BYTES, 0);
                if (n == 0) break;
                coal_idx++;
                coal_last_ms = now;
            }
            // Flush coalesce if: buffer full, or 60ms since last chunk arrived
            if (coal_idx > 0 && (coal_idx >= 6 || now - coal_last_ms > 60)) {
                size_t nsamp = coal_idx * CHUNK_SAMPLES;
                bool ok = M5.Speaker.playRaw(coalesce, nsamp, SAMPLE_RATE, false, 1, 0, false);
                if (!ok) { coal_idx = 0; } // discard on failure
                else { played = true; coal_idx = 0; }
            }

            bool hang_expired = (now - g_last_rx_ms > SPEAK_HANG_MS);
            if (!played && hang_expired && !M5.Speaker.isPlaying()) {
                coal_idx = 0;
                state = AudioState::LISTEN;
                set_listen();
                listen_since_ms = (uint32_t)(esp_timer_get_time() / 1000);
                ESP_LOGI(TAG, "audio task: LISTEN");
            }
        }
    }
}


extern "C" void app_main(void);

void app_main(void) {
    ESP_LOGI(TAG, "===== StackChan Voice Agent (ESP-IDF) =====");

    // 1. NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Credential check
    ESP_LOGI(TAG, "Credentials: AgoraAppID=%s, Channel=%s",
             AGORA_APP_ID, STACKCHAN_CHANNEL_NAME);

    // 3. M5Unified (display + ES7210 mic + AW88298 speaker)
    {
        auto cfg = M5.config();
        cfg.internal_mic = true;
        cfg.internal_spk = true;
        M5.begin(cfg);
        // Mic gain boost: CoreS3 MEMS mics are quiet, need ~20dB gain for office voice pickup
        auto mic_cfg = M5.Mic.config();
        mic_cfg.magnification = 8;  // 8x digital gain (~18dB) — office desks need boost
        M5.Mic.config(mic_cfg);
        M5.Display.setBrightness(80);
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.setCursor(10, 80);

        // Hardware speaker self-test: play a short beep at boot so the user
        // can confirm the amp+speaker path works.
        M5.Speaker.setVolume(200);
        M5.Speaker.setChannelVolume(0, 200);
        M5.Display.println("speaker test: beep!");
        // Generate a 200ms 1kHz sine wave at 16kHz sample rate
        static int16_t beep[3200]; // 200ms @ 16kHz
        for (int i = 0; i < 3200; i++)
            beep[i] = (int16_t)(16000.0 * sinf(2.0f * M_PI * 1000.0f * i / 16000.0f));
        M5.Speaker.playRaw(beep, 3200, 16000, false, 1, 0, true);
        vTaskDelay(pdMS_TO_TICKS(300));
        M5.Speaker.setVolume(200);
        M5.Display.println("booting...");
    }

    // 4. Wi-Fi: scan for known networks, auto-select
    ESP_LOGI(TAG, "Scanning Wi-Fi...");
    M5.Display.println("scanning Wi-Fi...");
    bool net_ok = wifi_init_sta();
    if (!net_ok) {
        ESP_LOGE(TAG, "No network — halting");
        M5.Display.println("No network");
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }

    // 5. Agora RTC
    ESP_LOGI(TAG, "Initializing Agora RTC...");
    if (!agora_init()) {
        ESP_LOGE(TAG, "Agora init FAILED. Halting.");
        M5.Display.println("Agora FAILED");
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }

    // 6. Audio pipeline
    g_rx_audio = xStreamBufferCreate(RX_BUF_BYTES, CHUNK_BYTES);
    ESP_LOGI(TAG, "creating audio task (stack=16384)...");
    auto taskHdl = xTaskCreatePinnedToCore(audio_task, "audio", 16384, nullptr, 15, nullptr, 1);
    ESP_LOGI(TAG, "audio task created: %s", taskHdl == pdPASS ? "OK" : "FAILED");

    ESP_LOGI(TAG, "===== G3 Voice Pipeline ACTIVE =====");
    M5.Display.println("listening");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Heartbeat -- free heap: %lu bytes, rtc_joined=%d, rx_buf=%u",
                 esp_get_free_heap_size(), g_rtc_joined,
                 (unsigned)xStreamBufferBytesAvailable(g_rx_audio));
    }
}
