// See camera_height.h for the why. Pin/clock config follows M5's official
// CoreS3 camera example (docs.m5stack.com/en/arduino/m5cores3/camera).
#include "camera_height.h"
#include <M5Unified.h>
#include "esp_camera.h"

namespace {

constexpr int GW = 80, GH = 60;        // diff grid (320x240 / 4)
constexpr uint8_t DIFF_TH = 24;        // per-cell luma delta to count as changed
constexpr float MOTION_RATIO = 0.02f;  // >=2% changed cells = motion

// static, not stack: loopTask stack is only 8KB
uint8_t g_gridA[GW * GH];
uint8_t g_gridB[GW * GH];

// average luma of a 4x4 cell from an RGB565 QVGA frame, scaled to ~0..187
uint8_t cellLuma(const uint16_t* buf, int gx, int gy) {
    uint32_t sum = 0;
    for (int y = 0; y < 4; y++) {
        const uint16_t* row = buf + (gy * 4 + y) * 320 + gx * 4;
        for (int x = 0; x < 4; x++) {
            uint16_t p = row[x];
            sum += ((p >> 11) & 31) * 8 + ((p >> 5) & 63) * 4 + (p & 31) * 8;
        }
    }
    return (uint8_t)(sum / 16 / 4);
}

void fillGrid(const uint16_t* buf, uint8_t* grid) {
    for (int gy = 0; gy < GH; gy++)
        for (int gx = 0; gx < GW; gx++)
            grid[gy * GW + gx] = cellLuma(buf, gx, gy);
}

}  // namespace

CamProbe cameraProbeOnce() {
    CamProbe r{false, false, 0.5f};

    M5.In_I2C.release();   // hand G11/G12 to the camera's SCCB

    camera_config_t cfg = {};
    cfg.pin_pwdn     = -1;
    cfg.pin_reset    = -1;
    cfg.pin_xclk     = -1;
    cfg.pin_sccb_sda = 12;
    cfg.pin_sccb_scl = 11;
    cfg.pin_d7 = 47; cfg.pin_d6 = 48; cfg.pin_d5 = 16; cfg.pin_d4 = 15;
    cfg.pin_d3 = 42; cfg.pin_d2 = 41; cfg.pin_d1 = 40; cfg.pin_d0 = 39;
    cfg.pin_vsync = 46; cfg.pin_href = 38; cfg.pin_pclk = 45;
    cfg.xclk_freq_hz = 20000000;
    cfg.ledc_timer   = LEDC_TIMER_0;
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.pixel_format = PIXFORMAT_RGB565;
    cfg.frame_size   = FRAMESIZE_QVGA;
    cfg.jpeg_quality = 0;
    cfg.fb_count     = 2;
    cfg.fb_location  = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
    cfg.sccb_i2c_port = -1;

    if (esp_camera_init(&cfg) != ESP_OK) {
        M5.In_I2C.begin();
        Serial.println("[cam] init failed");
        return r;
    }

    // warm up: two throwaway frames so auto-exposure settles
    for (int i = 0; i < 2; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
    }

    camera_fb_t* fb1 = esp_camera_fb_get();
    delay(300);
    camera_fb_t* fb2 = esp_camera_fb_get();

    if (fb1 && fb2 && fb1->len == 320 * 240 * 2 && fb2->len == 320 * 240 * 2) {
        fillGrid((const uint16_t*)fb1->buf, g_gridA);
        fillGrid((const uint16_t*)fb2->buf, g_gridB);
        int changed = 0;
        long rowSum = 0;
        for (int gy = 0; gy < GH; gy++) {
            for (int gx = 0; gx < GW; gx++) {
                int i = gy * GW + gx;
                if (abs((int)g_gridA[i] - (int)g_gridB[i]) > DIFF_TH) {
                    changed++;
                    rowSum += gy;
                }
            }
        }
        r.motion = changed >= (int)(GW * GH * MOTION_RATIO);
        if (changed > 0) r.cy = (rowSum / (float)changed) / (float)GH;
        r.ok = true;
    }

    if (fb1) esp_camera_fb_return(fb1);
    if (fb2) esp_camera_fb_return(fb2);
    esp_camera_deinit();
    M5.In_I2C.begin();   // give the bus back to touch / LEDs / IMU / RTC
    return r;
}
