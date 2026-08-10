// GIF face player. Playback pattern (file callbacks, RGB565_BE palette,
// playFrame pacing, loop-by-reopen) follows the proven CoreS3 pipeline in
// hardware-buddies/claude-code-buddy character_chan.cpp. Our GIFs are
// authored at exactly 320x240 so scanlines map 1:1 — no scaling path.
#include "gif_face.h"
#include <M5Unified.h>
#include <LittleFS.h>
#include <AnimatedGIF.h>

namespace {

const char* STATE_FILES[6] = {
    "/characters/cat/cat_idle.gif",      // STATE_IDLE
    "/characters/cat/cat_thinking.gif",  // STATE_THINKING
    "/characters/cat/cat_talking.gif",   // STATE_REPLYING
    "/characters/cat/cat_error.gif",     // STATE_ERROR
    "/characters/cat/cat_talking.gif",   // STATE_REMINDER (reuse talking face)
    "/characters/cat/cat_sleep.gif",     // STATE_SLEEP
};

AnimatedGIF g_gif;
File        g_file;
bool        g_open        = false;
uint8_t     g_state       = 0xFF;
uint32_t    g_nextFrameAt = 0;
int         g_bandY       = 0;    // 0 = draw full height
int         g_yOffset     = 0;    // vertical shift applied to every GIF row
uint16_t    g_line[320];

// --- AnimatedGIF file callbacks (LittleFS) ---------------------------------
void* openCb(const char* fname, int32_t* pSize) {
    g_file = LittleFS.open(fname, "r");
    if (!g_file) return nullptr;
    *pSize = g_file.size();
    return (void*)&g_file;
}
void closeCb(void* handle) {
    File* f = (File*)handle;
    if (f) f->close();
}
int32_t readCb(GIFFILE* pf, uint8_t* buf, int32_t len) {
    File* f = (File*)pf->fHandle;
    int32_t n = (int32_t)f->read(buf, len);
    pf->iPos = f->position();
    return n;
}
int32_t seekCb(GIFFILE* pf, int32_t pos) {
    File* f = (File*)pf->fHandle;
    f->seek(pos);
    pf->iPos = f->position();
    return pos;
}

// --- Draw callback: one palettized source row -> LCD row --------------------
void drawCb(GIFDRAW* d) {
    int y = d->iY + d->y + g_yOffset;
    if (g_bandY > 0 && y >= g_bandY) return;   // text band owns those rows
    if (y < 0 || y >= 240) return;

    uint16_t* pal  = d->pPalette;
    uint8_t*  src  = d->pPixels;
    uint8_t   tc   = d->ucTransparent;
    bool      hasT = d->ucHasTransparency;
    int       w    = d->iWidth;
    if (d->iX + w > 320) w = 320 - d->iX;
    if (w <= 0) return;

    if (hasT) {
        // Delta frames mark unchanged pixels transparent = keep what's on
        // screen. Push only the opaque runs; painting transparent pixels any
        // solid colour would erase the previous frame (black-cat bug).
        int x = 0;
        while (x < w) {
            while (x < w && src[x] == tc) x++;
            int runStart = x, n = 0;
            while (x < w && src[x] != tc) g_line[n++] = pal[src[x++]];
            if (n) M5.Display.pushImage(d->iX + runStart, y, n, 1, g_line);
        }
    } else {
        for (int x = 0; x < w; x++) g_line[x] = pal[src[x]];
        M5.Display.pushImage(d->iX, y, w, 1, g_line);
    }
}

bool openStateGif(uint8_t state) {
    if (g_open) { g_gif.close(); g_open = false; }
    if (!g_gif.open(STATE_FILES[state], openCb, closeCb, readCb, seekCb, drawCb)) {
        Serial.printf("[gif] open failed: %s (err=%d)\n",
                      STATE_FILES[state], g_gif.getLastError());
        return false;
    }
    g_open = true;
    return true;
}

}  // namespace

void gifFaceInit() {
    if (!LittleFS.begin(false)) {
        Serial.println("[gif] LittleFS mount failed — did you run uploadfs?");
        return;
    }
    g_gif.begin(GIF_PALETTE_RGB565_BE);
}

void gifFaceSetState(AgentState state) {
    if ((uint8_t)state == g_state) return;
    g_state = (uint8_t)state;
    g_nextFrameAt = 0;
    openStateGif(g_state);
}

void gifFaceSetTextBand(int bandY) {
    g_bandY = bandY;
}

void gifFaceSetYOffset(int dy) {
    g_yOffset = dy;
}

void gifFaceTick() {
    if (!g_open) return;
    uint32_t now = millis();
    if (now < g_nextFrameAt) return;

    int delayMs = 0;
    int rc = g_gif.playFrame(false, &delayMs);
    if (rc == 0) {                       // last frame done -> loop
        openStateGif(g_state);
        g_nextFrameAt = now + 20;
        return;
    }
    if (rc < 0) {
        Serial.printf("[gif] playFrame err=%d\n", g_gif.getLastError());
        g_gif.close();
        g_open = false;
        return;
    }
    if (delayMs < 16) delayMs = 16;
    g_nextFrameAt = now + delayMs;
}
