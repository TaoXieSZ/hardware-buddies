// GIF face player. Playback pattern (file callbacks, RGB565_BE palette,
// playFrame pacing, loop-by-reopen) follows the proven CoreS3 pipeline in
// hardware-buddies/claude-code-buddy character_chan.cpp. Clawd GIFs keep
// their native pixel size and are centred in the visible face band.
#include "gif_face.h"
#include <M5Unified.h>
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <esp_system.h>
#include <cstring>

namespace {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr int MAX_Y_NUDGE = 8;

const char* RANDOM_FILES[] = {
    "/characters/clawd/idle.gif",
    "/characters/clawd/clawd-idle-reading.gif",
    "/characters/clawd/busy_0.gif",
    "/characters/clawd/busy_1.gif",
    "/characters/clawd/busy_2.gif",
    "/characters/clawd/busy_3.gif",
    "/characters/clawd/attention.gif",
    "/characters/clawd/celebrate.gif",
    "/characters/clawd/clawd-thinking.gif",
    "/characters/clawd/clawd-carrying.gif",
    "/characters/clawd/error-120.gif",
    "/characters/clawd/dizzy.gif",
    "/characters/clawd/heart.gif",
};
constexpr int RANDOM_FILE_COUNT = sizeof(RANDOM_FILES) / sizeof(RANDOM_FILES[0]);

const char* IDLE_FALLBACK = "/characters/clawd/idle.gif";
const char* REMINDER_FILE = "/characters/clawd/clawd-notification.gif";
const char* SLEEP_FILE    = "/characters/clawd/sleep.gif";
const char* MEETING_FILE  = "/characters/clawd/clawd-thinking.gif";
const char* BREAK_FILE    = "/characters/clawd/heart.gif";
const char* CELEBRATE_FILE = "/characters/clawd/celebrate.gif";

AnimatedGIF g_gif;
File        g_file;
bool        g_open        = false;
uint8_t     g_state       = 0xFF;
uint32_t    g_nextFrameAt = 0;
int         g_bandY       = 0;    // 0 = draw full height
int         g_yOffset     = 0;    // centred-layout nudge, limited to +/-8px
int         g_originX     = 0;
int         g_originY     = 0;
int         g_lastRandom  = -1;
const char* g_currentFile = IDLE_FALLBACK;
uint16_t    g_line[SCREEN_W];

int visibleHeight() {
    if (g_bandY <= 0 || g_bandY > SCREEN_H) return SCREEN_H;
    return g_bandY;
}

void updateLayout() {
    if (!g_open) return;
    g_originX = (SCREEN_W - g_gif.getCanvasWidth()) / 2;
    int faceH = visibleHeight();
    int gifH  = g_gif.getCanvasHeight();
    g_originY = (faceH - gifH) / 2 + g_yOffset;
    if (gifH <= faceH) {
        if (g_originY < 0) g_originY = 0;
        int maxY = faceH - gifH;
        if (g_originY > maxY) g_originY = maxY;
    }
}

void clearFaceBand() {
    M5.Display.fillRect(0, 0, SCREEN_W, visibleHeight(), TFT_BLACK);
}

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
    int y = d->iY + d->y + g_originY;
    if (g_bandY > 0 && y >= g_bandY) return;   // text band owns those rows
    if (y < 0 || y >= SCREEN_H) return;

    uint16_t* pal  = d->pPalette;
    uint8_t*  src  = d->pPixels;
    uint8_t   tc   = d->ucTransparent;
    bool      hasT = d->ucHasTransparency;
    int       dstX = d->iX + g_originX;
    int       w    = d->iWidth;
    if (dstX < 0) {
        int skip = -dstX;
        if (skip >= w) return;
        src += skip;
        w -= skip;
        dstX = 0;
    }
    if (dstX + w > SCREEN_W) w = SCREEN_W - dstX;
    if (w <= 0) return;

    if (hasT && d->ucDisposalMethod == 2) {
        // AnimatedGIF's disposal=2 contract restores transparent pixels to
        // the GIF background. Drawing them as transparent would retain the
        // previous frame and leave trails in several Clawd animations.
        for (int x = 0; x < w; x++) {
            uint8_t index = (src[x] == tc) ? d->ucBackground : src[x];
            g_line[x] = pal[index];
        }
        M5.Display.pushImage(dstX, y, w, 1, g_line);
    } else if (hasT) {
        // Delta frames mark unchanged pixels transparent = keep what's on
        // screen. Push only the opaque runs; painting transparent pixels any
        // solid colour would erase the previous frame (black-cat bug).
        int x = 0;
        while (x < w) {
            while (x < w && src[x] == tc) x++;
            int runStart = x, n = 0;
            while (x < w && src[x] != tc) g_line[n++] = pal[src[x++]];
            if (n) M5.Display.pushImage(dstX + runStart, y, n, 1, g_line);
        }
    } else {
        for (int x = 0; x < w; x++) g_line[x] = pal[src[x]];
        M5.Display.pushImage(dstX, y, w, 1, g_line);
    }
}

bool tryOpenGif(const char* path) {
    if (g_open) { g_gif.close(); g_open = false; }
    if (!g_gif.open(path, openCb, closeCb, readCb, seekCb, drawCb)) return false;
    g_open = true;
    g_currentFile = path;
    updateLayout();
    return true;
}

bool openWithFallback(const char* path, bool clearBand) {
    if (tryOpenGif(path)) {
        if (clearBand) clearFaceBand();
        return true;
    }

    Serial.printf("[gif] open failed: %s (err=%d)\n", path, g_gif.getLastError());
    if (std::strcmp(path, IDLE_FALLBACK) == 0) return false;

    Serial.printf("[gif] fallback to %s\n", IDLE_FALLBACK);
    if (tryOpenGif(IDLE_FALLBACK)) {
        // A fallback changes the visible asset even when a loop reopen failed,
        // so clear stale pixels before idle starts drawing.
        clearFaceBand();
        return true;
    }
    Serial.printf("[gif] fallback open failed: %s (err=%d)\n",
                  IDLE_FALLBACK, g_gif.getLastError());
    return false;
}

const char* pickRandomFile() {
    int pick;
    if (g_lastRandom < 0) {
        pick = esp_random() % RANDOM_FILE_COUNT;
    } else {
        // Uniform across the other 12 entries: draw 0..11, then skip last.
        pick = esp_random() % (RANDOM_FILE_COUNT - 1);
        if (pick >= g_lastRandom) pick++;
    }
    g_lastRandom = pick;
    return RANDOM_FILES[pick];
}

const char* selectFile(AgentState state) {
    if (state == STATE_SLEEP) return SLEEP_FILE;
    if (state == STATE_REMINDER) return REMINDER_FILE;
    if (state == STATE_MEETING) return MEETING_FILE;
    if (state == STATE_BREAK) return BREAK_FILE;
    if (state == STATE_CELEBRATE) return CELEBRATE_FILE;
    return pickRandomFile();
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
    openWithFallback(selectFile(state), true);
}

void gifFaceForceState(AgentState state) {
    g_state = (uint8_t)state;
    g_nextFrameAt = 0;
    openWithFallback(selectFile(state), true);
}

void gifFaceShowRandom() {
    g_nextFrameAt = 0;
    openWithFallback(pickRandomFile(), true);
}

void gifFaceRefresh() {
    g_nextFrameAt = 0;
    openWithFallback(g_currentFile, true);
}

void gifFaceSetTextBand(int bandY) {
    g_bandY = bandY;
    updateLayout();
}

void gifFaceSetYOffset(int dy) {
    // Native-size Clawd is centred; only deliberate small nudges are accepted.
    g_yOffset = (dy >= -MAX_Y_NUDGE && dy <= MAX_Y_NUDGE) ? dy : 0;
    updateLayout();
}

void gifFaceTick() {
    if (!g_open) return;
    uint32_t now = millis();
    if (now < g_nextFrameAt) return;

    int delayMs = 0;
    int rc = g_gif.playFrame(false, &delayMs);
    if (rc == 0) {                       // last frame done -> loop
        openWithFallback(g_currentFile, false);  // keep the chosen asset
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
